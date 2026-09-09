#include "AppController.h"
#include <esp_system.h>
#include "ecosmart_registeries.h"
#include "crypto.hpp"
#include <WiFi.h>
#include "mybus_value_codec.h"

#ifndef WIFI_STA
#define WIFI_STA 1
#endif

AppController* AppController::s_instance = nullptr;


namespace {

Registery_t* findOutputRegistryEntry(uint16_t regAddr)
{
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.state[i].address == regAddr) {
            Serial.printf("[REG] 🔍 Found state[%zu] at 0x%04X\n", i, regAddr);
            return &reg_module_output.state[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_permanent[i].address == regAddr) {
            Serial.printf("[REG] 🔍 Found timer_permanent[%zu] at 0x%04X\n", i, regAddr);
            return &reg_module_output.timer_permanent[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_sleep[i].address == regAddr) {
            Serial.printf("[REG] 🔍 Found timer_sleep[%zu] at 0x%04X\n", i, regAddr);
            return &reg_module_output.timer_sleep[i];
        }
    }
    Serial.printf("[REG] ❌ No entry found for 0x%04X\n", regAddr);
    return nullptr;
}

} 

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

    ecosmart_registery_init();

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

    cloudManager->onLocalRegistryRead([this](uint16_t regAddr, JsonDocument& outValue) {
        return readLocalRegistry(regAddr, outValue);
    });

    cloudManager->onShouldSkipMybusWrite([](uint16_t regAddr) {
        return findOutputRegistryEntry(regAddr) != nullptr;
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
        
        for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
            outputs_object[i].value = ledState;
        }
        applyOutputsToHardware();
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

void AppController::applyOutputsToHardware()
{
    Serial.println("!!! 🔄 applyOutputsToHardware CALLED !!!");
    
    uint8_t byteLow = 0;
    uint8_t byteHigh = 0;

    Serial.println("[OUTPUTS] Current states:");
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        Serial.printf("  [%zu] = %d (addr=0x%04X)\n", 
                      i, outputs_object[i].value, 
                      reg_module_output.state[i].address);
        if (outputs_object[i].value) {
            if (i < 8) {
                byteLow |= (1U << i);
                Serial.printf("    → Setting bit %zu in byteLow\n", i);
            } else {
                byteHigh |= (1U << (i - 8));
                Serial.printf("    → Setting bit %zu in byteHigh\n", i - 8);
            }
        }
    }

    Serial.printf("[OUTPUTS] Sending: byteHigh=0x%02X, byteLow=0x%02X\n", byteHigh, byteLow);

    digitalWrite(PIN_SR_LATCH, LOW);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, byteLow);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, byteHigh);

    digitalWrite(PIN_SR_LATCH, HIGH);

    Serial.println("[OUTPUTS] ✅ Shift register updated");
}


bool AppController::writeLocalRegistry(uint16_t regAddr, const String& regVal)
{
    Serial.printf("[REG] 🔍 writeLocalRegistry called: 0x%04X = '%s'\n", regAddr, regVal.c_str());
    
    Registery_t* entry = findOutputRegistryEntry(regAddr);

    if (entry == nullptr || entry->ref == nullptr) {
        Serial.printf("[REG] ❌ Entry not found for 0x%04X\n", regAddr);
        return false;
    }

    Serial.printf("[REG] 🔍 Found entry: ref=%p, size=%u, datatype=%d\n", 
                  entry->ref, entry->size, entry->datatype);
    
    
    size_t index = 0;
    bool isState = false;
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.state[i] == entry) {
            isState = true;
            index = i;
            Serial.printf("    ➡️ This is state[%zu] (address 0x%04X), current value = %d\n", 
                          i, reg_module_output.state[i].address, outputs_object[i].value);
            break;
        }
    }

    if (!isState) {
        Serial.printf("[REG] ⏭️ Not a state register, skipping\n");
        return false;
    }

    if (entry->datatype > reg_datatype_float) {
        Serial.printf("[REG] ❌ Unsupported datatype\n");
        return false;
    }

    uint8_t buf[8];
    size_t len = 0;

    if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                               buf, sizeof(buf), len)) {
        Serial.printf("[REG] ❌ Failed to parse '%s'\n", regVal.c_str());
        return false;
    }

    if (len != entry->size) {
        Serial.printf("[REG] ❌ Size mismatch\n");
        return false;
    }

 
    Serial.printf("[REG] 📊 state[%zu] old value = %d\n", index, outputs_object[index].value);

    memcpy(entry->ref, buf, len);

 
    Serial.printf("[REG] 📊 state[%zu] new value = %d\n", index, outputs_object[index].value);

    Serial.printf("[REG] ✅ 0x%04X written\n", regAddr);

    Serial.println("[REG] 🔄 CALLING applyOutputsToHardware()");
    applyOutputsToHardware();

    return true;
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


bool AppController::handleAudioRegistryWrite(uint16_t regAddr, const String& regVal)
{
    if (regAddr == REG_ADD_AUDIO_VOLUME) {
        int vol = regVal.toInt();
        vol = constrain(vol, 0, 100);
        esp_err_t ret = tas5805m_set_volume_pct((uint8_t)vol);
        Serial.printf("[AUDIO] Volume -> %d%% (%s)\n", vol, ret == ESP_OK ? "OK" : "FAIL");
        return ret == ESP_OK;
    }

    if (regAddr == REG_ADD_AUDIO_CONTROL) {
        int cmd = regVal.toInt();
        Serial.printf("[AUDIO] Control command: %d\n", cmd);
        switch (cmd) {
            case 1:  // Play / Reconnect
                bta.reconnect();
                Serial.println("[AUDIO] ▶️ Play / Reconnect");
                break;
            case 0:  // Pause
            case 2:  // Stop
                Serial.printf("[AUDIO] ⏸️ Command %d received (not fully implemented)\n", cmd);
                break;
            default:
                return false;
        }
        return true;
    }

    if (regAddr == REG_ADD_AUDIO_BASS) {
        int bass = regVal.toInt();
        bass = constrain(bass, 0, 100);
        Serial.printf("[AUDIO] Bass -> %d%% (not wired)\n", bass);
        return true;
    }

    if (regAddr == REG_ADD_AUDIO_MODE) {
        int mode = regVal.toInt();
        Serial.printf("[AUDIO] Audio mode -> %d (not wired)\n", mode);
        return true;
    }

    if (regAddr == REG_ADD_AUDIO_STATION) {
        int station = regVal.toInt();
        Serial.printf("[AUDIO] Station -> %d (not wired)\n", station);
        return true;
    }

    if (regAddr == REG_ADD_AUDIO_SLEEP_TIMER) {
        int timer = regVal.toInt();
        Serial.printf("[AUDIO] Sleep timer -> %d min (not wired)\n", timer);
        return true;
    }

    return false;
}


bool AppController::handleCurtainRegistryWrite(uint16_t regAddr, const String& regVal)
{
    if (regAddr == REG_ADD_CURTAIN_STATE) {
        int state = regVal.toInt();
        ledState = (state != 0);
        Serial.printf("[CURTAIN] State -> %s\n", ledState ? "OPEN" : "CLOSE");
        return true;
    }

    return false;
}


bool AppController::readLocalRegistry(uint16_t regAddr, JsonDocument& outValue)
{
    Registery_t* entry = findOutputRegistryEntry(regAddr);

    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    switch (entry->datatype) {
        case reg_datatype_bit:
            outValue["value"] = *static_cast<bool*>(entry->ref);
            break;

        case reg_datatype_uint8:
            outValue["value"] = *static_cast<uint8_t*>(entry->ref);
            break;

        case reg_datatype_uint16:
            outValue["value"] = *static_cast<uint16_t*>(entry->ref);
            break;

        case reg_datatype_uint32:
            outValue["value"] = *static_cast<uint32_t*>(entry->ref);
            break;

        case reg_datatype_int8:
            outValue["value"] = *static_cast<int8_t*>(entry->ref);
            break;

        case reg_datatype_int16:
            outValue["value"] = *static_cast<int16_t*>(entry->ref);
            break;

        case reg_datatype_int32:
            outValue["value"] = *static_cast<int32_t*>(entry->ref);
            break;

        case reg_datatype_float:
            outValue["value"] = *static_cast<float*>(entry->ref);
            break;

        default:
            return false;
    }

    outValue["regAddr"] = regAddr;
    return true;
}


String AppController::getRegistryValue(uint16_t regAddr)
{
    JsonDocument doc;

    if (!readLocalRegistry(regAddr, doc)) {
        return "";
    }

    return doc["value"].as<String>();
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

        JsonDocument localValue;
        if (readLocalRegistry(regAddr, localValue)) {
            Serial.println("[CMD] ✅ Local registry read:");
            serializeJson(localValue, Serial);
            Serial.println();

            if (cloudManager != nullptr && cloudManager->isWebSocketConnected()) {
                JsonDocument wsMsg;
                wsMsg["type"] = "registry_response";
                wsMsg["RegAdd"] = regAddr;
                if (!localValue["value"].isNull()) {
                    wsMsg["value"] = localValue["value"];
                }
                cloudManager->sendRealtimeData(wsMsg);
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

        Registery_t* testEntry = findOutputRegistryEntry(regAddr);
        if (testEntry != nullptr) {
            Serial.printf("[CMD] 🔍 Found entry for 0x%04X in registry\n", regAddr);
        } else {
            Serial.printf("[CMD] 🔍 No entry found for 0x%04X in registry\n", regAddr);
        }

        bool handledLocally = handleAudioRegistryWrite(regAddr, regVal)
                            || handleCurtainRegistryWrite(regAddr, regVal)
                            || writeLocalRegistry(regAddr, regVal);

        if (handledLocally) {
            Serial.println("[CMD] ✅ Handled locally");
        } else {
            Serial.println("[CMD] ℹ️ Register not part of Phase-1 map - ignored locally");
        }

        const bool isLocalOutputRegister = (findOutputRegistryEntry(regAddr) != nullptr);

        if (!isLocalOutputRegister &&
            cloudManager != nullptr &&
            cloudManager->isSecureSessionEstablished()) {

            JsonDocument req;
            req["RegAdd"] = regAddr;
            req["RegVal"] = regVal;
            cloudManager->sendMybusData(req);
        }
    }
}