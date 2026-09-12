#include "CloudWebSocketServer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <vector>

#include "mybus_frame.h"
#include "mybus_protocol_constants.h"
#include "CloudStorage.h"
// ============================================================
// Constructor
// ============================================================

CloudWebSocketServer::CloudWebSocketServer(
    MybusTransport& mybus,
    CloudStorage& storage,
    const String& deviceId,
    const String& siteId
)
    : mybus_(mybus),
      storage_(storage),
      deviceId_(deviceId),
      siteId_(siteId)
{
}

// ============================================================
// Destructor
// ============================================================

CloudWebSocketServer::~CloudWebSocketServer()
{
    if (ws_ != nullptr) {
        ws_->closeAll();

        delete ws_;
        ws_ = nullptr;
    }

    if (server_ != nullptr) {
        delete server_;
        server_ = nullptr;
    }

    client_ = nullptr;
    connected_ = false;
}

// ============================================================
// Request number
// ============================================================

uint32_t CloudWebSocketServer::nextRequestNumber()
{
    ++requestNumber_;

    if (requestNumber_ == 0) {
        requestNumber_ = 1;
    }

    return requestNumber_;
}

// ============================================================
// Start
// ============================================================

void CloudWebSocketServer::start()
{
    if (server_ != nullptr) {
        return;
    }

    server_ = new AsyncWebServer(80);

    ws_ = new AsyncWebSocket("/ws");

    ws_->onEvent(
        [this](
            AsyncWebSocket* server,
            AsyncWebSocketClient* client,
            AwsEventType type,
            void* arg,
            uint8_t* data,
            size_t len
        ) {
            this->onEvent(
                server,
                client,
                type,
                arg,
                data,
                len
            );
        }
    );

    server_->addHandler(ws_);

    static constexpr const char* FIRMWARE_VERSION = "2.0.0";       
    static constexpr const char* PART_NUMBER      = "SEC-BLB56001"; 
    server_->on("/info", HTTP_GET, [this](AsyncWebServerRequest *request) {

        AsyncResponseStream *response =
            request->beginResponseStream("application/json");

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();

        root["deviceId"]        = deviceId_;
        root["cloudConnected"]  = connected_;

        
        char serialHex[9];
        snprintf(serialHex, sizeof(serialHex), "%08X",
                  static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFu));
        root["serialNumber"]    = String(serialHex);

        root["partNumber"]      = PART_NUMBER;
        root["firmwareVersion"] = FIRMWARE_VERSION;

        root["uptime"]   = millis() / 1000;

        root["ramTotal"] = ESP.getHeapSize();
        root["ramFree"]  = ESP.getFreeHeap();

        root["storageTotal"] = storage_.getStorageTotalBytes();
        root["storageUsed"]  = storage_.getStorageUsedBytes();

        root["wifiRSSI"] = WiFi.RSSI();

        serializeJson(root, *response);
        request->send(response);
    });

    server_->begin();

    Serial.println("[WS] WebSocket server started");
    Serial.println("[WS] Path: /ws");
    Serial.println("[WS] HTTP: GET /info (binary frame)");
}

// ============================================================
// Loop
// ============================================================

void CloudWebSocketServer::loop()
{
    if (ws_ != nullptr) {
        ws_->cleanupClients();
    }
}

// ============================================================
// Connection
// ============================================================

bool CloudWebSocketServer::isConnected() const
{
    return connected_ && client_ != nullptr;
}

// ============================================================
// Send realtime data
// ============================================================

bool CloudWebSocketServer::sendRealtimeData(
    JsonDocument& data
)
{
    if (!connected_ || client_ == nullptr) {

        Serial.println(
            "[WS] Cannot send: not connected"
        );

        return false;
    }

    String response;

    serializeJson(
        data,
        response
    );

    if (!client_->text(response)) {

        Serial.println(
            "[WS] Failed to send message"
        );

        return false;
    }

    return true;
}

// ============================================================
// Request Site Info
// ============================================================

void CloudWebSocketServer::requestSiteInfo()
{
    if (!isConnected()) {

        Serial.println(
            "[WS] WebSocket not connected"
        );

        return;
    }

    JsonDocument request;

    request["action"] = "GET_SITE_INFO";
    request["deviceId"] = deviceId_;

    sendRealtimeData(request);
}

// ============================================================
// Request Users
// ============================================================

void CloudWebSocketServer::requestUsersList()
{
    if (!isConnected()) {

        Serial.println(
            "[WS] WebSocket not connected"
        );

        return;
    }

    JsonDocument request;

    request["action"] = "GET_USERS";
    request["deviceId"] = deviceId_;

    sendRealtimeData(request);
}

// ============================================================
// WebSocket Event
// ============================================================

void CloudWebSocketServer::onEvent(
    AsyncWebSocket* server,
    AsyncWebSocketClient* client,
    AwsEventType type,
    void* arg,
    uint8_t* data,
    size_t len
)
{
    switch (type) {

        // --------------------------------------------------------
        // CONNECT
        // --------------------------------------------------------

        case WS_EVT_CONNECT: {

            Serial.printf(
                "[WS] Client connected: %u\n",
                client->id()
            );

            client_ = client;
            connected_ = true;

            JsonDocument welcome;

            welcome["type"] = "connection_ack";
            welcome["message"] =
                "Connected to ESP32 device";
            welcome["deviceId"] = deviceId_;
            welcome["uptime"] = millis() / 1000;
            welcome["freeHeap"] = ESP.getFreeHeap();
            welcome["wifiRSSI"] = WiFi.RSSI();

            sendRealtimeData(welcome);

            requestUsersList();

            break;
        }

        // --------------------------------------------------------
        // DISCONNECT
        // --------------------------------------------------------

        case WS_EVT_DISCONNECT: {

            Serial.printf(
                "[WS] Client disconnected: %u\n",
                client->id()
            );

            if (client_ == client) {

                client_ = nullptr;
                connected_ = false;
            }

            break;
        }

        // --------------------------------------------------------
        // DATA
        // --------------------------------------------------------

        case WS_EVT_DATA: {

            handleMessage(
                arg,
                data,
                len
            );

            break;
        }

        // --------------------------------------------------------
        // Other events
        // --------------------------------------------------------

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}

// ============================================================
// Handle Message
// ============================================================

void CloudWebSocketServer::handleMessage(
    void* arg,
    uint8_t* data,
    size_t len
)
{
    AwsFrameInfo* info =
        reinterpret_cast<AwsFrameInfo*>(arg);

    if (info == nullptr || data == nullptr) {
        return;
    }

    // Only handle complete TEXT frames.
    if (!info->final ||
        info->index != 0 ||
        info->len != len ||
        info->opcode != WS_TEXT) {

        return;
    }

    String message;

    message.reserve(len + 1);

    for (size_t i = 0; i < len; ++i) {
        message += static_cast<char>(data[i]);
    }

    Serial.printf(
        "[WS] Message received: %s\n",
        message.c_str()
    );

    JsonDocument doc;

    if (
        deserializeJson(doc, message)
        != DeserializationError::Ok
    ) {

        Serial.println(
            "[WS] JSON parse error"
        );

        return;
    }

    if (!doc["action"].is<const char*>()) {
        return;
    }

    String action =
        doc["action"].as<String>();

    // ============================================================
    // GET_STATUS
    // ============================================================

    if (action == "GET_STATUS") {

        JsonDocument response;

        response["type"] = "status";
        response["deviceId"] = deviceId_;
        response["status"] = "online";
        response["siteId"] = siteId_;
        response["uptime"] = millis() / 1000;
        response["freeHeap"] = ESP.getFreeHeap();
        response["wifiRSSI"] = WiFi.RSSI();

        sendRealtimeData(response);

        return;
    }

    // ============================================================
    // GET_USERS
    // ============================================================

    if (action == "GET_USERS") {

        std::vector<UserInfo> users;

        if (storage_.loadUsers(users)) {

            JsonDocument response;

            response["type"] = "users_list";
            response["count"] = users.size();

            JsonArray usersArray =
                response["users"].to<JsonArray>();

            for (const auto& user : users) {

                JsonObject obj =
                    usersArray.add<JsonObject>();

                obj["username"] = user.username;
                obj["role"] = user.role;
                obj["lastLogin"] = user.lastLogin;
            }

            sendRealtimeData(response);

        } else {

            JsonDocument response;

            response["type"] = "error";
            response["message"] =
                "No users found";

            sendRealtimeData(response);
        }

        return;
    }

    // ============================================================
    // GET_SITE_INFO
    // ============================================================

    if (action == "GET_SITE_INFO") {

        SiteInfo info;

        if (storage_.loadSiteInfo(info)) {

            JsonDocument response;

            response["type"] =
                "site_info_response";

            response["siteId"] =
                info.siteId;

            response["siteName"] =
                info.siteName;

            response["licenseKey"] =
                info.licenseKey;

            response["expiryDate"] =
                info.expiryDate;

            response["maxUsers"] =
                info.maxUsers;

            sendRealtimeData(response);

        } else {

            JsonDocument response;

            response["type"] = "error";
            response["message"] =
                "No site info found";

            sendRealtimeData(response);
        }

        return;
    }

    // ============================================================
    // GET_REGISTRY
    // ============================================================

    if (action == "GET_REGISTRY") {

        uint16_t regAddr =
            doc["RegAdd"] | 0;

        Serial.printf(
            "[WS] GET_REGISTRY: 0x%04X\n",
            regAddr
        );

        // --------------------------------------------------------
        // 1. Try local register first.
        //
        // CloudWebSocketServer does not know LocalRegisterMap.
        // AppController decides whether the register is local.
        // --------------------------------------------------------

        if (localReadCallback_) {

            JsonDocument localValue;

            if (
                localReadCallback_(
                    regAddr,
                    localValue
                )
            ) {

                Serial.printf(
                    "[WS] ✅ Local registry read: 0x%04X\n",
                    regAddr
                );

                JsonDocument wsMsg;

                wsMsg["type"] =
                    "registry_response";

                wsMsg["RegAdd"] =
                    regAddr;

                if (!localValue["value"].isNull()) {

                    wsMsg["value"] =
                        localValue["value"];
                }

                sendRealtimeData(wsMsg);

                return;
            }
        }

        // --------------------------------------------------------
        // 2. Not local -> physical mYBUS
        // --------------------------------------------------------

        Serial.printf(
            "[WS] ℹ️ Registry 0x%04X is not local, "
            "forwarding to mYBUS\n",
            regAddr
        );

        JsonDocument req;

        req["RegAdd"] = regAddr;
        req["RegVal"] = "";

        JsonDocument response;

        bool sent =
            mybus_.sendMybusData(
                req,
                nextRequestNumber(),
                &response
            );

        bool readSuccess =
            sent &&
            (response["success"] | false);

        if (readSuccess) {

            JsonDocument wsMsg;

            wsMsg["type"] =
                "registry_response";

            wsMsg["RegAdd"] =
                regAddr;

            if (!response["value"].isNull()) {

                wsMsg["value"] =
                    response["value"];
            }

            wsMsg["payloadHex"] =
                response["payloadHex"] | "";

            wsMsg["command"] =
                response["command"] | 0;

            wsMsg["flags"] =
                response["flags"] | 0;

            sendRealtimeData(wsMsg);

        } else {

            JsonDocument errorMsg;

            errorMsg["type"] = "error";

            errorMsg["message"] =
                "Failed to read registry";

            errorMsg["RegAdd"] =
                regAddr;

            if (!sent) {

                errorMsg["reason"] =
                    "transport_error";

            } else if (
                response["errorCode"].is<int>()
            ) {

                errorMsg["reason"] =
                    "backend_error";

                errorMsg["errorCode"] =
                    response["errorCode"];

            } else {

                errorMsg["reason"] =
                    "unknown";
            }

            sendRealtimeData(errorMsg);
        }

        return;
    }

    // ============================================================
    // WRITE_REGISTRY
    // ============================================================

    if (action == "WRITE_REGISTRY") {

        handleWriteRegistry(doc);

        return;
    }

    // ============================================================
    // COMMAND
    // ============================================================

    if (action == "COMMAND") {

        JsonDocument response;

        response["type"] = "error";

        response["message"] =
            "COMMAND action not supported "
            "in refactored transport yet";

        sendRealtimeData(response);

        Serial.println(
            "[WS] ⚠️ COMMAND action received "
            "but not implemented"
        );

        return;
    }

    // ============================================================
    // HANDSHAKE
    // ============================================================

    if (action == "HANDSHAKE") {

        JsonDocument response;

        response["type"] =
            "handshake_response";

        response["status"] =
            "success";

        response["message"] =
            "Use HTTP /devices/handshake "
            "for mYBUS v2";

        sendRealtimeData(response);

        return;
    }

    // ============================================================
    // Unknown Action
    // ============================================================

    JsonDocument response;

    response["type"] = "error";

    response["message"] =
        String("Unknown action: ") + action;

    sendRealtimeData(response);

    Serial.printf(
        "[WS] Unknown action: %s\n",
        action.c_str()
    );
}

// ============================================================
// WRITE_REGISTRY
// ============================================================

void CloudWebSocketServer::handleWriteRegistry(
    JsonDocument& doc
)
{
    Serial.println(
        "[WS] 📝 WRITE_REGISTRY called"
    );

    uint16_t regAddr =
        doc["RegAdd"] | 0;

    String regVal =
        doc["RegVal"] | "";

    uint8_t busDeviceId =
        doc["DeviceId"] | 1;

    // ============================================================
    // 1. Notify AppController.
    //
    // This allows local registers such as Audio Volume/Title/etc.
    // to be handled by LocalRegisterMap.
    // ============================================================

    if (commandCallback_) {

        JsonDocument localCmd;

        localCmd["action"] =
            "SET_REGISTRY";

        localCmd["RegAdd"] =
            regAddr;

        localCmd["RegVal"] =
            regVal;

        localCmd["DeviceId"] =
            busDeviceId;

        commandCallback_(localCmd);
    }

    // ============================================================
    // 2. Check whether physical mYBUS write must be skipped.
    //
    // Example:
    //
    // REG_AUDIO_VOLUME
    //     local = true
    //     mirrorToCloud = false
    //
    // Therefore:
    //
    //     LocalRegisterMap <- write
    //     mYBUS            <- NO WRITE
    // ============================================================

    if (
        shouldSkipMybusWriteCallback_ &&
        shouldSkipMybusWriteCallback_(regAddr)
    ) {

        Serial.printf(
            "[WS] ✅ Local register 0x%04X handled "
            "without mYBUS forwarding\n",
            regAddr
        );

        JsonDocument wsMsg;

        wsMsg["type"] =
            "registry_write_response";

        wsMsg["RegAdd"] =
            regAddr;

        wsMsg["status"] =
            "success";

        sendRealtimeData(wsMsg);

        return;
    }

    // ============================================================
    // 3. Non-local register OR mirrorToCloud=true
    //    -> physical mYBUS
    // ============================================================

    Serial.printf(
        "[WS] ➡️ Forwarding register 0x%04X to mYBUS\n",
        regAddr
    );

    JsonDocument req;

    req["RegAdd"] =
        regAddr;

    req["RegVal"] =
        regVal;

    req["DeviceId"] =
        busDeviceId;

    JsonDocument response;

    bool sent =
        mybus_.sendMybusData(
            req,
            nextRequestNumber(),
            &response
        );

    bool registrySuccess =
        sent &&
        (response["success"] | false);

    if (registrySuccess) {

        Serial.println(
            "[WS] ✅ WRITE_REGISTRY successful"
        );

        JsonDocument wsMsg;

        wsMsg["type"] =
            "registry_write_response";

        wsMsg["RegAdd"] =
            regAddr;

        wsMsg["status"] =
            "success";

        wsMsg["command"] =
            response["command"] | 0;

        wsMsg["flags"] =
            response["flags"] | 0;

        sendRealtimeData(wsMsg);

    } else {

        Serial.println(
            "[WS] ❌ WRITE_REGISTRY failed"
        );

        JsonDocument errorMsg;

        errorMsg["type"] =
            "error";

        errorMsg["message"] =
            "Failed to write registry";

        errorMsg["RegAdd"] =
            regAddr;

        if (!sent) {

            errorMsg["reason"] =
                "transport_error";

        } else if (
            response["errorCode"].is<int>()
        ) {

            errorMsg["reason"] =
                "backend_error";

            errorMsg["errorCode"] =
                response["errorCode"];

        } else {

            errorMsg["reason"] =
                "unknown";
        }

        sendRealtimeData(errorMsg);
    }
}