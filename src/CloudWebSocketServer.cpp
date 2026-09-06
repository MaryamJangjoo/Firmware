#include "CloudWebSocketServer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

CloudWebSocketServer::CloudWebSocketServer(MybusTransport& mybus, CloudStorage& storage,
                                            const String& deviceId, const String& siteId)
    : mybus_(mybus), storage_(storage), deviceId_(deviceId), siteId_(siteId)
{
}

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

uint32_t CloudWebSocketServer::nextRequestNumber()
{
    ++requestNumber_;
    if (requestNumber_ == 0) requestNumber_ = 1;
    return requestNumber_;
}

void CloudWebSocketServer::start()
{
    if (server_ != nullptr) return;

    server_ = new AsyncWebServer(80);
    ws_ = new AsyncWebSocket("/ws");

    ws_->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onEvent(server, client, type, arg, data, len);
    });

    server_->addHandler(ws_);
    server_->begin();

    Serial.println("[WS] WebSocket server started");
    Serial.println("[WS] Path: /ws");
}

void CloudWebSocketServer::loop()
{
    if (ws_ != nullptr) {
        ws_->cleanupClients();
    }
}

bool CloudWebSocketServer::isConnected() const
{
    return connected_ && client_ != nullptr;
}

bool CloudWebSocketServer::sendRealtimeData(JsonDocument& data)
{
    if (!connected_ || client_ == nullptr) {
        Serial.println("[WS] Cannot send: not connected");
        return false;
    }

    String response;
    serializeJson(data, response);

    if (!client_->text(response)) {
        Serial.println("[WS] Failed to send message");
        return false;
    }
    return true;
}

void CloudWebSocketServer::requestSiteInfo()
{
    if (!isConnected()) {
        Serial.println("[WS] WebSocket not connected");
        return;
    }
    JsonDocument request;
    request["action"] = "GET_SITE_INFO";
    request["deviceId"] = deviceId_;
    sendRealtimeData(request);
}

void CloudWebSocketServer::requestUsersList()
{
    if (!isConnected()) {
        Serial.println("[WS] WebSocket not connected");
        return;
    }
    JsonDocument request;
    request["action"] = "GET_USERS";
    request["deviceId"] = deviceId_;
    sendRealtimeData(request);
}

void CloudWebSocketServer::onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                    AwsEventType type, void* arg, uint8_t* data, size_t len)
{
    switch (type) {
        case WS_EVT_CONNECT: {
            Serial.printf("[WS] Client connected: %u\n", client->id());
            client_ = client;
            connected_ = true;

            JsonDocument welcome;
            welcome["type"] = "connection_ack";
            welcome["message"] = "Connected to ESP32 device";
            welcome["deviceId"] = deviceId_;
            welcome["uptime"] = millis() / 1000;
            welcome["freeHeap"] = ESP.getFreeHeap();
            welcome["wifiRSSI"] = WiFi.RSSI();
            sendRealtimeData(welcome);
            requestUsersList();
            break;
        }
        case WS_EVT_DISCONNECT: {
            Serial.printf("[WS] Client disconnected: %u\n", client->id());
            if (client_ == client) {
                client_ = nullptr;
                connected_ = false;
            }
            break;
        }
        case WS_EVT_DATA: {
            handleMessage(arg, data, len);
            break;
        }
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}

void CloudWebSocketServer::handleMessage(void* arg, uint8_t* data, size_t len)
{
    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (info == nullptr || data == nullptr) return;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

    String message;
    message.reserve(len + 1);
    for (size_t i = 0; i < len; ++i) {
        message += static_cast<char>(data[i]);
    }

    Serial.printf("[WS] Message received: %s\n", message.c_str());

    JsonDocument doc;
    if (deserializeJson(doc, message) != DeserializationError::Ok) {
        Serial.println("[WS] JSON parse error");
        return;
    }

    if (!doc["action"].is<const char*>()) return;
    String action = doc["action"].as<String>();

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

    if (action == "GET_USERS") {
        std::vector<UserInfo> users;
        if (storage_.loadUsers(users)) {
            JsonDocument response;
            response["type"] = "users_list";
            response["count"] = users.size();
            JsonArray usersArray = response["users"].to<JsonArray>();
            for (const auto& user : users) {
                JsonObject obj = usersArray.add<JsonObject>();
                obj["username"] = user.username;
                obj["role"] = user.role;
                obj["lastLogin"] = user.lastLogin;
            }
            sendRealtimeData(response);
        } else {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "No users found";
            sendRealtimeData(response);
        }
        return;
    }

    if (action == "GET_SITE_INFO") {
        SiteInfo info;
        if (storage_.loadSiteInfo(info)) {
            JsonDocument response;
            response["type"] = "site_info_response";
            response["siteId"] = info.siteId;
            response["siteName"] = info.siteName;
            response["licenseKey"] = info.licenseKey;
            response["expiryDate"] = info.expiryDate;
            response["maxUsers"] = info.maxUsers;
            sendRealtimeData(response);
        } else {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "No site info found";
            sendRealtimeData(response);
        }
        return;
    }

    if (action == "GET_REGISTRY") {
        uint16_t regAddr = doc["RegAdd"] | 0;

        JsonDocument req;
        req["RegAdd"] = regAddr;
        req["RegVal"] = "";

        JsonDocument response;
        bool sent = mybus_.sendMybusData(req, nextRequestNumber(), &response);
        bool readSuccess = sent && (response["success"] | false);

        if (readSuccess) {
            JsonDocument wsMsg;
            wsMsg["type"] = "registry_response";
            wsMsg["RegAdd"] = regAddr;
            if (!response["value"].isNull()) {
                wsMsg["value"] = response["value"];
            }
            wsMsg["payloadHex"] = response["payloadHex"] | "";
            wsMsg["command"] = response["command"] | 0;
            wsMsg["flags"] = response["flags"] | 0;
            sendRealtimeData(wsMsg);
        } else {
            JsonDocument errorMsg;
            errorMsg["type"] = "error";
            errorMsg["message"] = "Failed to read registry";
            errorMsg["RegAdd"] = regAddr;
            if (!sent) {
                errorMsg["reason"] = "transport_error";
            } else if (response["errorCode"].is<int>()) {
                errorMsg["reason"] = "backend_error";
                errorMsg["errorCode"] = response["errorCode"];
            } else {
                errorMsg["reason"] = "unknown";
            }
            sendRealtimeData(errorMsg);
        }
        return;
    }

    if (action == "WRITE_REGISTRY") {
        handleWriteRegistry(doc);
        return;
    }

    // ⚠️ COMMAND (legacy، رمزنگاری مستقیم با کلید نشست):
    // این اکشن در نسخه‌ی قدیمی از CloudManager::decryptPayload استفاده
    // می‌کرد که مستقیماً به sessionKey خام دسترسی داشت. در معماری جدید،
    // CloudWebSocketServer فقط MybusTransport& دارد، نه MybusSession&،
    // و MybusTransport هیچ متد عمومی‌ای برای رمزگشایی یک payload دلخواه
    // (غیر از فریم‌های Registry) ندارد. پیاده‌سازی درست این اکشن نیاز به
    // یک متد جدید مثل MybusTransport::decryptLegacyPayload(...) دارد که
    // session_.sessionKey() را expose کند. تا آن زمان، این اکشن را با یک
    // خطای صریح پاسخ می‌دهیم به‌جای شبیه‌سازی رفتار قدیمی به‌شکل ناقص.
    if (action == "COMMAND") {
        JsonDocument response;
        response["type"] = "error";
        response["message"] = "COMMAND action not supported in refactored transport yet";
        sendRealtimeData(response);
        Serial.println("[WS] ⚠️ COMMAND action received but not implemented (see code comment)");
        return;
    }

    if (action == "HANDSHAKE") {
        JsonDocument response;
        response["type"] = "handshake_response";
        response["status"] = "success";
        response["message"] = "Use HTTP /devices/handshake for mYBUS v2";
        sendRealtimeData(response);
        return;
    }

    JsonDocument response;
    response["type"] = "error";
    response["message"] = String("Unknown action: ") + action;
    sendRealtimeData(response);
    Serial.printf("[WS] Unknown action: %s\n", action.c_str());
}

void CloudWebSocketServer::handleWriteRegistry(JsonDocument& doc)
{
    Serial.println("[WS] 📝 WRITE_REGISTRY called");

    uint16_t regAddr = doc["RegAdd"] | 0;
    String regVal = doc["RegVal"] | "";
    uint8_t busDeviceId = doc["DeviceId"] | 1;

    if (commandCallback_) {
        JsonDocument localCmd;
        localCmd["action"] = "SET_REGISTRY";
        localCmd["RegAdd"] = regAddr;
        localCmd["RegVal"] = regVal;
        localCmd["DeviceId"] = busDeviceId;
        commandCallback_(localCmd);
    }

    JsonDocument req;
    req["RegAdd"] = regAddr;
    req["RegVal"] = regVal;
    req["DeviceId"] = busDeviceId;

    JsonDocument response;
    bool sent = mybus_.sendMybusData(req, nextRequestNumber(), &response);
    bool registrySuccess = sent && (response["success"] | false);

    if (registrySuccess) {
        Serial.println("[WS] ✅ WRITE_REGISTRY successful");
        JsonDocument wsMsg;
        wsMsg["type"] = "registry_write_response";
        wsMsg["RegAdd"] = regAddr;
        wsMsg["status"] = "success";
        wsMsg["command"] = response["command"] | 0;
        wsMsg["flags"] = response["flags"] | 0;
        sendRealtimeData(wsMsg);
    } else {
        Serial.println("[WS] ❌ WRITE_REGISTRY failed");
        JsonDocument errorMsg;
        errorMsg["type"] = "error";
        errorMsg["message"] = "Failed to write registry";
        errorMsg["RegAdd"] = regAddr;
        if (!sent) {
            errorMsg["reason"] = "transport_error";
        } else if (response["errorCode"].is<int>()) {
            errorMsg["reason"] = "backend_error";
            errorMsg["errorCode"] = response["errorCode"];
        } else {
            errorMsg["reason"] = "unknown";
        }
        sendRealtimeData(errorMsg);
    }
}