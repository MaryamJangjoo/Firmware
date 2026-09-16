#include "AppController.h"
#include <esp_system.h>
#include "crypto.hpp"
#include <WiFi.h>
#include "mybus_protocol_constants.h"
#include <esp_wifi.h>
#include <ESPmDNS.h>

#ifndef WIFI_STA
#define WIFI_STA 1
#endif

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
    // Populate the base-class list after all controllers are constructed.
    // The Cloud controller is appended so its registers participate in
    // the same iteration path as the others.
    registryControllers_ = {
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
    Serial.println("System Starting ....");

    pinMode(PIN_I2S_PDN, OUTPUT);
    digitalWrite(PIN_I2S_PDN, HIGH);
    delay(10);
    Serial.println("PDN pin set HIGH (TAS5805M active)");

    ecosmart_registery_init();

    // Load cloud connectivity registers from NVS before any
    // registry read/write can occur. The store applies its
    // values to cloud_object through the bindings created by
    // ecosmart_registery_init().
    cloudStore_.begin();
    cloud_object.server_fqdn = cloudStore_.getServerFqdn();
    cloud_object.server_ip   = cloudStore_.getServerIp();
    cloud_object.server_port = cloudStore_.getServerPort();
    cloud_object.username    = cloudStore_.getUsername();
    cloud_object.password    = cloudStore_.getPassword();
    cloud_object.device_id   = cloudStore_.getDeviceId();

    if (!connectToWiFi()) {
        Serial.println("[ERROR] WiFi connection failed. Retrying in 5 seconds...");
        delay(5000);
        ESP.restart();
        return;
    }

    // mDNS: ecosmart.local
    if (!MDNS.begin("ecosmart")) {
        Serial.println("[mDNS] Failed to start");
    } else {
        Serial.println("[mDNS] Started: http://ecosmart.local");
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "device", "EcoSmart");
        MDNS.addServiceTxt("http", "tcp", "version", "2.0.0");
    }

    initCloudManager();

    // Wire the controller to CloudManager so IP / Port / credentials
    // changes are propagated as soon as they are written.
    cloudController_.attachCloudManager(cloudManager);
    cloudController_.syncDeviceIdFromCloudManager();

    // If the store contains a persisted API base URL, prefer it
    // over the compile-time default.
    if (cloudStore_.isConfigured()) {
        const String persistedUrl = cloudStore_.buildApiBaseUrl();
        if (!persistedUrl.isEmpty()) {
            Serial.printf("[CLOUD-REG] Applying persisted apiBaseUrl: %s\n",
                          persistedUrl.c_str());
            cloudManager->setApiBaseUrl(persistedUrl);
        }
    }

    configureMybusAddress();

    Serial.println("[AUTH] Attempting to login...");
    bool loginSuccess = cloudManager->loginUser(
        cloud_object.username.isEmpty()
            ? String(OWNER_USERNAME)
            : cloud_object.username,
        cloud_object.password.isEmpty()
            ? String(OWNER_PASSWORD)
            : cloud_object.password,
        cloudManager->getDeviceId());

    if (loginSuccess) {
        Serial.println("[AUTH] Login successful!");
    } else {
        Serial.println("[AUTH] Login failed, trying offline...");
        if (cloudManager->loginOffline(OWNER_USERNAME, OWNER_PASSWORD)) {
            Serial.println("[AUTH] Offline login successful!");
        } else {
            Serial.println("[AUTH] Offline login failed!");
        }
    }

    if (cloudManager->isLoggedIn()) {
        if (cloudManager->isSecureSessionEstablished()) {
            Serial.println("[mYBUS] Using restored session from NVS, skipping handshake");
        } else {
            Serial.println("[mYBUS] Starting handshake...");
            if (cloudManager->performHandshake()) {
                Serial.println("[mYBUS] Handshake successful!");
            } else {
                Serial.println("[mYBUS] Handshake failed!");
            }
        }
    }

    cloudManager->startWebSocketServer();
    Serial.println("[WS] WebSocket server started on /ws");

    initAudioHardware();

    Serial.println();
    Serial.println("========================================");
    Serial.println("ESP32 Ready! (Binary WS mode)");
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

// ============================================================
// WiFi Connection
// ============================================================

static const char* wifiStatusToString(wl_status_t status);

bool AppController::connectToWiFi()
{
    Serial.println();
    Serial.print("[WiFi] MAC Address: ");
    Serial.println(WiFi.macAddress());

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

    Serial.println("[WiFi] Scanning networks...");
    int networksFound = WiFi.scanNetworks();

    bool targetFoundOn24GHz = false;
    bool targetIsOpen = false;

    if (networksFound == 0) {
        Serial.println("[WiFi] No networks found at all (radio issue?)");
    } else {
        Serial.printf("[WiFi] Found %d networks:\n", networksFound);

        for (int i = 0; i < networksFound; i++) {
            String ssid = WiFi.SSID(i);
            int32_t rssi = WiFi.RSSI(i);
            wifi_auth_mode_t enc = WiFi.encryptionType(i);
            int32_t channel = WiFi.channel(i);

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

            Serial.printf("  [%d] SSID='%s' RSSI=%d Channel=%d Enc=%s\n",
                          i, ssid.c_str(), rssi, channel, encStr);

            if (ssid == String(WIFI_SSID)) {
                targetFoundOn24GHz = true;
                targetIsOpen = (enc == WIFI_AUTH_OPEN);

                Serial.printf(
                    "[WiFi] Target SSID found (RSSI=%d, Enc=%s)\n",
                    rssi, encStr
                );

                if (enc == WIFI_AUTH_WPA3_PSK) {
                    Serial.println(
                        "[WiFi] Network is WPA3-only - ESP32 may fail to connect."
                    );
                }
            }
        }

        if (!targetFoundOn24GHz) {
            Serial.printf(
                "[WiFi] Target SSID '%s' was NOT found in scan.\n",
                WIFI_SSID
            );
        }
    }

    WiFi.scanDelete();

    Serial.println();
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);

    if (targetIsOpen) {
        Serial.println(
            "[WiFi] Target network scanned as OPEN - connecting without password"
        );
        WiFi.begin(WIFI_SSID);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    int attempts = 0;
    wl_status_t lastStatus = WL_IDLE_STATUS;

    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);

        wl_status_t status = WiFi.status();
        if (status != lastStatus) {
            Serial.printf("\n[WiFi] status=%d (%s)\n",
                          status, wifiStatusToString(status));
            lastStatus = status;
        } else {
            Serial.print(".");
        }

        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());

        // NOTE: Do NOT call WiFi.setSleep(false) here. It breaks WiFi+BT
        // coexistence and causes abort() in coex_core_enable when
        // btAudio::begin() runs.
        WiFi.setAutoReconnect(true);
        Serial.println("[WiFi] Auto-reconnect enabled");
        return true;
    }

    Serial.printf(
        "[WiFi] Connection failed! Final status=%d (%s)\n",
        WiFi.status(), wifiStatusToString(WiFi.status())
    );

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
    Serial.println("[CLOUD] Initializing CloudManager...");
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

        // Try each registry controller in order until one handles
        // the address.
        bool handled = false;
        for (auto* ctrl : registryControllers_) {
            if (ctrl->read(regAddr, rv)) {
                handled = true;
                break;
            }
        }

        // Fall back to the outputs controller for input registers,
        // which are not part of the base-class candidate list.
        if (!handled) {
            handled = outputsController_.readLocal(regAddr, rv);
        }

        if (!handled) {
            return false;
        }

        outValue.stringValue = rv.stringValue;

        // Clamp the copied length to the destination buffer size to
        // avoid reading past the source array and to avoid leaving
        // stale bytes in the destination when byteLen < sizeof(bytes).
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
            audioController_.write(regAddr, value)
            || rgbController_.write(regAddr, value)
            || curtainController_.write(regAddr, value)
            || cloudController_.write(regAddr, value)
            || outputsController_.writeOutput(regAddr, value);

        Serial.printf("[LOCAL-WS] %s 0x%04X = '%s'\n",
                      handled ? "OK" : "FAIL",
                      regAddr, value.c_str());

        return handled;
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
    // ============================================================
    // DISABLED: WiFi reconnect logic is currently disabled.
    //
    // The original implementation called WiFi.reconnect() every
    // WIFI_RECONNECT_ATTEMPT_INTERVAL_MS while in RECONNECTING
    // state, but this caused the ESP32 to repeatedly disconnect
    // and reconnect even when the WiFi link was stable. The exact
    // root cause is still under investigation.
    //
    // For now, the ESP32 relies on the Arduino core's built-in
    // WiFi.setAutoReconnect(true) to handle reconnection at the
    // SDK level, without any application-level retry loop.
    //
    // To re-enable, uncomment the original implementation below.
    // ============================================================

    // if (WiFi.status() == WL_CONNECTED) {
    //     if (wifiReconnectState_ == WifiReconnectState::RECONNECTING) {
    //         Serial.println("[WiFi] Reconnected!");
    //         wifiReconnectState_ = WifiReconnectState::IDLE;
    //     }
    //     return;
    // }
    //
    // const unsigned long now = millis();
    //
    // if (wifiReconnectState_ == WifiReconnectState::IDLE) {
    //     Serial.println("[WiFi] Connection lost. Reconnecting...");
    //     WiFi.reconnect();
    //     wifiReconnectState_ = WifiReconnectState::RECONNECTING;
    //     wifiReconnectStartMs_ = now;
    //     wifiLastAttemptMs_ = now;
    //     return;
    // }
    //
    // if (wifiReconnectState_ == WifiReconnectState::RECONNECTING) {
    //     if (now - wifiLastAttemptMs_ >= WIFI_RECONNECT_ATTEMPT_INTERVAL_MS) {
    //         Serial.println("[WiFi] Retrying reconnect...");
    //         WiFi.reconnect();
    //         wifiLastAttemptMs_ = now;
    //     }
    //
    //     if (now - wifiReconnectStartMs_ >= WIFI_RECONNECT_TIMEOUT_MS) {
    //         Serial.println("[WiFi] Reconnect timeout, will retry on next loop pass");
    //         wifiReconnectState_ = WifiReconnectState::IDLE;
    //     }
    // }
}

void AppController::handleLedState()
{
    // Legacy: nothing sets ledState anymore. If a future physical
    // input toggles it, only the first LED_OUTPUT_COUNT outputs
    // (the LEDs) should be affected - never the curtain channels
    // at indices 14 and 15.
    if (lastLedState != ledState) {
        lastLedState = ledState;

        for (size_t i = 0; i < LED_OUTPUT_COUNT; i++) {
            outputs_object[i].value = ledState;
        }
        outputsController_.applyToHardware();
    }
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
    Serial.printf("[BIN] cmd=%u payloadLen=%u\n",
                  hdr.command, static_cast<unsigned>(payload.size()));

    switch (hdr.command) {

        case mybus_proto::COMMAND_WRITE_REGISTRY: {
            if (payload.size() < 3) {
                Serial.println("[BIN] WRITE payload too short");
                return;
            }

            const uint16_t regAddr =
                static_cast<uint16_t>(payload[0] | (payload[1] << 8));
            const uint8_t* value = payload.data() + 2;
            const size_t valueLen = payload.size() - 2;

            const String regVal = rawPayloadToRegValString(regAddr, value, valueLen);

            Serial.printf("[BIN] WRITE 0x%04X = '%s'\n",
                          regAddr, regVal.c_str());

            bool handledLocally =
                audioController_.write(regAddr, regVal) ||
                rgbController_.write(regAddr, regVal) ||
                curtainController_.write(regAddr, regVal) ||
                cloudController_.write(regAddr, regVal) ||
                outputsController_.writeOutput(regAddr, regVal);

            if (handledLocally) {
                Serial.println("[BIN] Handled locally");
            } else {
                Serial.println("[BIN] Not a local register - ignored");
            }
            break;
        }

        case mybus_proto::COMMAND_READ_REGISTRY:
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
            break;

        default:
            Serial.printf("[BIN] Unhandled cmd: %u\n", hdr.command);
            break;
    }
}