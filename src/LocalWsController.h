#ifndef LOCAL_WS_CONTROLLER_H
#define LOCAL_WS_CONTROLLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <map>
#include <functional>

// ============================================================
// LocalWsController
//
// Handles plaintext (unencrypted) control frames on the same
// /ws WebSocket used by the encrypted backend channel.
//
// Wire format:
//   Byte 0: 0x4C ('L')  - magic 1
//   Byte 1: 0x4F ('O')  - magic 2
//   Byte 2: command
//   Byte 3..N: JSON payload (UTF-8)
//
// Commands:
//   0x10  AUTH            - must be the first frame
//   0x01  WRITE_REGISTRY  - write a registry value
//   0x02  READ_REGISTRY   - read a registry value
//
// Auth policy:
//   - Client must send AUTH within 30 seconds of connecting.
//   - All other frames are rejected until AUTH succeeds.
//   - Credentials are verified against /users.json on LittleFS
//     using the same PBKDF2 verification used by CloudStorage.
//
// Rate limit:
//   - 10 frames per second per client.
//
// Multi-client:
//   - Auth state is tracked per AsyncWebSocketClient::id().
//   - Multiple local clients are supported simultaneously.
// ============================================================

class LocalWsController {
public:
enum class HandleResult {
NOT_LOCAL,       // Not a [LO] frame
HANDLED,         // Local frame handled
AUTH_REQUIRED,   // Local frame received but client not authenticated
RATE_LIMITED,    // Frame dropped due to rate limit
};

using RegistryReadCallback =
std::function<bool(uint16_t addr, String& outValue)>;

using RegistryWriteCallback =
std::function<bool(uint16_t addr, const String& value)>;

LocalWsController();

void onClientConnected(AsyncWebSocketClient* client);
void onClientDisconnected(AsyncWebSocketClient* client);

HandleResult tryHandle(
AsyncWebSocketClient* client,
const uint8_t* data,
size_t len,
String& outResponse);

void loop();

void onRegistryRead(RegistryReadCallback cb) { readCb_ = cb; }
void onRegistryWrite(RegistryWriteCallback cb) { writeCb_ = cb; }

static bool isLocalFrame(const uint8_t* data, size_t len);

private:
struct ClientState {
bool      isLocal       = false;
bool      authenticated = false;
uint32_t  authDeadlineMs = 0;
uint32_t  frameCount    = 0;
uint32_t  windowStartMs = 0;
};

void handleAuth(ClientState& state, JsonDocument& req, JsonDocument& resp);
void handleRead(JsonDocument& req, JsonDocument& resp);
void handleWrite(JsonDocument& req, JsonDocument& resp);

bool verifyCredentials(const String& user, const String& pass);
bool checkRateLimit(ClientState& state);

static constexpr uint8_t LOCAL_MAGIC_0 = 0x4C;  // 'L'
static constexpr uint8_t LOCAL_MAGIC_1 = 0x4F;  // 'O'

static constexpr uint8_t CMD_WRITE_REGISTRY = 0x01;
static constexpr uint8_t CMD_READ_REGISTRY  = 0x02;
static constexpr uint8_t CMD_AUTH           = 0x10;

static constexpr uint32_t AUTH_TIMEOUT_MS = 30000;
static constexpr uint32_t RATE_WINDOW_MS  = 1000;
static constexpr uint32_t RATE_MAX_FRAMES = 10;

std::map<uint32_t, ClientState> clients_;

RegistryReadCallback  readCb_;
RegistryWriteCallback writeCb_;
};

#endif 