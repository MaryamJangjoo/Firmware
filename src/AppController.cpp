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
        "tes29t_operator", "SecurePassword@20266", cloudManager->getDeviceId());

    if (loginSuccess) {
        Serial.println("[AUTH] ✅ Login successful!");
    } else {
        Serial.println("[AUTH] ❌ Login failed, trying offline...");
        if (cloudManager->loginOffline("tes29t_operator", "SecurePassword@20266")) {
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

    // ✅ پردازش دستورات Serial
    handleSerialCommands();
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
    cloudManager->setApiBaseUrl("http://192.168.88.98:3000");

    cloudManager->onCommand([this](const JsonDocument& cmd) {
        onCommandReceived(cmd);
    });

    cloudManager->onLocalRegistryRead([this](uint16_t addr, JsonDocument& out) {
        if (!localRegisters_.isLocal(addr)) {
            return false;
        }
        return localRegisters_.readValueToJson(addr, out);
    });

    cloudManager->onShouldSkipMybusWrite([this](uint16_t addr) {
        return localRegisters_.isLocal(addr) &&
               !localRegisters_.shouldMirrorToCloud(addr);
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
        if (tas5805m_set_volume_pct(audioVolumeRaw_) != ESP_OK) {
            ESP_LOGE("TAS5805M", "Failed to set volume");
        }

        uint8_t actualVolume = 0;
        if (tas5805m_get_volume_pct(&actualVolume) != ESP_OK) {
            ESP_LOGE("TAS5805M", "Failed to get volume");
        } else {
            audioVolumeRaw_ = actualVolume;
            ESP_LOGI("TAS5805M", "Current volume: %d", audioVolumeRaw_);
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
// ============================================================

void AppController::registerAudioRegisters()
{
    // ============================================================
    // 1. Mode - 0x8101 (u8, R/W)
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_MODE, DT_UINT8, &audioModeRaw_, sizeof(audioModeRaw_),
        true, false, false, this,
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

    // ============================================================
    // 2. Control - 0x8102 (u8, R/W)
    // ============================================================
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

    // ============================================================
    // 3. Sleep Timer - 0x8221 (u16, R/W)
    // ============================================================
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

    // ============================================================
    // 4. Station - 0x8222 (u16, R/W)
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_STATION, DT_UINT16, &audioStationRaw_, sizeof(audioStationRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] Station -> %u (⚠️ radio hardware not wired)\n",
                          *static_cast<uint16_t*>(ptr));
        },
        nullptr
    });

    // ============================================================
    // 5. Volume - 0x8103 (u8, R/W)
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_VOLUME, DT_UINT8, &audioVolumeRaw_, sizeof(audioVolumeRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            uint8_t requested = *static_cast<uint8_t*>(ptr);
            uint8_t clamped = constrain(requested, (uint8_t)0, (uint8_t)124);
            esp_err_t ret = tas5805m_set_volume_pct(clamped);
            Serial.printf("[AUDIO] Volume -> %u (requested %u, %s)\n",
                          clamped, requested, ret == ESP_OK ? "OK" : "FAIL");
        },
        nullptr
    });

    // ============================================================
    // 6. Bass - 0x8104 (u8, R/W)
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_BASS, DT_UINT8, &audioBassRaw_, sizeof(audioBassRaw_),
        true, false, false, this,
        [](void*, void* ptr) {
            Serial.printf("[AUDIO] Bass -> %u (⚠️ EQ hardware not wired, value stored only)\n",
                          *static_cast<uint8_t*>(ptr));
        },
        nullptr
    });

    // ============================================================
    // 7. Title - 0x0800 (string, R) ⚠️ اصلاح‌شده از 0x0700
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_TITLE, DT_STRING, &audioTitle_, 0,
        false, false, true, this, nullptr, nullptr
    });

    // ============================================================
    // 8. Artist - 0x0801 (string, R) ⚠️ اصلاح‌شده از 0x0701
    // ============================================================
    localRegisters_.bind({
        REG_AUDIO_ARTIST, DT_STRING, &audioArtist_, 0,
        false, false, true, this, nullptr, nullptr
    });
}

void AppController::registerLocalRegisters()
{
    registerAudioRegisters();
    // بعداً: Curtain / RGB / HVAC
}

void AppController::tickAudioSleepTimer()
{
    if (audioSleepTimerRaw_ == 0) return;

    const unsigned long now = millis();
    if (now - lastSleepTimerTickMs_ < 60000UL) return;

    lastSleepTimerTickMs_ = now;
    audioSleepTimerRaw_--;

    if (audioSleepTimerRaw_ == 0) {
        Serial.println("[AUDIO] ⏰ Sleep timer expired");
    }
}

// ============================================================
// Serial Commands Handler
// ============================================================

void AppController::handleSerialCommands()
{
    if (!Serial.available()) return;

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.isEmpty()) return;

    // ---- READ COMMANDS ----

    if (cmd == "getvol" || cmd == "getvolume") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_VOLUME, doc)) {
            Serial.printf("[VOLUME] Current: %s\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[VOLUME] ❌ Read failed");
        }
        return;
    }

    if (cmd == "getbass") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_BASS, doc)) {
            Serial.printf("[BASS] Current: %s\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[BASS] ❌ Read failed");
        }
        return;
    }


    if (cmd == "getmode") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_MODE, doc)) {
            int mode = doc["value"].as<int>();
            Serial.printf("[MODE] Current: %d (%s)\n", mode, mode == 0 ? "Bluetooth" : "Radio");
        } else {
            Serial.println("[MODE] ❌ Read failed");
        }
        return;
    }

    if (cmd == "gettitle") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_TITLE, doc)) {
            Serial.printf("[TITLE] %s\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[TITLE] ❌ Read failed");
        }
        return;
    }

    if (cmd == "getartist") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_ARTIST, doc)) {
            Serial.printf("[ARTIST] %s\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[ARTIST] ❌ Read failed");
        }
        return;
    }

    if (cmd == "getsleep") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_SLEEP_TIMER, doc)) {
            Serial.printf("[SLEEP TIMER] %s minutes\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[SLEEP TIMER] ❌ Read failed");
        }
        return;
    }

    if (cmd == "getstation") {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(REG_AUDIO_STATION, doc)) {
            Serial.printf("[STATION] %s\n", doc["value"].as<String>().c_str());
        } else {
            Serial.println("[STATION] ❌ Read failed");
        }
        return;
    }

    // ---- WRITE COMMANDS ----

    if (cmd.startsWith("setvol ")) {
        int vol = cmd.substring(7).toInt();
        vol = constrain(vol, 0, 100);
        if (localRegisters_.writeValueFromString(REG_AUDIO_VOLUME, String(vol))) {
            Serial.printf("[VOLUME] Set to %d ✅\n", vol);
        } else {
            Serial.println("[VOLUME] ❌ Write failed");
        }
        return;
    }

    if (cmd.startsWith("setbass ")) {
        int val = cmd.substring(8).toInt();
        val = constrain(val, 0, 20);
        if (localRegisters_.writeValueFromString(REG_AUDIO_BASS, String(val))) {
            Serial.printf("[BASS] Set to %d ✅\n", val);
        } else {
            Serial.println("[BASS] ❌ Write failed");
        }
        return;
    }


    if (cmd.startsWith("setmode ")) {
        int val = cmd.substring(8).toInt();
        val = constrain(val, 0, 1);
        if (localRegisters_.writeValueFromString(REG_AUDIO_MODE, String(val))) {
            Serial.printf("[MODE] Set to %d (%s) ✅\n", val, val == 0 ? "Bluetooth" : "Radio");
        } else {
            Serial.println("[MODE] ❌ Write failed");
        }
        return;
    }

    if (cmd.startsWith("setsleep ")) {
        int val = cmd.substring(9).toInt();
        val = constrain(val, 0, 65535);
        if (localRegisters_.writeValueFromString(REG_AUDIO_SLEEP_TIMER, String(val))) {
            Serial.printf("[SLEEP TIMER] Set to %d minutes ✅\n", val);
        } else {
            Serial.println("[SLEEP TIMER] ❌ Write failed");
        }
        return;
    }

    if (cmd.startsWith("setstation ")) {
        int val = cmd.substring(11).toInt();
        val = constrain(val, 0, 65535);
        if (localRegisters_.writeValueFromString(REG_AUDIO_STATION, String(val))) {
            Serial.printf("[STATION] Set to %d ✅\n", val);
        } else {
            Serial.println("[STATION] ❌ Write failed");
        }
        return;
    }

    if (cmd.startsWith("settitle ")) {
        String title = cmd.substring(9);
        audioTitle_ = title;
        Serial.printf("[TITLE] Set to: %s ✅\n", title.c_str());
        return;
    }

    if (cmd.startsWith("setartist ")) {
        String artist = cmd.substring(10);
        audioArtist_ = artist;
        Serial.printf("[ARTIST] Set to: %s ✅\n", artist.c_str());
        return;
    }

    // ---- TEST COMMANDS ----

    if (cmd == "testall") {
        testAllAudioRegisters();
        return;
    }

    if (cmd == "help" || cmd == "?") {
        printHelp();
        return;
    }

    Serial.printf("[CMD] Unknown: '%s' (type 'help' for list)\n", cmd.c_str());
}

void AppController::testAllAudioRegisters()
{
    Serial.println("\n========== TESTING AUDIO REGISTERS ==========");

    struct TestEntry {
        uint16_t addr;
        const char* name;
    };

    TestEntry entries[] = {
        {REG_AUDIO_MODE, "Mode"},
        {REG_AUDIO_CONTROL, "Control"},
        {REG_AUDIO_SLEEP_TIMER, "Sleep Timer"},
        {REG_AUDIO_STATION, "Station"},
        {REG_AUDIO_VOLUME, "Volume"},
        {REG_AUDIO_BASS, "Bass"},
        {REG_AUDIO_TITLE, "Title"},
        {REG_AUDIO_ARTIST, "Artist"}
    };

    for (auto& entry : entries) {
        JsonDocument doc;
        if (localRegisters_.readValueToJson(entry.addr, doc)) {
            Serial.printf("[TEST] %s (0x%04X) -> %s\n",
                          entry.name, entry.addr,
                          doc["value"].as<String>().c_str());
        } else {
            Serial.printf("[TEST] %s (0x%04X) -> ❌ READ FAILED\n",
                          entry.name, entry.addr);
        }
    }

    Serial.println("=============================================\n");
}

void AppController::printHelp()
{
    Serial.println("\n========== AUDIO REGISTER COMMANDS ==========");
    Serial.println("");
    Serial.println("📖 READ:");
    Serial.println("  getvol       - Get Volume");
    Serial.println("  getbass      - Get Bass");
    Serial.println("  gettreble    - Get Treble");
    Serial.println("  geteq        - Get EQ");
    Serial.println("  getmode      - Get Mode");
    Serial.println("  gettitle     - Get Title");
    Serial.println("  getartist    - Get Artist");
    Serial.println("  getsleep     - Get Sleep Timer");
    Serial.println("  getstation   - Get Station");
    Serial.println("");
    Serial.println("✍️ WRITE:");
    Serial.println("  setvol 0-100       - Set Volume");
    Serial.println("  setbass 0-20       - Set Bass");
    Serial.println("  settreble 0-20     - Set Treble");
    Serial.println("  seteq 0-4          - Set EQ (0=Off,1=Rock,2=Jazz,3=Pop,4=Classical)");
    Serial.println("  setmode 0-1        - Set Mode (0=Bluetooth, 1=Radio)");
    Serial.println("  settitle <text>    - Set Title");
    Serial.println("  setartist <text>   - Set Artist");
    Serial.println("  setsleep 0-65535   - Set Sleep Timer (minutes)");
    Serial.println("  setstation 0-65535 - Set Station");
    Serial.println("");
    Serial.println("🧪 TEST:");
    Serial.println("  testall    - Read all registers");
    Serial.println("  help, ?    - Show this help");
    Serial.println("=============================================\n");
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

        if (localRegisters_.isLocal(regAddr)) {
            handledLocally = localRegisters_.writeValueFromString(regAddr, regVal);
            Serial.println(handledLocally
                ? "[CMD] ✅ Handled locally (register map)"
                : "[CMD] ❌ Local register write failed");
        } else if (regAddr == REG_CURTAIN_STATE) {
            int state = regVal.toInt();
            ledState = (state != 0);
            Serial.printf("[CURTAIN] State -> %s\n", ledState ? "OPEN" : "CLOSE");
            handledLocally = true;
        }

        if (!handledLocally) {
            Serial.println("[CMD] ℹ️ Register not part of local map - forwarding to mYBUS only");
        }

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