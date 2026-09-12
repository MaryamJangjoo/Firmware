#include "CloudWebSocketServer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <vector>

#include "mybus_frame.h"
#include "mybus_registry.h"
#include "mybus_protocol_constants.h"
#include "mybus_value_codec.h"
#include "CloudStorage.h"
#include "RegisterRawValue.h"

namespace {

// ------------------------------------------------------------
// ByteWriter: TLV باینری ساده، فقط برای payloadهای WS-only
// (STATUS/USERS_LIST/SITE_INFO/WELCOME).
// ------------------------------------------------------------
class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& buf) : buf_(buf) {}

    void u8(uint8_t v) { buf_.push_back(v); }

    void i8(int8_t v) { buf_.push_back(static_cast<uint8_t>(v)); }

    void u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void str8(const String& s) {
        size_t len = s.length();
        if (len > 255) len = 255;
        buf_.push_back(static_cast<uint8_t>(len));
        for (size_t i = 0; i < len; ++i) {
            buf_.push_back(static_cast<uint8_t>(s[i]));
        }
    }

private:
    std::vector<uint8_t>& buf_;
};

uint8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}

void hexStringToBytes(const String& hex, std::vector<uint8_t>& out) {
    out.clear();
    const size_t n = hex.length() / 2;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<uint8_t>(
            (hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1])));
    }
}

} // namespace


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

    ws_->onEvent(
        [this](
            AsyncWebSocket* server,
            AsyncWebSocketClient* client,
            AwsEventType type,
            void* arg,
            uint8_t* data,
            size_t len
        ) {
            this->onEvent(server, client, type, arg, data, len);
        }
    );

    server_->addHandler(ws_);

    static constexpr const char* FIRMWARE_VERSION = "2.0.0";
    static constexpr const char* PART_NUMBER      = "SEC-BLB56001";

    // فقط endpoint دیباگ JSON-over-HTTP باقی می‌ماند
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
    Serial.println("[WS] Path: /ws (binary mYBUS control frames only)");
    Serial.println("[WS] HTTP: GET /info (JSON, debug only)");
}

void CloudWebSocketServer::loop()
{
    if (ws_ != nullptr) ws_->cleanupClients();
}

bool CloudWebSocketServer::isConnected() const
{
    return connected_ && client_ != nullptr;
}


bool CloudWebSocketServer::sendControlFrame(
    uint8_t command,
    uint8_t flags,
    uint16_t requestNumber,
    const uint8_t* payload,
    size_t payloadLen)
{
    if (!connected_ || client_ == nullptr) {
        Serial.println("[WS] Cannot send: not connected");
        return false;
    }

    std::vector<uint8_t> wire;

    if (!mybus_.buildControlFrame(command, flags, requestNumber, payload, payloadLen, wire)) {
        Serial.println("[WS] ❌ Failed to build control frame");
        return false;
    }

    if (!client_->binary(wire.data(), wire.size())) {
        Serial.println("[WS] ❌ Failed to send binary frame");
        return false;
    }

    return true;
}


void CloudWebSocketServer::sendError(
    uint8_t originalCommand,
    uint16_t requestNumber,
    uint8_t reason,
    uint16_t regAddr)
{
    std::vector<uint8_t> payload;
    ByteWriter w(payload);
    w.u8(originalCommand);
    w.u8(reason);
    w.u16(regAddr);

    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT) | (1U << MYBUS_FLAG_SF_BIT);
    sendControlFrame(mybus_proto::COMMAND_WS_ERROR, flags, requestNumber,
                      payload.data(), payload.size());
}


void CloudWebSocketServer::sendRegistryReadResponse(
    uint16_t requestNumber,
    uint16_t regAddr,
    const uint8_t* value,
    size_t valueLen)
{
    std::vector<uint8_t> respPayload(2 + valueLen);
    respPayload[0] = static_cast<uint8_t>(regAddr & 0xFF);
    respPayload[1] = static_cast<uint8_t>((regAddr >> 8) & 0xFF);
    if (valueLen > 0 && value != nullptr) {
        memcpy(respPayload.data() + 2, value, valueLen);
    }

    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
    sendControlFrame(mybus_proto::COMMAND_READ_REGISTRY, flags, requestNumber,
                      respPayload.data(), respPayload.size());
}


void CloudWebSocketServer::requestSiteInfo()
{
    if (!isConnected()) {
        Serial.println("[WS] WebSocket not connected");
        return;
    }
    handleGetSiteInfo(static_cast<uint16_t>(nextRequestNumber() & 0xFFFF));
}

void CloudWebSocketServer::requestUsersList()
{
    if (!isConnected()) {
        Serial.println("[WS] WebSocket not connected");
        return;
    }
    handleGetUsers(static_cast<uint16_t>(nextRequestNumber() & 0xFFFF));
}


void CloudWebSocketServer::onEvent(
    AsyncWebSocket* server,
    AsyncWebSocketClient* client,
    AwsEventType type,
    void* arg,
    uint8_t* data,
    size_t len)
{
    switch (type) {

        case WS_EVT_CONNECT: {
            Serial.printf("[WS] Client connected: %u\n", client->id());

            client_ = client;
            connected_ = true;

            std::vector<uint8_t> payload;
            ByteWriter w(payload);
            w.str8(deviceId_);
            w.u32(millis() / 1000);
            w.u32(ESP.getFreeHeap());
            w.i8(static_cast<int8_t>(WiFi.RSSI()));

            const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
            sendControlFrame(
                mybus_proto::COMMAND_WS_WELCOME, flags,
                static_cast<uint16_t>(nextRequestNumber() & 0xFFFF),
                payload.data(), payload.size());

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
            handleBinaryMessage(arg, data, len);
            break;
        }

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}


void CloudWebSocketServer::handleBinaryMessage(void* arg, uint8_t* data, size_t len)
{
    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);

    if (info == nullptr || data == nullptr) return;

    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_BINARY) {
        Serial.println("[WS] ⚠️ Ignoring fragmented/non-binary frame");
        return;
    }

    Serial.printf("[WS] 📥 Binary control frame (%u bytes)\n", static_cast<unsigned>(len));

    static constexpr uint8_t kAllowedIncoming[] = {
        mybus_proto::COMMAND_READ_REGISTRY,
        mybus_proto::COMMAND_WRITE_REGISTRY,
        mybus_proto::COMMAND_WS_STATUS,
        mybus_proto::COMMAND_WS_USERS_LIST,
        mybus_proto::COMMAND_WS_SITE_INFO,
    };
    static constexpr size_t kAllowedIncomingCount =
        sizeof(kAllowedIncoming) / sizeof(kAllowedIncoming[0]);

    MyBusHeader hdr;
    std::vector<uint8_t> payload;
    MyBusFrameError err;

    if (!mybus_.parseControlFrame(data, len, kAllowedIncoming, kAllowedIncomingCount,
                                   hdr, payload, err)) {
        Serial.printf("[WS] ❌ Invalid control frame: %s\n", mybus_frameErrorToString(err));
        return;
    }

    switch (hdr.command) {
        case mybus_proto::COMMAND_READ_REGISTRY:
            handleReadRegistry(hdr, payload);
            break;
        case mybus_proto::COMMAND_WRITE_REGISTRY:
            handleWriteRegistryFrame(hdr, payload);
            break;
        case mybus_proto::COMMAND_WS_STATUS:
            handleGetStatus(hdr.requestNumber);
            break;
        case mybus_proto::COMMAND_WS_USERS_LIST:
            handleGetUsers(hdr.requestNumber);
            break;
        case mybus_proto::COMMAND_WS_SITE_INFO:
            handleGetSiteInfo(hdr.requestNumber);
            break;
        default:
            sendError(hdr.command, hdr.requestNumber, mybus_proto::REASON_BAD_REQUEST);
            break;
    }
}


// ============================================================
// READ_REGISTRY
// ============================================================
void CloudWebSocketServer::handleReadRegistry(
    const MyBusHeader& hdr,
    const std::vector<uint8_t>& payload)
{
    if (payload.size() < 2) {
        sendError(hdr.command, hdr.requestNumber, mybus_proto::REASON_BAD_REQUEST);
        return;
    }

    const uint16_t regAddr = static_cast<uint16_t>(payload[0] | (payload[1] << 8));

    Serial.printf("[WS] GET_REGISTRY: 0x%04X\n", regAddr);

    // 1. رجیستر لوکال؟
    if (localReadCallback_) {
        RegisterRawValue localValue;
        if (localReadCallback_(regAddr, localValue)) {
            Serial.printf("[WS] ✅ Local registry read: 0x%04X\n", regAddr);
            if (localValue.isString()) {
                const uint8_t* bytes =
                    reinterpret_cast<const uint8_t*>(localValue.stringValue.c_str());
                sendRegistryReadResponse(hdr.requestNumber, regAddr, bytes,
                                          localValue.stringValue.length());
            } else {
                sendRegistryReadResponse(hdr.requestNumber, regAddr,
                                          localValue.bytes, localValue.byteLen);
            }
            return;
        }
    }

    // 2. غیرلوکال -> mYBUS
    Serial.printf("[WS] ℹ️ Registry 0x%04X not local, forwarding to mYBUS\n", regAddr);

    const uint8_t busDeviceId = 1;

    JsonDocument response;
    const bool sent = mybus_.sendRegistryFrame(
        regAddr, nullptr, 0, /*isWrite=*/false, busDeviceId, nextRequestNumber(), &response);

    const bool readSuccess = sent && (response["success"] | false);

    if (!readSuccess) {
        const uint8_t reason = !sent ? mybus_proto::REASON_TRANSPORT_ERROR
                                      : mybus_proto::REASON_BACKEND_ERROR;
        sendError(hdr.command, hdr.requestNumber, reason, regAddr);
        return;
    }

    std::vector<uint8_t> valueBytes;
    hexStringToBytes(response["payloadHex"] | "", valueBytes);

    sendRegistryReadResponse(hdr.requestNumber, regAddr, valueBytes.data(), valueBytes.size());
}


// ============================================================
// WRITE_REGISTRY  (✅ کاملاً باینری)
// ============================================================
void CloudWebSocketServer::handleWriteRegistryFrame(
    const MyBusHeader& hdr,
    const std::vector<uint8_t>& payload)
{
    uint16_t regAddr = 0;
    const uint8_t* value = nullptr;
    size_t valueLen = 0;

    if (!mybus_parseRegistryPayload(payload.data(), payload.size(), regAddr, &value, valueLen) ||
        valueLen == 0) {
        sendError(hdr.command, hdr.requestNumber, mybus_proto::REASON_BAD_REQUEST);
        return;
    }

    Serial.printf("[WS] 📝 WRITE_REGISTRY: 0x%04X (%u bytes)\n",
                  regAddr, static_cast<unsigned>(valueLen));

    const uint8_t busDeviceId = 1;

    // ✅ باینری: مستقیم به AppController بدون JSON
    if (binaryFrameCallback_) {
        binaryFrameCallback_(hdr, payload);
    }

    // ✅ رجیستر لوکال؟
    if (shouldSkipMybusWriteCallback_ && shouldSkipMybusWriteCallback_(regAddr)) {
        Serial.printf("[WS] ✅ Local register 0x%04X handled without mYBUS forwarding\n", regAddr);
        const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
        sendControlFrame(mybus_proto::COMMAND_WRITE_REGISTRY, flags, hdr.requestNumber,
                          payload.data(), 2);
        return;
    }

    // ✅ غیرلوکال -> mYBUS باینری (بدون JSON)
    JsonDocument response;
    const bool sent = mybus_.sendRegistryFrame(
        regAddr, value, valueLen, /*isWrite=*/true, busDeviceId, nextRequestNumber(), &response);

    const bool ok = sent && (response["success"] | false);

    if (ok) {
        Serial.println("[WS] ✅ WRITE_REGISTRY successful");
        const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
        sendControlFrame(mybus_proto::COMMAND_WRITE_REGISTRY, flags, hdr.requestNumber,
                          payload.data(), 2);
    } else {
        Serial.println("[WS] ❌ WRITE_REGISTRY failed");
        const uint8_t reason = !sent ? mybus_proto::REASON_TRANSPORT_ERROR
                                      : mybus_proto::REASON_BACKEND_ERROR;
        sendError(hdr.command, hdr.requestNumber, reason, regAddr);
    }
}


// ============================================================
// STATUS
// ============================================================
void CloudWebSocketServer::handleGetStatus(uint16_t requestNumber)
{
    std::vector<uint8_t> payload;
    ByteWriter w(payload);

    w.u32(millis() / 1000);
    w.u32(ESP.getFreeHeap());
    w.i8(static_cast<int8_t>(WiFi.RSSI()));
    w.str8(siteId_);
    w.str8(deviceId_);

    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
    sendControlFrame(mybus_proto::COMMAND_WS_STATUS, flags, requestNumber,
                      payload.data(), payload.size());
}


// ============================================================
// USERS_LIST
// ============================================================
void CloudWebSocketServer::handleGetUsers(uint16_t requestNumber)
{
    std::vector<UserInfo> users;

    if (!storage_.loadUsers(users)) {
        sendError(mybus_proto::COMMAND_WS_USERS_LIST, requestNumber, mybus_proto::REASON_NOT_FOUND);
        return;
    }

    std::vector<uint8_t> payload;
    ByteWriter w(payload);

    const uint8_t count = static_cast<uint8_t>(min(users.size(), static_cast<size_t>(255)));
    w.u8(count);

    for (uint8_t i = 0; i < count; ++i) {
        const auto& user = users[i];
        w.str8(user.username);
        w.str8(user.role);
        w.u32(user.lastLogin);
    }

    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
    sendControlFrame(mybus_proto::COMMAND_WS_USERS_LIST, flags, requestNumber,
                      payload.data(), payload.size());
}


// ============================================================
// SITE_INFO
// ============================================================
void CloudWebSocketServer::handleGetSiteInfo(uint16_t requestNumber)
{
    SiteInfo info;

    if (!storage_.loadSiteInfo(info)) {
        sendError(mybus_proto::COMMAND_WS_SITE_INFO, requestNumber, mybus_proto::REASON_NOT_FOUND);
        return;
    }

    std::vector<uint8_t> payload;
    ByteWriter w(payload);

    w.str8(info.siteId);
    w.str8(info.siteName);
    w.str8(info.licenseKey);
    w.u32(info.expiryDate);
    w.u16(static_cast<uint16_t>(info.maxUsers));

    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
    sendControlFrame(mybus_proto::COMMAND_WS_SITE_INFO, flags, requestNumber,
                      payload.data(), payload.size());
}