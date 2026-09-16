#include "CloudWebSocketServer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <vector>
#include <LittleFS.h>
#include "mybus_frame.h"
#include "mybus_registry.h"
#include "mybus_protocol_constants.h"
#include "mybus_value_codec.h"
#include "CloudStorage.h"
#include "RegisterRawValue.h"
#include "LocalWsController.h"
#include "PlaintextTokenStore.h"
#include "crypto.hpp"

namespace {
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

// True if the frame looks like a plaintext mYBUS frame:
//   - first byte == MYBUS_PROTOCOL_VERSION (0x02)
//   - minimum length is at least the header + CRC
//   - security byte at offset 13 is 0 (no AES-GCM)
bool isPlaintextMybusFrame(const uint8_t* data, size_t len)
{
    if (data == nullptr || len < MYBUS_MIN_FRAME_SIZE) {
        return false;
    }
    if (data[0] != MYBUS_PROTOCOL_VERSION) {
        return false;
    }
    if (data[13] != 0x00) {
        return false;
    }
    return true;
}

} // namespace


// ============================================================
// Global POST body capture buffer
//
// ESPAsyncWebServer's onRequestBody callback does not carry a
// request pointer in this build, so we stash incoming body
// chunks in a global String and read them from the handler.
//
// Safe because HTTP requests on this device are serialized:
// only one POST body can be in flight at a time.
// ============================================================
static String g_pendingPostBody;
static bool   g_pendingPostBodyActive = false;


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
    if (wsFrontend_ != nullptr) {
        wsFrontend_->closeAll();
        delete wsFrontend_;
        wsFrontend_ = nullptr;
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


    // ============================================================
    // Backend channel: /ws (encrypted mYBUS)
    // ============================================================
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

    // ============================================================
    // Frontend channel: /ws/frontend (plaintext + token)
    // ============================================================
    wsFrontend_ = new AsyncWebSocket("/ws/frontend");

    wsFrontend_->onEvent(
        [this](
            AsyncWebSocket* server,
            AsyncWebSocketClient* client,
            AwsEventType type,
            void* arg,
            uint8_t* data,
            size_t len
        ) {
            this->onFrontendEvent(server, client, type, arg, data, len);
        }
    );

    server_->addHandler(wsFrontend_);

    // Wire the local (plaintext) WS read/write callbacks to the
    // same registry controllers used by the encrypted WS path.
    localWs_.onRegistryRead([this](uint16_t addr, String& out) {
        if (!localReadCallback_) return false;

        RegisterRawValue v;
        if (!localReadCallback_(addr, v)) return false;

        if (v.isString()) {
            out = v.stringValue;
            return true;
        }

        switch (v.type) {
            case RegRawType::BIT:
                out = (v.bytes[0] != 0) ? "1" : "0";
                break;
            case RegRawType::UINT8:
                out = String(v.bytes[0]);
                break;
            case RegRawType::UINT16: {
                uint16_t tmp; memcpy(&tmp, v.bytes, 2);
                out = String(tmp);
                break;
            }
            case RegRawType::UINT32: {
                uint32_t tmp; memcpy(&tmp, v.bytes, 4);
                out = String(tmp);
                break;
            }
            case RegRawType::INT8:
                out = String(static_cast<int8_t>(v.bytes[0]));
                break;
            case RegRawType::INT16: {
                int16_t tmp; memcpy(&tmp, v.bytes, 2);
                out = String(tmp);
                break;
            }
            case RegRawType::INT32: {
                int32_t tmp; memcpy(&tmp, v.bytes, 4);
                out = String(tmp);
                break;
            }
            case RegRawType::FLOAT: {
                float tmp; memcpy(&tmp, v.bytes, 4);
                out = String(tmp, 6);
                break;
            }
            default:
                return false;
        }

        return true;
    });

    localWs_.onRegistryWrite([this](uint16_t addr, const String& val) {
        return localWriteCallback_ ? localWriteCallback_(addr, val) : false;
    });

    static constexpr const char* FIRMWARE_VERSION = "2.0.0";
    static constexpr const char* PART_NUMBER      = "SEC-BLB56001";

    // ============================================================
    // HTTP endpoints
    // ============================================================
    server_->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String host = request->host();

        Serial.printf("[HTTP] 🔍 GET / host='%s'\n", host.c_str());

        if (host.indexOf("ecosmart") >= 0) {
            Serial.println("[HTTP] → mDNS: returning IP");
            request->send(200, "text/plain", WiFi.localIP().toString());
            return;
        }

        Serial.println("[HTTP] → IP: serving index.html");
        if (!handleFileRead(request, "/index.html")) {
            Serial.println("[HTTP] ❌ index.html not found in LittleFS");
            request->send(404, "text/plain",
                "index.html not found. Run: pio run -t uploadfs");
        }
    });

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

    // ------------------------------------------------------------
    // POST /auth/login
    //
    // Registered via AsyncCallbackWebHandler so we can install
    // onBody + onRequest callbacks. The default server_->on()
    // helper does not populate the request body.
    // ------------------------------------------------------------
    {
        AsyncCallbackWebHandler* authHandler = new AsyncCallbackWebHandler();
        authHandler->setUri("/auth/login");
        authHandler->setMethod(HTTP_POST);

        authHandler->onBody([this](
            AsyncWebServerRequest* request,
            uint8_t* data,
            size_t len,
            size_t index,
            size_t total)
        {
            (void)request;

            if (index == 0) {
                g_pendingPostBody = "";
                g_pendingPostBody.reserve(total + 1);
                g_pendingPostBodyActive = true;
            }

            if (g_pendingPostBodyActive) {
                for (size_t i = 0; i < len; ++i) {
                    g_pendingPostBody += static_cast<char>(data[i]);
                }
            }
        });

        authHandler->onRequest([this](AsyncWebServerRequest* request) {
            this->handleAuthLogin(request);
        });

        server_->addHandler(authHandler);
    }

    server_->onNotFound([this](AsyncWebServerRequest* request) {
        this->handleNotFound(request);
    });

    server_->begin();

    Serial.println("[WS] HTTP server started on port 80");
    Serial.println("[WS]   GET  /             (device info / index.html)");
    Serial.println("[WS]   GET  /info         (device status JSON)");
    Serial.println("[WS]   POST /auth/login   (frontend auth)");
    Serial.println("[WS] WS endpoint: /ws           (backend, encrypted)");
    Serial.println("[WS] WS endpoint: /ws/frontend  (frontend, plaintext)");
}

void CloudWebSocketServer::loop()
{
    if (ws_ != nullptr) ws_->cleanupClients();
    if (wsFrontend_ != nullptr) wsFrontend_->cleanupClients();
    localWs_.loop();
    tokenStore_.purgeExpired();
}

bool CloudWebSocketServer::isConnected() const
{
    return connected_ && client_ != nullptr;
}


// ============================================================
// Backend channel (/ws) - unchanged behavior
// ============================================================

bool CloudWebSocketServer::sendControlFrame(
    uint8_t command,
    uint8_t flags,
    uint16_t requestNumber,
    const uint8_t* payload,
    size_t payloadLen)
{
    if (!connected_ || client_ == nullptr) {
        return false;
    }

    std::vector<uint8_t> wire;

    if (!mybus_.buildControlFrame(command, flags, requestNumber, payload, payloadLen, wire)) {
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


// ============================================================
// Backend channel event handler (encrypted)
// ============================================================

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
            Serial.printf("[WS] Backend client connected: %u\n", client->id());

            client_ = client;
            connected_ = true;

            if (mybus_.isSessionEstablished()) {
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
            } else {
                Serial.println("[WS] Skipping welcome: no secure session");
            }

            break;
        }

        case WS_EVT_DISCONNECT: {
            Serial.printf("[WS] Backend client disconnected: %u\n", client->id());
            if (client_ == client) {
                client_ = nullptr;
                connected_ = false;
            }
            break;
        }

        case WS_EVT_DATA: {
            AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);

            if (info == nullptr || data == nullptr) break;

            if (!info->final || info->index != 0 || info->len != len
                || info->opcode != WS_BINARY) {
                Serial.println("[WS] ⚠️ Ignoring fragmented/non-binary frame");
                break;
            }

            handleBinaryMessage(arg, data, len);
            break;
        }

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}


// ============================================================
// Frontend channel event handler (plaintext)
// ============================================================

void CloudWebSocketServer::onFrontendEvent(
    AsyncWebSocket* server,
    AsyncWebSocketClient* client,
    AwsEventType type,
    void* arg,
    uint8_t* data,
    size_t len)
{
    switch (type) {

        case WS_EVT_CONNECT:
            Serial.printf("[WS-FRONTEND] Client connected: %u\n", client->id());
            localWs_.onClientConnected(client);
            break;

        case WS_EVT_DISCONNECT:
            Serial.printf("[WS-FRONTEND] Client disconnected: %u\n", client->id());
            localWs_.onClientDisconnected(client);
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);

            if (info == nullptr || data == nullptr) break;

            if (!info->final || info->index != 0 || info->len != len
                || info->opcode != WS_BINARY) {
                Serial.println("[WS-FRONTEND] ⚠️ Ignoring fragmented/non-binary frame");
                break;
            }

            // Path 1: Local JSON frame (magic [0x4C, 0x4F])
            if (LocalWsController::isLocalFrame(data, len)) {
                Serial.printf("[WS-FRONTEND] 📥 Local frame (%u bytes)\n",
                              static_cast<unsigned>(len));

                String response;
                const auto result = localWs_.tryHandle(client, data, len, response);

                if (result == LocalWsController::HandleResult::HANDLED
                    && client != nullptr
                    && !response.isEmpty()) {
                    if (!client->text(response)) {
                        Serial.println("[WS-FRONTEND] ❌ Failed to send response");
                    }
                }
                break;
            }

            // Path 2: Plaintext mYBUS frame (protocol=0x02, security=0x00)
            if (isPlaintextMybusFrame(data, len)) {
                Serial.printf("[WS-FRONTEND] 📥 Plaintext mYBUS frame (%u bytes)\n",
                              static_cast<unsigned>(len));
                handlePlaintextMybusFrame(client, data, len);
                break;
            }

            Serial.println("[WS-FRONTEND] ❌ Unknown frame type");
            break;
        }

        default:
            break;
    }
}


// ============================================================
// Encrypted backend frame handling (unchanged)
// ============================================================

void CloudWebSocketServer::handleBinaryMessage(void* arg, uint8_t* data, size_t len)
{
    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);

    if (info == nullptr || data == nullptr) return;

    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_BINARY) {
        Serial.println("[WS] ⚠️ Ignoring fragmented/non-binary frame");
        return;
    }

    Serial.printf("[WS] 📥 Encrypted frame (%u bytes)\n", static_cast<unsigned>(len));

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
// Plaintext mYBUS frame handling (frontend channel)
// ============================================================

void CloudWebSocketServer::handlePlaintextMybusFrame(
    AsyncWebSocketClient* client,
    const uint8_t* data,
    size_t len)
{
    if (client == nullptr || data == nullptr) return;

    MyBusHeader hdr;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;

    if (!mybus_parseFrame(data, len, hdr, &payload, &payloadLen)) {
        Serial.println("[PLAINTEXT-MYBUS] ❌ Frame parse failed (bad CRC or length)");
        // Can't send error because we don't have a valid header to
        // echo the command from. Best we can do is stay silent.
        return;
    }

    Serial.printf("[PLAINTEXT-MYBUS] cmd=%u iface=%u zone=%u devId=%u req=%u flags=0x%02X security=0x%02X\n",
                  hdr.command, hdr.interfaceId, hdr.zone,
                  hdr.deviceId, hdr.requestNumber,
                  hdr.flags, hdr.security);

    dispatchPlaintextFrame(client, hdr, payload, payloadLen);
}

void CloudWebSocketServer::dispatchPlaintextFrame(
    AsyncWebSocketClient* client,
    const MyBusHeader& hdr,
    const uint8_t* payload,
    size_t payloadLen)
{
    if (payloadLen < PlaintextTokenStore::TOKEN_LENGTH) {
        Serial.println("[PLAINTEXT-MYBUS] ❌ Payload too short for token");
        sendPlaintextError(client, hdr.command, hdr.requestNumber,
                           mybus_proto::REASON_BAD_REQUEST);
        return;
    }

    const uint8_t* token = payload;
    const uint8_t* framePayload = payload + PlaintextTokenStore::TOKEN_LENGTH;
    const size_t framePayloadLen = payloadLen - PlaintextTokenStore::TOKEN_LENGTH;

    if (!tokenStore_.validate(token)) {
        Serial.printf("[PLAINTEXT-MYBUS] ❌ Invalid token %02X%02X%02X%02X\n",
                      token[0], token[1], token[2], token[3]);
        sendPlaintextError(client, hdr.command, hdr.requestNumber,
                           mybus_proto::REASON_BAD_REQUEST);
        return;
    }

    Serial.printf("[PLAINTEXT-MYBUS] ✅ Token OK (%02X%02X%02X%02X)\n",
                  token[0], token[1], token[2], token[3]);

    switch (hdr.command) {

        case mybus_proto::COMMAND_WRITE_REGISTRY: {
            if (framePayloadLen < 3) {
                Serial.println("[PLAINTEXT-MYBUS] WRITE payload too short");
                sendPlaintextError(client, hdr.command, hdr.requestNumber,
                                   mybus_proto::REASON_BAD_REQUEST);
                return;
            }

            const uint16_t regAddr =
                static_cast<uint16_t>(framePayload[0] | (framePayload[1] << 8));
            const uint8_t* value = framePayload + 2;
            const size_t valueLen = framePayloadLen - 2;

            String regVal;
            if (valueLen == 1) {
                regVal = String(value[0]);
            } else if (valueLen == 2) {
                uint16_t v; memcpy(&v, value, 2);
                regVal = String(v);
            } else if (valueLen == 4) {
                uint32_t v; memcpy(&v, value, 4);
                regVal = String(v);
            } else {
                regVal.reserve(valueLen + 1);
                for (size_t i = 0; i < valueLen; ++i) {
                    regVal += static_cast<char>(value[i]);
                }
            }

            Serial.printf("[PLAINTEXT-MYBUS] WRITE 0x%04X = '%s'\n",
                          regAddr, regVal.c_str());

            const bool handled =
                (localWriteCallback_ && localWriteCallback_(regAddr, regVal));

            if (handled) {
                Serial.println("[PLAINTEXT-MYBUS] Handled locally");
            } else {
                Serial.println("[PLAINTEXT-MYBUS] Register not found");
                sendPlaintextError(client, hdr.command, hdr.requestNumber,
                                   mybus_proto::REASON_NOT_FOUND, regAddr);
                return;
            }

            const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
            sendPlaintextFrame(
                client,
                mybus_proto::COMMAND_WRITE_REGISTRY,
                flags,
                hdr.requestNumber,
                framePayload,
                2);
            break;
        }

        case mybus_proto::COMMAND_READ_REGISTRY: {
            if (framePayloadLen < 2) {
                Serial.println("[PLAINTEXT-MYBUS] READ payload too short");
                sendPlaintextError(client, hdr.command, hdr.requestNumber,
                                   mybus_proto::REASON_BAD_REQUEST);
                return;
            }

            const uint16_t regAddr =
                static_cast<uint16_t>(framePayload[0] | (framePayload[1] << 8));

            Serial.printf("[PLAINTEXT-MYBUS] READ 0x%04X\n", regAddr);

            RegisterRawValue rv;
            if (localReadCallback_ && localReadCallback_(regAddr, rv)) {
                std::vector<uint8_t> respPayload(2 + rv.byteLen);
                respPayload[0] = static_cast<uint8_t>(regAddr & 0xFF);
                respPayload[1] = static_cast<uint8_t>((regAddr >> 8) & 0xFF);
                if (rv.byteLen > 0) {
                    memcpy(respPayload.data() + 2, rv.bytes, rv.byteLen);
                }

                const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
                sendPlaintextFrame(
                    client,
                    mybus_proto::COMMAND_READ_REGISTRY,
                    flags,
                    hdr.requestNumber,
                    respPayload.data(),
                    respPayload.size());
            } else {
                Serial.println("[PLAINTEXT-MYBUS] Register not found");
                sendPlaintextError(client, hdr.command, hdr.requestNumber,
                                   mybus_proto::REASON_NOT_FOUND, regAddr);
            }
            break;
        }

        default:
            Serial.printf("[PLAINTEXT-MYBUS] Unhandled command: %u\n", hdr.command);
            sendPlaintextError(client, hdr.command, hdr.requestNumber,
                               mybus_proto::REASON_BAD_REQUEST);
            break;
    }
}

bool CloudWebSocketServer::sendPlaintextFrame(
    AsyncWebSocketClient* client,
    uint8_t command,
    uint8_t flags,
    uint16_t requestNumber,
    const uint8_t* payload,
    size_t payloadLen)
{
    if (client == nullptr) return false;

    MyBusHeader hdr;
    hdr.protocolVersion = MYBUS_PROTOCOL_VERSION;
    hdr.length          = 0;
    hdr.sequence        = 0;
    hdr.interfaceId     = mybus_proto::INTERFACE_WIFI;
    hdr.zone            = 0;
    hdr.deviceId        = 0;
    hdr.reserved        = 0;
    hdr.requestNumber   = requestNumber;
    hdr.qos             = 0;
    hdr.options         = 0;
    hdr.flags           = flags;
    hdr.security        = 0;
    hdr.compression     = 0;
    hdr.command         = command;

    const size_t maxFrameSize =
        MYBUS_HEADER_SIZE + MYBUS_MAX_PAYLOAD_SIZE + MYBUS_CRC_SIZE;

    std::vector<uint8_t> frame(maxFrameSize);

    const size_t frameLen = mybus_buildFrame(
        hdr, payload, payloadLen, frame.data(), frame.size());

    if (frameLen == 0) {
        Serial.println("[PLAINTEXT-MYBUS] ❌ Frame build failed");
        return false;
    }

    if (!client->binary(frame.data(), frameLen)) {
        Serial.println("[PLAINTEXT-MYBUS] ❌ Failed to send frame");
        return false;
    }

    Serial.printf("[PLAINTEXT-MYBUS] 📤 Sent response (%u bytes)\n",
                  static_cast<unsigned>(frameLen));

    return true;
}

bool CloudWebSocketServer::sendPlaintextError(
    AsyncWebSocketClient* client,
    uint8_t originalCommand,
    uint16_t requestNumber,
    uint8_t reason,
    uint16_t regAddr)
{
    if (client == nullptr) return false;

    // Error payload: [originalCommand][reason][regAddrLow][regAddrHigh]
    std::vector<uint8_t> payload(4);
    payload[0] = originalCommand;
    payload[1] = reason;
    payload[2] = static_cast<uint8_t>(regAddr & 0xFF);
    payload[3] = static_cast<uint8_t>((regAddr >> 8) & 0xFF);

    // flags = RSP bit (0) | SF bit (2) → 0x05
    const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT) | (1U << MYBUS_FLAG_SF_BIT);

    const bool ok = sendPlaintextFrame(
        client,
        originalCommand,
        flags,
        requestNumber,
        payload.data(),
        payload.size()
    );

    Serial.printf("[PLAINTEXT-MYBUS] ⚠️ Error response: cmd=0x%02X reason=0x%02X reg=0x%04X\n",
                  originalCommand, reason, regAddr);

    return ok;
}


// ============================================================
// HTTP /auth/login
// ============================================================

void CloudWebSocketServer::handleAuthLogin(AsyncWebServerRequest* request)
{
    if (request == nullptr) return;

    if (!g_pendingPostBodyActive) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(400);
        response->print("{\"ok\":false,\"error\":\"Missing body\"}");
        request->send(response);
        return;
    }

    const String body = g_pendingPostBody;
    g_pendingPostBody = "";
    g_pendingPostBodyActive = false;

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, body);

    if (err != DeserializationError::Ok) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(400);
        response->print("{\"ok\":false,\"error\":\"Invalid JSON\"}");
        request->send(response);
        return;
    }

    const String username = doc["username"] | "";
    const String password = doc["password"] | "";

    if (username.isEmpty() || password.isEmpty()) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(400);
        response->print("{\"ok\":false,\"error\":\"Missing username or password\"}");
        request->send(response);
        return;
    }

    std::vector<UserInfo> users;
    if (!storage_.loadUsers(users)) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(500);
        response->print("{\"ok\":false,\"error\":\"Failed to load users\"}");
        request->send(response);
        return;
    }

    bool authenticated = false;
    for (const auto& u : users) {
        if (u.username == username) {
            authenticated = cryptoVerifyPassword(password, u.passwordHash);
            break;
        }
    }

    if (!authenticated) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(401);
        response->print("{\"ok\":false,\"error\":\"Invalid credentials\"}");
        request->send(response);
        return;
    }

    uint8_t tokenBytes[PlaintextTokenStore::TOKEN_LENGTH] = {0};
    if (!tokenStore_.issue(tokenBytes, PlaintextTokenStore::DEFAULT_TTL_MS)) {
        AsyncResponseStream* response =
            request->beginResponseStream("application/json");
        response->setCode(500);
        response->print("{\"ok\":false,\"error\":\"Token generation failed\"}");
        request->send(response);
        return;
    }

    char tokenHex[PlaintextTokenStore::TOKEN_LENGTH * 2 + 1];
    for (size_t i = 0; i < PlaintextTokenStore::TOKEN_LENGTH; ++i) {
        snprintf(tokenHex + i * 2, 3, "%02x", tokenBytes[i]);
    }
    tokenHex[PlaintextTokenStore::TOKEN_LENGTH * 2] = '\0';

    char responseBuf[128];
    snprintf(responseBuf, sizeof(responseBuf),
             "{\"ok\":true,\"token\":\"%s\",\"expiresIn\":%lu}",
             tokenHex,
             static_cast<unsigned long>(PlaintextTokenStore::DEFAULT_TTL_MS / 1000));

    AsyncResponseStream* response =
        request->beginResponseStream("application/json");
    response->setCode(200);
    response->print(responseBuf);
    request->send(response);

    Serial.printf("[AUTH] ✅ Issued token for '%s'\n", username.c_str());
}


// ============================================================
// Encrypted backend frame dispatch (unchanged)
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

    if (binaryFrameCallback_) {
        binaryFrameCallback_(hdr, payload);
    }

    if (shouldSkipMybusWriteCallback_ && shouldSkipMybusWriteCallback_(regAddr)) {
        Serial.printf("[WS] ✅ Local register 0x%04X handled without mYBUS forwarding\n", regAddr);
        const uint8_t flags = (1U << MYBUS_FLAG_RSP_BIT);
        sendControlFrame(mybus_proto::COMMAND_WRITE_REGISTRY, flags, hdr.requestNumber,
                          payload.data(), 2);
        return;
    }

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

String CloudWebSocketServer::getContentType(const String& path)
{
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".xml")) return "text/xml";
    if (path.endsWith(".pdf")) return "application/pdf";
    if (path.endsWith(".zip")) return "application/zip";
    if (path.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool CloudWebSocketServer::handleFileRead(AsyncWebServerRequest* request, String path)
{
    if (path.endsWith("/")) {
        path += "index.html";
    }

    const String contentType = getContentType(path);

    if (!LittleFS.exists(path)) {
        return false;
    }

    request->send(LittleFS, path, contentType);
    return true;
}

void CloudWebSocketServer::handleNotFound(AsyncWebServerRequest* request)
{
    // Free any captured body to avoid leaks for unmatched routes.
    if (g_pendingPostBodyActive) {
        g_pendingPostBody = "";
        g_pendingPostBodyActive = false;
    }

    Serial.printf("[HTTP] ⚠️ Not Found: %s %s\n",
                  request->methodToString(),
                  request->url().c_str());

    if (request->method() == HTTP_GET && handleFileRead(request, request->url())) {
        return;
    }

    AsyncResponseStream* response =
        request->beginResponseStream("application/json");
    response->setCode(404);

    JsonDocument doc;
    doc["error"] = "Not Found";
    doc["path"] = request->url();
    doc["method"] = request->methodToString();

    serializeJson(doc, *response);
    request->send(response);
}

// ============================================================
// Local WS callback setters
// ============================================================

void CloudWebSocketServer::setLocalRegistryRead(
    CloudWebSocketServer::LocalRegistryReadCallback cb)
{
    localReadCallback_ = cb;
}

void CloudWebSocketServer::setLocalRegistryWrite(
    CloudWebSocketServer::LocalRegistryWriteCallback cb)
{
    localWriteCallback_ = cb;
}