#include "LocalWsController.h"

#include <Arduino.h>
#include <string.h>

#include "CloudStorage.h"
#include "crypto.hpp"
#include "Logging.h"

static const char* TAG = "LOCAL-WS";

LocalWsController::LocalWsController()
{
}

// ============================================================
// Magic detection
// ============================================================

bool LocalWsController::isLocalFrame(const uint8_t* data, size_t len)
{
    return data != nullptr
        && len >= 3
        && data[0] == LOCAL_MAGIC_0
        && data[1] == LOCAL_MAGIC_1;
}

// ============================================================
// Client lifecycle
// ============================================================

void LocalWsController::onClientConnected(AsyncWebSocketClient* client)
{
    if (client == nullptr) return;

    ClientState state;
    state.isLocal        = false;
    state.authenticated  = false;
    state.authDeadlineMs = millis() + AUTH_TIMEOUT_MS;
    state.frameCount     = 0;
    state.windowStartMs  = millis();

    clients_[client->id()] = state;

    ECOSMART_LOGI(TAG, "Client %u connected", static_cast<unsigned>(client->id()));
}

void LocalWsController::onClientDisconnected(AsyncWebSocketClient* client)
{
    if (client == nullptr) return;

    const auto it = clients_.find(client->id());
    if (it != clients_.end()) {
        clients_.erase(it);
    }

    ECOSMART_LOGI(TAG, "Client %u disconnected", static_cast<unsigned>(client->id()));
}

void LocalWsController::loop()
{
    const uint32_t now = millis();

    for (auto it = clients_.begin(); it != clients_.end(); ) {
        ClientState& state = it->second;

        if (!state.authenticated && now > state.authDeadlineMs) {
            ECOSMART_LOGW(TAG, "Client %u auth timeout",
                          static_cast<unsigned>(it->first));
            it = clients_.erase(it);
            continue;
        }

        ++it;
    }
}

// ============================================================
// Rate limit
// ============================================================

bool LocalWsController::checkRateLimit(ClientState& state)
{
    const uint32_t now = millis();

    if (now - state.windowStartMs >= RATE_WINDOW_MS) {
        state.windowStartMs = now;
        state.frameCount    = 0;
    }

    state.frameCount++;

    return state.frameCount <= RATE_MAX_FRAMES;
}

// ============================================================
// Credential verification
// ============================================================

bool LocalWsController::verifyCredentials(
    const String& user,
    const String& pass)
{
    if (user.isEmpty() || pass.isEmpty()) {
        return false;
    }

    CloudStorage storage;
    std::vector<UserInfo> users;

    if (!storage.loadUsers(users)) {
        ECOSMART_LOGE(TAG, "Failed to load users.json");
        return false;
    }

    for (const auto& u : users) {
        if (u.username == user) {
            const bool ok = cryptoVerifyPassword(pass, u.passwordHash);
            if (ok) {
                ECOSMART_LOGI(TAG, "Auth OK for '%s'", user.c_str());
            } else {
                ECOSMART_LOGW(TAG, "Wrong password for '%s'", user.c_str());
            }
            return ok;
        }
    }

    ECOSMART_LOGW(TAG, "Unknown user '%s'", user.c_str());
    return false;
}

// ============================================================
// Frame dispatch
// ============================================================

LocalWsController::HandleResult LocalWsController::tryHandle(
    AsyncWebSocketClient* client,
    const uint8_t* data,
    size_t len,
    String& outResponse)
{
    outResponse = "";

    if (client == nullptr || !isLocalFrame(data, len)) {
        return HandleResult::NOT_LOCAL;
    }

    const auto it = clients_.find(client->id());
    if (it == clients_.end()) {
        ECOSMART_LOGW(TAG, "Unknown client %u",
                      static_cast<unsigned>(client->id()));
        return HandleResult::AUTH_REQUIRED;
    }

    ClientState& state = it->second;

    if (!checkRateLimit(state)) {
        ECOSMART_LOGW(TAG, "Rate limit exceeded for client %u",
                      static_cast<unsigned>(client->id()));

        JsonDocument resp;
        resp["ok"] = false;
        resp["error"] = "Rate limit exceeded";
        serializeJson(resp, outResponse);
        return HandleResult::RATE_LIMITED;
    }

    const uint8_t command = data[2];
    const char* jsonStart = reinterpret_cast<const char*>(data + 3);
    const size_t jsonLen  = len - 3;

    JsonDocument req;
    if (jsonLen > 0) {
        const DeserializationError err =
            deserializeJson(req, jsonStart, jsonLen);
        if (err != DeserializationError::Ok) {
            JsonDocument resp;
            resp["ok"] = false;
            resp["error"] = String("Invalid JSON: ") + err.c_str();
            serializeJson(resp, outResponse);
            return HandleResult::HANDLED;
        }
    }

    if (command == CMD_AUTH) {
        state.isLocal = true;

        JsonDocument resp;
        handleAuth(state, req, resp);
        serializeJson(resp, outResponse);
        return HandleResult::HANDLED;
    }

    if (!state.authenticated) {
        ECOSMART_LOGW(TAG, "Client %u not authenticated",
                      static_cast<unsigned>(client->id()));

        JsonDocument resp;
        resp["ok"] = false;
        resp["error"] = "AUTH required";
        serializeJson(resp, outResponse);
        return HandleResult::AUTH_REQUIRED;
    }

    JsonDocument resp;

    switch (command) {
        case CMD_WRITE_REGISTRY:
            handleWrite(req, resp);
            break;

        case CMD_READ_REGISTRY:
            handleRead(req, resp);
            break;

        default: {
            resp["ok"] = false;
            resp["error"] = String("Unknown command: ") + String(command);
            break;
        }
    }

    serializeJson(resp, outResponse);
    return HandleResult::HANDLED;
}

// ============================================================
// AUTH
// ============================================================

void LocalWsController::handleAuth(
    ClientState& state,
    JsonDocument& req,
    JsonDocument& resp)
{
    if (!req["username"].is<const char*>() || !req["password"].is<const char*>()) {
        resp["ok"] = false;
        resp["error"] = "Missing username or password";
        state.authenticated = false;
        return;
    }

    const String user = req["username"].as<String>();
    const String pass = req["password"].as<String>();

    const bool ok = verifyCredentials(user, pass);

    state.authenticated = ok;

    resp["ok"] = ok;
    resp["authenticated"] = ok;

    if (!ok) {
        resp["error"] = "Invalid credentials";
    }

    if (req["id"].is<uint32_t>()) {
        resp["id"] = req["id"].as<uint32_t>();
    }
}

// ============================================================
// READ_REGISTRY
// ============================================================

void LocalWsController::handleRead(JsonDocument& req, JsonDocument& resp)
{
    if (!readCb_) {
        resp["ok"] = false;
        resp["error"] = "Read callback not registered";
        return;
    }

    if (!req["addr"].is<uint16_t>()) {
        resp["ok"] = false;
        resp["error"] = "Missing or invalid 'addr'";
        return;
    }

    const uint16_t addr = req["addr"].as<uint16_t>();
    String value;
    const bool ok = readCb_(addr, value);

    resp["ok"] = ok;
    resp["addr"] = addr;
    if (ok) {
        resp["value"] = value;
    } else {
        resp["error"] = "Register not found";
    }

    if (req["id"].is<uint32_t>()) {
        resp["id"] = req["id"].as<uint32_t>();
    }
}

// ============================================================
// WRITE_REGISTRY
// ============================================================

void LocalWsController::handleWrite(JsonDocument& req, JsonDocument& resp)
{
    if (!writeCb_) {
        resp["ok"] = false;
        resp["error"] = "Write callback not registered";
        return;
    }

    if (!req["addr"].is<uint16_t>()) {
        resp["ok"] = false;
        resp["error"] = "Missing or invalid 'addr'";
        return;
    }

    const uint16_t addr = req["addr"].as<uint16_t>();

    String value;
    if (req["val"].is<const char*>()) {
        value = req["val"].as<String>();
    } else if (req["val"].is<int>()) {
        value = String(req["val"].as<int>());
    } else if (req["val"].is<bool>()) {
        value = req["val"].as<bool>() ? "1" : "0";
    } else {
        resp["ok"] = false;
        resp["error"] = "Missing or invalid 'val'";
        return;
    }

    const bool ok = writeCb_(addr, value);

    resp["ok"] = ok;
    resp["addr"] = addr;
    resp["applied"] = value;

    if (!ok) {
        resp["error"] = "Register not found or write rejected";
    }

    if (req["id"].is<uint32_t>()) {
        resp["id"] = req["id"].as<uint32_t>();
    }
}