#include "AppController.h"
#include <esp_system.h>
#include "crypto.hpp"
#include <WiFi.h>
#include "mybus_protocol_constants.h"
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include "Logging.h"

#ifndef WIFI_STA
#define WIFI_STA 1
#endif

static const char* TAG        = "APP";
static const char* TAG_WIFI   = "WIFI";
static const char* TAG_MDNS   = "MDNS";
static const char* TAG_CLOUD  = "CLOUD";
static const char* TAG_AUTH   = "AUTH";
static const char* TAG_MYBUS  = "MYBUS";
static const char* TAG_WS     = "WS";
static const char* TAG_BIN    = "BIN";
static const char* TAG_AUDIO  = "AUDIO";

AppController* AppController::s_instance = nullptr;

AppController::AppController()
    : amp(&Wire),
      bta("mYSpeaker"),
      outputsController_(PIN_SR_LATCH, PIN_SR_CLOCK, PIN_SR_DATA),
      audioController_(amp, bta),
      rgbController_(leds, NUM_LEDS),
      curtainController_(outputsController_),
      cloudController_(cloudStore_)
{
    // The inputs controller needs a late binding to the outputs
    // controller so that the master channel can drive every
    // output when it transitions.
    inputsController_.attachOutputs(&outputsController_);

    registryControllers_ = {
        &inputsController_,
        &audioController_,
        &rgbController_,
        &curtainController_,
        &outputsController_,
        &cloudController_,
    };

    s_instance = this;
}

void AppController::begin()
{
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    outputsController_.begin();

    Serial.begin(115200);
    delay(200);
    ECOSMART_LOGI(TAG, "System Starting ....");

    pinMode(PIN_I2S_PDN, OUTPUT);
    digitalWrite(PIN_I2S_PDN, HIGH);
    delay(10);
    ECOSMART_LOGI(TAG, "PDN pin set HIGH (TAS5805M active)");

    ecosmart_registery_init();

    // Configure the digital inputs after the registry has been
    // initialized, so that the initial GPIO read uses the
    // correct pull mode for each channel.
    inputsController_.begin();

    cloudStore_.begin();

    {
        String defaultHost = String(API_BASE_URL);
        const int schemeEnd = defaultHost.indexOf("://");
        if (schemeEnd >= 0) {
            defaultHost = defaultHost.substring(schemeEnd + 3);
        }
        const int portEnd = defaultHost.indexOf(':');
        if (portEnd >= 0) {
            defaultHost = defaultHost.substring(0, portEnd);
        }

        cloudStore_.seedDefaults(
            defaultHost,
            String(OWNER_USERNAME),
            String(OWNER_PASSWORD));
    }

    cloud_object.server_fqdn = cloudStore_.getServerFqdn();
    cloud_object.server_ip   = cloudStore_.getServerIp();
    cloud_object.server_port = cloudStore_.getServerPort();
    cloud_object.username    = cloudStore_.getUsername();
    cloud_object.password    = "****";
    cloud_object.device_id   = cloudStore_.getDeviceId();

    if (!connectToWiFi()) {
        ECOSMART_LOGE(TAG, "WiFi connection failed. Retrying in 5 seconds...");
        delay(5000);
        ESP.restart();
        return;
    }

    if (!MDNS.begin("ecosmart")) {
        ECOSMART_LOGE(TAG_MDNS, "Failed to start");
    } else {
        ECOSMART_LOGI(TAG_MDNS, "Started: http://ecosmart.local");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "device", "EcoSmart");
        MDNS.addServiceTxt("http", "tcp", "version", "2.0.0");
    }

    initCloudManager();

    cloudController_.attachCloudManager(cloudManager);
    cloudController_.applyStoredConfig();
    cloudController_.syncDeviceIdFromCloudManager();

    configureMybusAddress();

    ECOSMART_LOGI(TAG_AUTH, "Attempting to login...");

    const bool haveStoredUsername = !cloudStore_.getUsername().isEmpty();
    const bool haveStoredPassword = !cloudStore_.getPassword().isEmpty();

    bool loginSuccess = cloudManager->loginUser(
        haveStoredUsername ? cloudStore_.getUsername()
                           : String(OWNER_USERNAME),
        haveStoredPassword ? cloudStore_.getPassword()
                           : String(OWNER_PASSWORD),
        cloudManager->getDeviceId());

    if (loginSuccess) {
        ECOSMART_LOGI(TAG_AUTH, "Login successful!");
    } else {
        ECOSMART_LOGW(TAG_AUTH, "Login failed, trying offline...");
        if (cloudManager->loginOffline(OWNER_USERNAME, OWNER_PASSWORD)) {
            ECOSMART_LOGI(TAG_AUTH, "Offline login successful!");
        } else {
            ECOSMART_LOGE(TAG_AUTH, "Offline login failed!");
        }
    }

    if (cloudManager->isLoggedIn()) {
        if (cloudManager->isSecureSessionEstablished()) {
            ECOSMART_LOGI(TAG_MYBUS,
                "Using restored session from NVS, skipping handshake");
        } else {
            ECOSMART_LOGI(TAG_MYBUS, "Starting handshake...");
            if (cloudManager->performHandshake()) {
                ECOSMART_LOGI(TAG_MYBUS, "Handshake successful!");
            } else {
                ECOSMART_LOGE(TAG_MYBUS, "Handshake failed!");
            }
        }
    }

    cloudManager->startWebSocketServer();
    ECOSMART_LOGI(TAG_WS, "WebSocket server started on /ws");

    initAudioHardware();

    ECOSMART_LOGI(TAG, "========================================");
    ECOSMART_LOGI(TAG, "ESP32 Ready! (Binary WS mode)");
    ECOSMART_LOGI(TAG, "========================================");
}

void AppController::handle()
{
    if (cloudManager != nullptr) {
        cloudManager->loopWebSocketServer();
    }

    // Scan the physical inputs on every loop iteration. The
    // controller throttles itself internally using
    // INPUT_POLL_INTERVAL_MS.
    inputsController_.poll();

    handleWiFiReconnect();
    handleLedState();
}

static const char* wifiStatusToString(wl_status_t status);

bool AppController::connectToWiFi()
{
    ECOSMART_LOGI(TAG_WIFI, "MAC: %s", WiFi.macAddress().c_str());

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    {
        wifi_country_t country = {};
        strncpy(country.cc, "AZ", sizeof(country.cc));
        country.schan = 1;
        country.nchan = 13;
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);
    }

    ECOSMART_LOGI(TAG_WIFI, "Scanning...");
    int networksFound = WiFi.scanNetworks();

    bool targetFound = false;
    bool targetIsOpen = false;

    if (networksFound == 0) {
        ECOSMART_LOGW(TAG_WIFI, "No networks found (radio issue?)");
    } else {
        ECOSMART_LOGI(TAG_WIFI, "Found %d networks", networksFound);

        for (int i = 0; i < networksFound; i++) {
            String ssid = WiFi.SSID(i);

            if (ssid != String(WIFI_SSID)) {
                continue;
            }

            targetFound = true;

            const int32_t rssi = WiFi.RSSI(i);
            const int32_t channel = WiFi.channel(i);
            const wifi_auth_mode_t enc = WiFi.encryptionType(i);

            const char* encStr;
            switch (enc) {
                case WIFI_AUTH_OPEN:            encStr = "OPEN";        break;
                case WIFI_AUTH_WEP:             encStr = "WEP";         break;
                case WIFI_AUTH_WPA_PSK:         encStr = "WPA_PSK";     break;
                case WIFI_AUTH_WPA2_PSK:        encStr = "WPA2_PSK";    break;
                case WIFI_AUTH_WPA_WPA2_PSK:    encStr = "WPA_WPA2_PSK";break;
                case WIFI_AUTH_WPA2_ENTERPRISE: encStr = "WPA2_ENT";    break;
                case WIFI_AUTH_WPA3_PSK:        encStr = "WPA3_PSK";    break;
                case WIFI_AUTH_WPA2_WPA3_PSK:   encStr = "WPA2_WPA3";   break;
                default:                        encStr = "UNKNOWN";    break;
            }

            targetIsOpen = (enc == WIFI_AUTH_OPEN);

            ECOSMART_LOGI(TAG_WIFI,
                "Target found: RSSI=%d Ch=%d Enc=%s",
                rssi, channel, encStr);

            if (enc == WIFI_AUTH_WPA3_PSK) {
                ECOSMART_LOGW(TAG_WIFI,
                    "WPA3-only - ESP32 may fail to connect.");
            }

            break;
        }

        if (!targetFound) {
            ECOSMART_LOGW(TAG_WIFI,
                "Target SSID '%s' NOT found in scan.", WIFI_SSID);
        }
    }

    WiFi.scanDelete();

    ECOSMART_LOGI(TAG_WIFI, "Connecting to %s%s",
                  WIFI_SSID, targetIsOpen ? " (OPEN)" : "");

    int attempt = 0;
    constexpr int kMaxAttempts = 3;
    bool connected = false;

    while (attempt < kMaxAttempts && !connected) {
        ++attempt;
        ECOSMART_LOGI(TAG_WIFI, "Attempt %d/%d", attempt, kMaxAttempts);

        if (targetIsOpen) {
            WiFi.begin(WIFI_SSID);
        } else {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }

        const uint32_t deadline = millis() + 8000;
        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
        }

        ECOSMART_LOGW(TAG_WIFI,
            "Attempt %d failed (status=%d)",
            attempt, static_cast<int>(WiFi.status()));
        WiFi.disconnect(true);
        delay(500);
    }

    if (connected) {
        ECOSMART_LOGI(TAG_WIFI,
            "Connected on attempt %d | IP=%s RSSI=%d dBm",
            attempt,
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI());

        WiFi.setAutoReconnect(true);
        return true;
    }

    ECOSMART_LOGE(TAG_WIFI,
        "Failed after %d attempts (status=%d, %s)",
        attempt, WiFi.status(), wifiStatusToString(WiFi.status()));

    return false;
}

static const char* wifiStatusToString(wl_status_t status)
{
    switch (status) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

void AppController::initCloudManager()
{
    ECOSMART_LOGI(TAG_CLOUD, "Initializing CloudManager...");
    cloudManager = new CloudManager();
    cloudManager->setApiBaseUrl(API_BASE_URL);

    cloudManager->onBinaryFrame([this](
        const MyBusHeader& hdr,
        const std::vector<uint8_t>& payload
    ) {
        onBinaryFrameReceived(hdr, payload);
    });

    cloudManager->onLocalRegistryRead([this](
        uint16_t regAddr,
        RegisterRawValue& outValue
    ) {
        RawRegisterValue rv;

        bool handled = false;
        for (auto* ctrl : registryControllers_) {
            if (ctrl->read(regAddr, rv)) {
                handled = true;
                break;
            }
        }

        if (!handled) {
            handled = outputsController_.readLocal(regAddr, rv);
        }

        if (!handled) {
            return false;
        }

        outValue.stringValue = rv.stringValue;
        outValue.byteLen = min(rv.byteLen, sizeof(outValue.bytes));

        memset(outValue.bytes, 0, sizeof(outValue.bytes));
        memcpy(outValue.bytes, rv.bytes, outValue.byteLen);

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
        for (auto* ctrl : registryControllers_) {
            if (ctrl->findEntry(regAddr) != nullptr) {
                return true;
            }
        }
        return outputsController_.findOutputEntry(regAddr) != nullptr;
    });

    cloudManager->onLocalRegistryWrite([this](
        uint16_t regAddr,
        const String& value
    ) {
        const bool handled =
            inputsController_.write(regAddr, value)
            || audioController_.write(regAddr, value)
            || rgbController_.write(regAddr, value)
            || curtainController_.write(regAddr, value)
            || cloudController_.write(regAddr, value)
            || outputsController_.writeOutput(regAddr, value);

        if (handled) {
            ECOSMART_LOGI(TAG, "LOCAL-WS OK 0x%04X = '%s'",
                          regAddr, value.c_str());
        } else {
            ECOSMART_LOGW(TAG, "LOCAL-WS FAIL 0x%04X = '%s'",
                          regAddr, value.c_str());
        }

        return handled;
    });

    ECOSMART_LOGI(TAG_CLOUD, "CloudManager initialized successfully");
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
        ECOSMART_LOGE("TAS5805M", "Failed to initialize TAS5805M");
    } else {
        uint8_t volume = 70;
        if (tas5805m_set_volume_pct(volume) != ESP_OK) {
            ECOSMART_LOGE("TAS5805M", "Failed to set volume");
        }
        if (tas5805m_get_volume_pct(&volume) != ESP_OK) {
            ECOSMART_LOGE("TAS5805M", "Failed to get volume");
        } else {
            ECOSMART_LOGI("TAS5805M", "Current volume: %d", volume);
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
    // ============================================================
    // DISABLED: WiFi reconnect logic is currently disabled.
    //
    // The original implementation called WiFi.reconnect() every
    // WIFI_RECONNECT_ATTEMPT_INTERVAL_MS while in RECONNECTING
    // state, but this caused the ESP32 to repeatedly disconnect
    // and reconnect even when the WiFi link was stable.
    //
    // For now, the ESP32 relies on the Arduino core's built-in
    // WiFi.setAutoReconnect(true) to handle reconnection at the
    // SDK level, without any application-level retry loop.
    // ============================================================
}

void AppController::handleLedState()
{
    if (lastLedState != ledState) {
        lastLedState = ledState;

        for (size_t i = 0; i < LED_OUTPUT_COUNT; i++) {
            outputs_object[i].value = ledState;
        }
        outputsController_.applyToHardware();
    }
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
// rawPayloadToRegValString - convert raw bytes to String
// ============================================================

String AppController::rawPayloadToRegValString(
    uint16_t regAddr,
    const uint8_t* data,
    size_t len)
{
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

void AppController::onBinaryFrameReceived(
    const MyBusHeader& hdr,
    const std::vector<uint8_t>& payload)
{
    ECOSMART_LOGI(TAG_BIN, "cmd=%u payloadLen=%u",
                  hdr.command, static_cast<unsigned>(payload.size()));

    switch (hdr.command) {

        case mybus_proto::COMMAND_WRITE_REGISTRY: {
            if (payload.size() < 3) {
                ECOSMART_LOGW(TAG_BIN, "WRITE payload too short");
                return;
            }

            const uint16_t regAddr =
                static_cast<uint16_t>(payload[0] | (payload[1] << 8));
            const uint8_t* value = payload.data() + 2;
            const size_t valueLen = payload.size() - 2;

            const String regVal = rawPayloadToRegValString(regAddr, value, valueLen);

            ECOSMART_LOGI(TAG_BIN, "WRITE 0x%04X = '%s'",
                          regAddr, regVal.c_str());

            bool handledLocally =
                inputsController_.write(regAddr, regVal)
                || audioController_.write(regAddr, regVal)
                || rgbController_.write(regAddr, regVal)
                || curtainController_.write(regAddr, regVal)
                || cloudController_.write(regAddr, regVal)
                || outputsController_.writeOutput(regAddr, regVal);

            if (handledLocally) {
                ECOSMART_LOGI(TAG_BIN, "Handled locally");
            } else {
                ECOSMART_LOGW(TAG_BIN, "Not a local register - ignored");
            }
            break;
        }

        case mybus_proto::COMMAND_READ_REGISTRY:
            ECOSMART_LOGI(TAG_BIN,
                "READ_REGISTRY 0x%04X (handled by WS server)",
                payload.size() >= 2
                    ? (payload[0] | (payload[1] << 8))
                    : 0);
            break;

        case mybus_proto::COMMAND_WS_STATUS:
        case mybus_proto::COMMAND_WS_USERS_LIST:
        case mybus_proto::COMMAND_WS_SITE_INFO:
        case mybus_proto::COMMAND_WS_WELCOME:
        case mybus_proto::COMMAND_WS_ERROR:
            break;

        default:
            ECOSMART_LOGW(TAG_BIN, "Unhandled cmd: %u", hdr.command);
            break;
    }
}