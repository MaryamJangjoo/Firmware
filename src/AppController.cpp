#include "AppController.h"
#include <esp_system.h>

#include "ecosmart_registries.h"
#include "crypto.hpp"
#include "mybus_value_codec.h"

#include <WiFi.h>

#ifndef WIFI_STA
#define WIFI_STA 1
#endif

AppController* AppController::s_instance = nullptr;

AppController::AppController()
    : amp(&Wire),
      bta("mYSpeaker")
{
    s_instance = this;
}


void AppController::begin()
{
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    pinMode(PIN_SR_CLOCK, OUTPUT);
    pinMode(PIN_SR_DATA, OUTPUT);
    pinMode(PIN_SR_LATCH, OUTPUT);

    Serial.begin(115200);
    delay(200);
    Serial.println("System Starting ....");

    pinMode(PIN_I2S_PDN, OUTPUT);
    digitalWrite(PIN_I2S_PDN, HIGH);
    delay(10);
    Serial.println("PDN pin set HIGH (TAS5805M active)");

    // ✅ رجیسترهای محلی (فعلاً فقط Audio) باید قبل از هر جایی که
    // ممکنه GET_REGISTRY/SET_REGISTRY بیاد (یعنی قبل از WebSocket)
    // بایند شده باشن.
    registerLocalRegisters();

    if (!connectToWiFi()) {
        Serial.println("[ERROR] WiFi connection failed. Retrying in 5 seconds...");
        delay(5000);
        ESP.restart();
        return;
    }

    initCloudManager();
    configureMybusAddress();

    Serial.println("[AUTH] Attempting to login...");
    bool loginSuccess = cloudManager->loginUser(
        "tes29t_operator", "SecurePassword@2026", cloudManager->getDeviceId());

    if (loginSuccess) {
        Serial.println("[AUTH] ✅ Login successful!");
    } else {
        Serial.println("[AUTH] ❌ Login failed, trying offline...");
        if (cloudManager->loginOffline("tes29t_operator", "SecurePassword@2026")) {
            Serial.println("[AUTH] ✅ Offline login successful!");
        } else {
            Serial.println("[AUTH] ❌ Offline login failed!");
        }
    }


    if (cloudManager->isLoggedIn()) {
        if (cloudManager->isSecureSessionEstablished()) {
            Serial.println("[mYBUS] ✅ Using restored session from NVS, skipping handshake");
        } else {
            Serial.println("[mYBUS] Starting handshake...");
            if (cloudManager->performHandshake()) {
                Serial.println("[mYBUS] ✅ Handshake successful!");
            } else {
                Serial.println("[mYBUS] ❌ Handshake failed!");
            }
        }
    }

    cloudManager->startWebSocketServer();
    Serial.println("[WS] WebSocket server started on /ws");

    initAudioHardware();

    Serial.println();
    Serial.println("========================================");
    Serial.println("✅ ESP32 Ready! (Phase 1: Audio wired to registries)");
    Serial.println("========================================");
    Serial.println();
}


void AppController::handle()
{
    if (cloudManager != nullptr) {
        cloudManager->loopWebSocketServer();
    }

    handleWiFiReconnect();
    handleLedState();
    tickAudioSleepTimer();
}


bool AppController::connectToWiFi()
{
    Serial.println();
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] ✅ Connected!");
        Serial.print("[WiFi] 📶 IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }
    Serial.println("[WiFi] ❌ Connection failed!");
    return false;
}

void AppController::initCloudManager()
{
    Serial.println("[CLOUD] Initializing CloudManager...");
    cloudManager = new CloudManager();
    cloudManager->setApiBaseUrl("http://192.168.88.198:3000");
    cloudManager->onCommand([this](const JsonDocument& cmd) {
        onCommandReceived(cmd);
    });
    Serial.println("[CLOUD] CloudManager initialized successfully");
}

void AppController::configureMybusAddress()
{
    if (cloudManager == nullptr) {
        return;
    }

    if (cloudManager->getMybusDeviceId() == 0) {
        cloudManager->setMybusDeviceId(MYBUS_DEVICE_ID);
    }

    if (cloudManager->getMybusZoneId() == 0) {
        cloudManager->setMybusZoneId(MYBUS_ZONE_ID);
    }
}

void AppController::initAudioHardware()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (amp.init() != ESP_OK) {
        Serial.println("Failed to initialize TAS5805M");
    } else {
        uint8_t volume = 70;
        if (tas5805m_set_volume_pct(volume) != ESP_OK) {
            ESP_LOGE("TAS5805M", "Failed to set volume");
        }
        if (tas5805m_get_volume_pct(&volume) != ESP_OK) {
            ESP_LOGE("TAS5805M", "Failed to get volume");
        } else {
            ESP_LOGI("TAS5805M", "Current volume: %d", volume);
        }
    }

    bta.begin();
    bta.reconnect();
    bta.I2S(PIN_I2S_SCK, PIN_I2S_SDOUT, PIN_I2S_WS);
    bta.volume(1.0);
    bta.setSinkCallback(&AppController::btDataTrampoline);
}


void AppController::handleWiFiReconnect()
{
    if (WiFi.status() == WL_CONNECTED) {
        if (wifiReconnectState_ == WifiReconnectState::RECONNECTING) {
            Serial.println("[WiFi] ✅ Reconnected!");
            wifiReconnectState_ = WifiReconnectState::IDLE;
        }
        return;
    }

    const unsigned long now = millis();

    if (wifiReconnectState_ == WifiReconnectState::IDLE) {
        Serial.println("[WiFi] Connection lost. Reconnecting...");
        WiFi.reconnect();
        wifiReconnectState_ = WifiReconnectState::RECONNECTING;
        wifiReconnectStartMs_ = now;
        wifiLastAttemptMs_ = now;
        return;
    }

    if (now - wifiLastAttemptMs_ >= WIFI_RECONNECT_ATTEMPT_INTERVAL_MS) {
        wifiLastAttemptMs_ = now;
    
    }
    if (now - wifiReconnectStartMs_ >= WIFI_RECONNECT_TIMEOUT_MS) {
        Serial.println("[WiFi] ❌ Reconnect timeout, will retry on next loop pass");
        wifiReconnectState_ = WifiReconnectState::IDLE;
    }
}

void AppController::handleLedState()
{
    if (lastLedState != ledState) {
        lastLedState = ledState;
        ledState ? setCurtainOn() : setCurtainOff();
    }
}


void AppController::setCurtainOn()
{
    digitalWrite(PIN_SR_LATCH, LOW);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0xff);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0xff);
    digitalWrite(PIN_SR_LATCH, HIGH);
}

void AppController::setCurtainOff()
{
    digitalWrite(PIN_SR_LATCH, LOW);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0);
    digitalWrite(PIN_SR_LATCH, HIGH);
}


void AppController::visualizeAudio(const uint8_t* data, uint32_t len)
{
    int16_t* samples = (int16_t*)data;
    int peak = 0;
    for (uint32_t i = 0; i < len / 2; i++) {
        int16_t val = abs(samples[i]);
        if (val > peak) peak = val;
    }
    if (peak > dynamicMax) dynamicMax = peak;

    if (millis() - lastUpdate > 50) {
        dynamicMax = max(peak, dynamicMax - 5);
        lastUpdate = millis();
    }
    uint8_t level = map(peak, 0, 32767, 0, NUM_LEDS);
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(160 - i * 10, 255, (i < level) ? 255 : 0);
    }
    FastLED.show();
}

void AppController::onBtData(const uint8_t* data, uint32_t len)
{
    size_t written;
    i2s_write(I2S_NUM_0, data, len, &written, portMAX_DELAY);
    visualizeAudio(data, len);
}

void AppController::btDataTrampoline(const uint8_t* data, uint32_t len)
{
    if (s_instance != nullptr) {
        s_instance->onBtData(data, len);
    }
}


// ============================================================
// Local register bindings — Audio
//
// هر آدرس مستقیماً به متغیر عضو مربوطه bind می‌شود؛ نوشتن روی
// رجیستر یعنی نوشتن روی همان حافظه‌ای که ماژول صوتی استفاده
// می‌کند، نه یک کپی جدا که باید دستی sync شود.
// ============================================================

void AppController::registerAudioRegisters()
{
    localRegisters_.bind({
        REG_AUDIO_MODE, DT_UINT8, &audioModeRaw_, sizeof(audioModeRaw_),
        /*writable*/ true, /*mirrorToCloud*/ false, /*isString*/ false,
        this,
        [](void* ctx, void* ptr) {
            auto* self = static_cast<AppController*>(ctx);
            uint8_t mode = *static_cast<uint8_t*>(ptr);
            if (mode == 0) {
                Serial.println("[AUDIO] Mode -> Bluetooth");
                self->bta.reconnect();
            } else {
                Serial.println("[AUDIO] Mode -> Radio (⚠️ hardware not wired)");
            }
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_CONTROL, DT_UINT8, &audioControlRaw_, sizeof(audioControlRaw_),
        true, false, false, this,
        [](void* ctx, void* ptr) {
            auto* self = static_cast<AppController*>(ctx);
            uint8_t cmd = *static_cast<uint8_t*>(ptr);
            switch (cmd) {
                case 1:
                    self->bta.reconnect();
                    Serial.println("[AUDIO] Control -> reconnect");
                    break;
                case 0:
                case 2:
                    Serial.println("[AUDIO] ⚠️ Stop/Pause not wired yet - verify btAudio API");
                    break;
                default:
                    Serial.printf("[AUDIO] ⚠️ Unknown control command: %u\n", cmd);
                    break;
            }
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_SLEEP_TIMER, DT_UINT16, &audioSleepTimerRaw_, sizeof(audioSleepTimerRaw_),
        true, false, false, this,
        [](void* ctx, void* ptr) {
            uint16_t minutes = *static_cast<uint16_t*>(ptr);
            Serial.printf("[AUDIO] Sleep timer set to %u minute(s)\n", minutes);
            static_cast<AppController*>(ctx)->lastSleepTimerTickMs_ = millis();
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_STATION, DT_UINT16, &audioStationRaw_, sizeof(audioStationRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] Station -> %u (⚠️ radio hardware not wired)\n",
                          *static_cast<uint16_t*>(ptr));
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_VOLUME, DT_UINT8, &audioVolumeRaw_, sizeof(audioVolumeRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            uint8_t* vol = static_cast<uint8_t*>(ptr);
            *vol = constrain(*vol, (uint8_t)0, (uint8_t)124);
            esp_err_t ret = tas5805m_set_volume_pct(*vol);
            Serial.printf("[AUDIO] Volume -> %u (%s)\n", *vol, ret == ESP_OK ? "OK" : "FAIL");
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_BASS, DT_UINT8, &audioBassRaw_, sizeof(audioBassRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] Bass -> %u (⚠️ EQ hardware not wired, value stored only)\n",
                          *static_cast<uint8_t*>(ptr));
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_TREBLE, DT_UINT8, &audioTrebleRaw_, sizeof(audioTrebleRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] Treble -> %u (⚠️ EQ hardware not wired, value stored only)\n",
                          *static_cast<uint8_t*>(ptr));
        },
        nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_EQ, DT_UINT8, &audioEqRaw_, sizeof(audioEqRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] EQ preset -> %u (⚠️ EQ hardware not wired, value stored only)\n",
                          *static_cast<uint8_t*>(ptr));
        },
        nullptr
    });

    // Title/Artist: read-only از دید کلاود. منبع واقعی‌شون متادیتای
    // بلوتوثه که فعلاً از btAudio دریافت نمی‌شه (TODO - نیاز به
    // بررسی callback متادیتای کتابخونه). فعلاً فقط رشته‌ی خالی دارن.
    localRegisters_.bind({
        REG_AUDIO_TITLE, DT_STRING, &audioTitle_, 0,
        false, false, true, this, nullptr, nullptr
    });

    localRegisters_.bind({
        REG_AUDIO_ARTIST, DT_STRING, &audioArtist_, 0,
        false, false, true, this, nullptr, nullptr
    });
}

void AppController::registerLocalRegisters()
{
    registerAudioRegisters();
    // بعداً: Curtain / RGB / HVAC هم به همین صورت اضافه می‌شن.
}

void AppController::tickAudioSleepTimer()
{
    if (audioSleepTimerRaw_ == 0) return;

    const unsigned long now = millis();
    if (now - lastSleepTimerTickMs_ < 60000UL) return; // هر یک دقیقه

    lastSleepTimerTickMs_ = now;
    audioSleepTimerRaw_--;

    if (audioSleepTimerRaw_ == 0) {
        Serial.println("[AUDIO] ⏰ Sleep timer expired (⚠️ actual stop not wired - verify btAudio API)");
    }
}


void AppController::onCommandReceived(const JsonDocument& command)
{
    Serial.println("[CMD] Command received:");
    serializeJson(command, Serial);
    Serial.println();

    String action = command["action"] | "";

    if (action == "REBOOT") {
        Serial.println("[CMD] Rebooting ESP32...");
        ESP.restart();

    } else if (action == "STATUS") {
        Serial.println("[CMD] Status requested");

    } else if (action == "GET_REGISTRY") {
        uint16_t regAddr = command["RegAdd"] | 0;
        Serial.printf("[CMD] Get registry: 0x%04X\n", regAddr);

        // ✅ رجیسترهای محلی (فعلاً Audio) مستقیم از حافظه خونده
        // می‌شن - نیازی به رفتن سراغ mYBUS نیست.
        if (localRegisters_.isLocal(regAddr)) {
            JsonDocument response;
            if (localRegisters_.readValueToJson(regAddr, response)) {
                Serial.println("[CMD] ✅ Local register read");
                if (cloudManager != nullptr && cloudManager->isWebSocketConnected()) {
                    JsonDocument wsMsg;
                    wsMsg["type"] = "registry_response";
                    wsMsg["RegAdd"] = regAddr;
                    wsMsg["value"] = response["value"];
                    cloudManager->sendRealtimeData(wsMsg);
                }
            } else {
                Serial.println("[CMD] ❌ Local register read failed");
            }
            return;
        }

        if (cloudManager != nullptr && cloudManager->isSecureSessionEstablished()) {
            JsonDocument req;
            req["RegAdd"] = regAddr;
            req["RegVal"] = "";

            JsonDocument response;
            bool sent = cloudManager->sendMybusData(req, &response);
            bool readSuccess = sent && (response["success"] | false);

            if (readSuccess) {
                Serial.println("[CMD] ✅ Registry response received:");
                serializeJson(response, Serial);
                Serial.println();

                if (cloudManager->isWebSocketConnected()) {
                    JsonDocument wsMsg;
                    wsMsg["type"] = "registry_response";
                    wsMsg["RegAdd"] = regAddr;
                    if (!response["value"].isNull()) {
                        wsMsg["value"] = response["value"];
                    }
                    wsMsg["payloadHex"] = response["payloadHex"] | "";
                    cloudManager->sendRealtimeData(wsMsg);
                }
            } else {
                Serial.println("[CMD] ❌ Failed to get registry / no response decoded");
            }
        }

    } else if (action == "SET_REGISTRY") {
        uint16_t regAddr = command["RegAdd"] | 0;
        String regVal = command["RegVal"] | "";
        Serial.printf("[CMD] Set registry: 0x%04X = %s\n", regAddr, regVal.c_str());

        bool handledLocally = false;

        // ✅ Audio (و هر رجیستر دیگری که در آینده register بشه) از
        // طریق LocalRegisterMap نوشته می‌شه.
        if (localRegisters_.isLocal(regAddr)) {
            handledLocally = localRegisters_.writeValueFromString(regAddr, regVal);
            Serial.println(handledLocally
                ? "[CMD] ✅ Handled locally (register map)"
                : "[CMD] ❌ Local register write failed");
        }
        // ⚠️ Curtain هنوز به LocalRegisterMap منتقل نشده - مسیر قدیمی
        // موقتاً همین‌جا inline نگه داشته شده تا رفتار قبلی حفظ بشه.
        else if (regAddr == REG_CURTAIN_STATE) {
            int state = regVal.toInt();
            ledState = (state != 0);
            Serial.printf("[CURTAIN] State -> %s\n", ledState ? "OPEN" : "CLOSE");
            handledLocally = true;
        }

        if (!handledLocally) {
            Serial.println("[CMD] ℹ️ Register not part of local map - forwarding to mYBUS only");
        }

        // اگر رجیستر محلی بود و mirrorToCloud=false، دیگه نیازی به
        // فرستادن فریم mYBUS نیست.
        if (localRegisters_.isLocal(regAddr) && !localRegisters_.shouldMirrorToCloud(regAddr)) {
            return;
        }

        if (cloudManager != nullptr && cloudManager->isSecureSessionEstablished()) {
            JsonDocument req;
            req["RegAdd"] = regAddr;
            req["RegVal"] = regVal;
            cloudManager->sendMybusData(req);
        }
    }
}