#include "AppController.h"
#include <esp_system.h>

#include "ecosmart_registries.h"
#include "crypto.hpp"

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

// ============================================================
// begin
// ============================================================

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
    digitalWrite(PIN_I2S_PDN, LOW);
    Serial.println("PDN pin set LOW (TAS5805M active)");

    if (!connectToWiFi()) {
        Serial.println("[ERROR] WiFi connection failed. Retrying in 5 seconds...");
        delay(5000);
        ESP.restart();
        return;
    }

    initCloudManager();
    configureMybusAddress();

    // ---- Login ----
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

    // ---- mYBUS Handshake ----
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

// ============================================================
// handle
// ============================================================

void AppController::handle()
{
    if (cloudManager != nullptr) {
        cloudManager->loopWebSocketServer();
    }

    handleWiFiReconnect();
    handleLedState();

    delay(100);
}

// ============================================================
// setup helpers
// ============================================================

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
    cloudManager->setApiBaseUrl("http://192.168.88.174:3000");
    cloudManager->onCommand([this](const JsonDocument& cmd) {
        onCommandReceived(cmd);
    });
    Serial.println("[CLOUD] CloudManager initialized successfully");
}

// ✅ رفع باگ: mybusDeviceId_/mybusZoneId_ در CloudManager هیچ‌جا مقداردهی
// نمی‌شدند (مگر از NVS بازیابی شده باشند)، پس performHandshake() همیشه
// با "Invalid Device ID/Zone" شکست می‌خورد. این تابع مقدار پیش‌فرض را
// -فقط در صورتی که هنوز پیکربندی نشده باشند- تنظیم می‌کند.
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
        uint8_t volume = 60;
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

// ============================================================
// loop helpers
// ============================================================

void AppController::handleWiFiReconnect()
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection lost. Reconnecting...");
        WiFi.reconnect();
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[WiFi] ✅ Reconnected!");
        } else {
            Serial.println("[WiFi] ❌ Reconnect failed!");
        }
    }
}

void AppController::handleLedState()
{
    if (lastLedState != ledState) {
        lastLedState = ledState;
        ledState ? setCurtainOn() : setCurtainOff();
    }
}

// ============================================================
// Curtain (shift register)
// ============================================================

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

// ============================================================
// Audio visualization + BT sink
// ============================================================

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
// Audio registry write
// ============================================================

bool AppController::handleAudioRegistryWrite(uint16_t regAddr, const String& regVal)
{
    if (regAddr == REG_AUDIO_VOLUME) {
        int vol = regVal.toInt();
        vol = constrain(vol, 0, 124);
        esp_err_t ret = tas5805m_set_volume_pct((uint8_t)vol);
        Serial.printf("[AUDIO] Volume -> %d (%s)\n", vol, ret == ESP_OK ? "OK" : "FAIL");
        return ret == ESP_OK;
    }

    if (regAddr == REG_AUDIO_CONTROL) {
        int cmd = regVal.toInt();
        Serial.printf("[AUDIO] Control command: %d\n", cmd);
        switch (cmd) {
            case 1:
                bta.reconnect();
                break;
            case 0:
            case 2:
                Serial.println("[AUDIO] ⚠️ Stop/Pause not wired yet - verify btAudio API");
                break;
            default:
                return false;
        }
        return true;
    }

    if (regAddr == REG_AUDIO_BASS || regAddr == REG_AUDIO_TREBLE || regAddr == REG_AUDIO_EQ) {
        Serial.printf("[AUDIO] ⚠️ Bass/Treble/EQ register 0x%04X received but not wired yet\n", regAddr);
        return true;
    }

    return false;
}

// ============================================================
// Curtain registry write
//
// ✅ رفع باگ: قبلاً ledState هیچ‌جا از روی رجیستر ست نمی‌شد، پس
// نوشتن روی REG_CURTAIN_STATE هیچ اثری روی رله‌های شیفت‌رجیستر
// نداشت. الان با نوشتن روی این رجیستر، ledState به‌روزرسانی می‌شود
// و handleLedState() در حلقه‌ی اصلی آن را به رله‌ها اعمال می‌کند.
// ============================================================

bool AppController::handleCurtainRegistryWrite(uint16_t regAddr, const String& regVal)
{
    if (regAddr == REG_CURTAIN_STATE) {
        int state = regVal.toInt();
        ledState = (state != 0);
        Serial.printf("[CURTAIN] State -> %s\n", ledState ? "OPEN" : "CLOSE");
        return true;
    }

    return false;
}

// ============================================================
// Command callback از CloudManager
// ============================================================

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

        if (cloudManager != nullptr && cloudManager->isSecureSessionEstablished()) {
            JsonDocument req;
            req["RegAdd"] = regAddr;
            req["RegVal"] = "";

            JsonDocument response;
            if (cloudManager->sendMybusData(req, &response)) {
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

        bool handledLocally = handleAudioRegistryWrite(regAddr, regVal)
                            || handleCurtainRegistryWrite(regAddr, regVal);
        if (handledLocally) {
            Serial.println("[CMD] ✅ Handled locally");
        } else {
            Serial.println("[CMD] ℹ️ Register not part of Phase-1 map - ignored locally");
        }

        if (cloudManager != nullptr && cloudManager->isSecureSessionEstablished()) {
            JsonDocument req;
            req["RegAdd"] = regAddr;
            req["RegVal"] = regVal;
            cloudManager->sendMybusData(req);
        }
    }
}