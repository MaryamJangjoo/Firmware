#include "AppController.h"
#include <esp_system.h>
#include "ecosmart_registeries.h"
#include "audio.hpp"
#include "crypto.hpp"
#include <WiFi.h>
#include "mybus_value_codec.h"
#include "mybus_protocol_constants.h"

#ifndef WIFI_STA
#define WIFI_STA 1
#endif

AppController* AppController::s_instance = nullptr;


namespace {

Registery_t* findOutputRegistryEntry(uint16_t regAddr)
{
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.state[i].address == regAddr) {
            return &reg_module_output.state[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_permanent[i].address == regAddr) {
            return &reg_module_output.timer_permanent[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_sleep[i].address == regAddr) {
            return &reg_module_output.timer_sleep[i];
        }
    }
    return nullptr;
}


Registery_t* findInputRegistryEntry(uint16_t regAddr)
{
    for (size_t i = 0; i < INPUTS_NUMBER; i++) {
        if (reg_module_input.state[i].address == regAddr) {
            return &reg_module_input.state[i];
        }
    }
    return nullptr;
}

} // namespace


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
    Serial.println("✅ ESP32 Ready! (Binary WS mode)");
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
    cloudManager->setApiBaseUrl("http://192.168.88.184:3000");

    // ✅ callback جدید: باینری (بدون JsonDocument)
    cloudManager->onBinaryFrame([this](
        const MyBusHeader& hdr,
        const std::vector<uint8_t>& payload
    ) {
        onBinaryFrameReceived(hdr, payload);
    });

    cloudManager->onLocalRegistryRead([this](uint16_t regAddr, RegisterRawValue& outValue) {
        RawRegisterValue rv;

        if (!readAudioRegistry(regAddr, rv) && !readLocalRegistry(regAddr, rv)) {
            return false;
        }

        outValue.stringValue = rv.stringValue;
        outValue.byteLen = rv.byteLen;
        memcpy(outValue.bytes, rv.bytes, sizeof(outValue.bytes));

        if (rv.isString) {
            outValue.type = RegRawType::STRING;
        } else {
            switch (rv.datatype) {
                case reg_datatype_bit:    outValue.type = RegRawType::BIT;    break;
                case reg_datatype_uint8:  outValue.type = RegRawType::UINT8;  break;
                case reg_datatype_uint16: outValue.type = RegRawType::UINT16; break;
                case reg_datatype_uint32: outValue.type = RegRawType::UINT32; break;
                case reg_datatype_int8:   outValue.type = RegRawType::INT8;   break;
                case reg_datatype_int16:  outValue.type = RegRawType::INT16;  break;
                case reg_datatype_int32:  outValue.type = RegRawType::INT32;  break;
                case reg_datatype_float:  outValue.type = RegRawType::FLOAT;  break;
                default:                  outValue.type = RegRawType::UINT8;  break;
            }
        }

        return true;
    });

    cloudManager->onShouldSkipMybusWrite([this](uint16_t regAddr) {
        return findAudioRegistryEntry(regAddr) != nullptr
            || findOutputRegistryEntry(regAddr) != nullptr;
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


// ============================================================
// Local registry write (string-based, internal use)
// ============================================================

bool AppController::writeLocalRegistry(uint16_t regAddr, const String& regVal)
{
    Serial.printf("[REG] 🔍 writeLocalRegistry called: 0x%04X = '%s'\n",
                  regAddr, regVal.c_str());

    Registery_t* entry = findOutputRegistryEntry(regAddr);

    if (entry == nullptr || entry->ref == nullptr) {
        Serial.printf("[REG] ❌ Entry not found for 0x%04X\n", regAddr);
        return false;
    }

    bool isState = false;
    bool isTimer = false;
    size_t index = 0;

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.state[i] == entry) {
            isState = true;
            index = i;
            break;
        }
        if (&reg_module_output.timer_permanent[i] == entry) {
            isTimer = true;
            index = i;
            break;
        }
        if (&reg_module_output.timer_sleep[i] == entry) {
            isTimer = true;
            index = i;
            break;
        }
    }

    if (!isState && !isTimer) {
        Serial.printf("[REG] ⏭️ Not an output/timer register\n");
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

    memcpy(entry->ref, buf, len);
    Serial.printf("[REG] ✅ 0x%04X written\n", regAddr);

    if (isState) {
        applyOutputsToHardware();
    } else {
        Serial.printf("[REG] 📊 timer[%zu] stored (metadata only)\n", index);
    }

    return true;
}


// ============================================================
// Audio visualization
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
// Audio registry
// ============================================================

Registery_t* AppController::findAudioRegistryEntry(uint16_t regAddr)
{
    Registery_t* candidates[] = {
        &reg_module_audio.mode,
        &reg_module_audio.control,
        &reg_module_audio.sleep_timer,
        &reg_module_audio.station,
        &reg_module_audio.title,
        &reg_module_audio.artist,
        &reg_module_audio.volume,
        &reg_module_audio.bass,
        &reg_module_audio.treble,
        &reg_module_audio.eq
    };

    for (auto* entry : candidates) {
        if (entry->address == regAddr) {
            return entry;
        }
    }
    return nullptr;
}


bool AppController::readAudioRegistry(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findAudioRegistryEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = entry->isString;

    if (entry->isString) {
        outValue.stringValue = *static_cast<String*>(entry->ref);
        outValue.byteLen = 0;
        return true;
    }

    switch (entry->datatype) {
        case reg_datatype_uint8:
            outValue.bytes[0] = *static_cast<uint8_t*>(entry->ref);
            outValue.byteLen = sizeof(uint8_t);
            break;
        case reg_datatype_uint16: {
            uint16_t v = *static_cast<uint16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        default:
            return false;
    }

    return true;
}


bool AppController::writeAudioRegistry(uint16_t regAddr, const String& regVal)
{
    Registery_t* entry = findAudioRegistryEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    if (!entry->writable) {
        Serial.printf("[AUDIO] ❌ 0x%04X read-only\n", regAddr);
        return false;
    }

    if (entry->isString) {
        *static_cast<String*>(entry->ref) = regVal;
    } else {
        uint8_t buf[8];
        size_t len = 0;

        if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                                   buf, sizeof(buf), len)) {
            Serial.printf("[AUDIO] ❌ Parse failed: '%s'\n", regVal.c_str());
            return false;
        }

        if (len != entry->size) {
            Serial.printf("[AUDIO] ❌ Size mismatch at 0x%04X\n", regAddr);
            return false;
        }

        memcpy(entry->ref, buf, len);
    }

    if (regAddr == REG_ADD_AUDIO_VOLUME) {
        uint8_t vol = audio_object.volume;
        if (vol > 124) vol = 124;
        esp_err_t ret = tas5805m_set_volume_pct(vol);
        Serial.printf("[AUDIO] Volume -> %u%% (%s)\n", vol, ret == ESP_OK ? "OK" : "FAIL");

    } else if (regAddr == REG_ADD_AUDIO_CONTROL) {
        switch (audio_object.control) {
            case 1:
                bta.reconnect();
                Serial.println("[AUDIO] ▶️ Play / Reconnect");
                break;
            case 0:
            case 2:
                Serial.printf("[AUDIO] ⏸️ Command %u\n", audio_object.control);
                break;
            default:
                Serial.printf("[AUDIO] ⚠️ Unknown control: %u\n", audio_object.control);
                break;
        }

    } else if (regAddr == REG_ADD_AUDIO_BASS) {
        Serial.printf("[AUDIO] Bass -> %u (not wired)\n", audio_object.bass);
    } else if (regAddr == REG_ADD_AUDIO_TREBLE) {
        Serial.printf("[AUDIO] Treble -> %u (not wired)\n", audio_object.treble);
    } else if (regAddr == REG_ADD_AUDIO_EQ) {
        Serial.printf("[AUDIO] EQ -> %u (not wired)\n", audio_object.eq);
    } else if (regAddr == REG_ADD_AUDIO_MODE) {
        Serial.printf("[AUDIO] Mode -> %u (not wired)\n", audio_object.mode);
    } else if (regAddr == REG_ADD_AUDIO_STATION) {
        Serial.printf("[AUDIO] Station -> %u (not wired)\n", audio_object.station);
    } else if (regAddr == REG_ADD_AUDIO_SLEEP_TIMER) {
        Serial.printf("[AUDIO] Sleep timer -> %u min (not wired)\n", audio_object.sleep_timer);
    }

    return true;
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


bool AppController::readLocalRegistry(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findOutputRegistryEntry(regAddr);

    if (entry == nullptr) {
        entry = findInputRegistryEntry(regAddr);
    }

    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = false;

    switch (entry->datatype) {
        case reg_datatype_bit:
            outValue.bytes[0] = (*static_cast<bool*>(entry->ref)) ? 1 : 0;
            outValue.byteLen = 1;
            break;
        case reg_datatype_uint8:
            outValue.bytes[0] = *static_cast<uint8_t*>(entry->ref);
            outValue.byteLen = sizeof(uint8_t);
            break;
        case reg_datatype_uint16: {
            uint16_t v = *static_cast<uint16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_uint32: {
            uint32_t v = *static_cast<uint32_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int8: {
            int8_t v = *static_cast<int8_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int16: {
            int16_t v = *static_cast<int16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int32: {
            int32_t v = *static_cast<int32_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_float: {
            float v = *static_cast<float*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        default:
            return false;
    }

    return true;
}


String AppController::getRegistryValue(uint16_t regAddr)
{
    RawRegisterValue rv;

    if (!readLocalRegistry(regAddr, rv)) {
        return "";
    }

    if (rv.isString) {
        return rv.stringValue;
    }

    switch (rv.datatype) {
        case reg_datatype_bit:
            return String(rv.bytes[0] != 0 ? 1 : 0);
        case reg_datatype_uint8:
            return String(rv.bytes[0]);
        case reg_datatype_uint16: {
            uint16_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_uint32: {
            uint32_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int8: {
            int8_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int16: {
            int16_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int32: {
            int32_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_float: {
            float v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v, 6);
        }
        default:
            return "";
    }
}


// ============================================================
// ✅ rawPayloadToRegValString — تبدیل بایت خام به String
// (فقط برای رجیسترهای لوکال که API قدیمی String می‌گیرند)
// ============================================================

String AppController::rawPayloadToRegValString(
    uint16_t regAddr,
    const uint8_t* data,
    size_t len)
{
    // نوع داده را از خود آدرس استخراج کن
    const MyBusDataType dataType =
        static_cast<MyBusDataType>((regAddr >> 8) & 0x0F);

    if (data == nullptr || len == 0) {
        return "";
    }

    switch (dataType) {
        case DT_BIT:
            return String(data[0] != 0 ? "1" : "0");

        case DT_UINT8:
            return String(data[0]);

        case DT_UINT16: {
            uint16_t v;
            memcpy(&v, data, sizeof(v));
            return String(v);
        }

        case DT_UINT32: {
            uint32_t v;
            memcpy(&v, data, sizeof(v));
            return String(v);
        }

        case DT_INT8: {
            int8_t v;
            memcpy(&v, data, sizeof(v));
            return String(v);
        }

        case DT_INT16: {
            int16_t v;
            memcpy(&v, data, sizeof(v));
            return String(v);
        }

        case DT_INT32: {
            int32_t v;
            memcpy(&v, data, sizeof(v));
            return String(v);
        }

        case DT_FLOAT: {
            float v;
            memcpy(&v, data, sizeof(v));
            return String(v, 6);
        }

        case DT_STRING:
        case DT_JSON:
        case DT_STRUCT:
        default: {
            String s;
            s.reserve(len + 1);
            for (size_t i = 0; i < len; ++i) {
                s += static_cast<char>(data[i]);
            }
            return s;
        }
    }
}


// ============================================================
// ✅ onBinaryFrameReceived — پردازش فریم mYBUS باینری از WS
//
// ⚠️ این تابع از طریق callback از CloudWebSocketServer صدا زده
// می‌شود. کانال WS کاملاً باینری است - هیچ JsonDocument اینجا
// وجود ندارد.
//
// برای رجیسترهای لوکال، بایت خام را به String تبدیل می‌کنیم
// (چون writeAudioRegistry/writeLocalRegistry هنوز String می‌گیرند)
// و از همان مسیر encodeRegValueString استفاده می‌کنیم.
// ============================================================

void AppController::onBinaryFrameReceived(
    const MyBusHeader& hdr,
    const std::vector<uint8_t>& payload)
{
    Serial.printf("[BIN] cmd=%u payloadLen=%u\n",
                  hdr.command, static_cast<unsigned>(payload.size()));

    switch (hdr.command) {

        case mybus_proto::COMMAND_WRITE_REGISTRY: {
            // payload: [AddrLow][AddrHigh][RawValueBytes...]
            if (payload.size() < 3) {
                Serial.println("[BIN] ❌ WRITE payload too short");
                return;
            }

            const uint16_t regAddr =
                static_cast<uint16_t>(payload[0] | (payload[1] << 8));
            const uint8_t* value = payload.data() + 2;
            const size_t valueLen = payload.size() - 2;

            const String regVal = rawPayloadToRegValString(regAddr, value, valueLen);

            Serial.printf("[BIN] WRITE 0x%04X = '%s'\n",
                          regAddr, regVal.c_str());

            // audio / curtain / local
            bool handledLocally =
                writeAudioRegistry(regAddr, regVal) ||
                handleCurtainRegistryWrite(regAddr, regVal) ||
                writeLocalRegistry(regAddr, regVal);

            if (handledLocally) {
                Serial.println("[BIN] ✅ Handled locally");
            } else {
                Serial.println("[BIN] ℹ️ Not a local register - ignored");
            }
            break;
        }

        case mybus_proto::COMMAND_READ_REGISTRY:
            // پاسخ READ مستقیماً توسط CloudWebSocketServer فرستاده شد
            Serial.printf("[BIN] READ_REGISTRY 0x%04X (handled by WS server)\n",
                          payload.size() >= 2
                              ? (payload[0] | (payload[1] << 8))
                              : 0);
            break;

        case mybus_proto::COMMAND_WS_STATUS:
        case mybus_proto::COMMAND_WS_USERS_LIST:
        case mybus_proto::COMMAND_WS_SITE_INFO:
        case mybus_proto::COMMAND_WS_WELCOME:
        case mybus_proto::COMMAND_WS_ERROR:
            // این‌ها مستقیماً توسط CloudWebSocketServer پردازش می‌شوند
            break;

        default:
            Serial.printf("[BIN] ⚠️ Unhandled cmd: %u\n", hdr.command);
            break;
    }
}