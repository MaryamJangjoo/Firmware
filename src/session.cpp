#include "session.hpp"
#include <Arduino.h>   // for millis()
#include <ESPAsyncWebServer.h>
// #include <algorithm>

// Add a new session for a clientId
bool SessionManager::add(int clientId) {
    if (sessions.find(clientId) != sessions.end()) {
        // Already exists
        return false;
    }
    Session s;
    s.clientId = clientId;
    s.authenticated = false;
    s.lastActiveMs = millis();
    sessions[clientId] = s;
    return true;
}

// Remove a session by clientId
void SessionManager::remove(int clientId) {
    sessions.erase(clientId);
}

// Get a pointer to a session (nullptr if not found)
Session* SessionManager::get(int clientId) {
    auto it = sessions.find(clientId);
    if (it == sessions.end()) return nullptr;
    return &it->second;
}

// Update lastActive timestamp
void SessionManager::touch(int clientId) {
    auto it = sessions.find(clientId);
    if (it != sessions.end()) {
        it->second.lastActiveMs = millis();
    }
}

// Expire idle sessions older than timeoutMs
void SessionManager::expireIdle(uint64_t nowMs, uint64_t timeoutMs) {
    for (auto it = sessions.begin(); it != sessions.end(); ) {
        if (nowMs - it->second.lastActiveMs > timeoutMs) {
            Serial.printf("Expiring session for client %d\n", it->first);
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}

// Mark a session as authenticated
void SessionManager::setAuthenticated(int clientId, bool ok) {
    auto it = sessions.find(clientId);
    if (it != sessions.end()) {
        it->second.authenticated = ok;
        it->second.lastActiveMs = millis();
    }
}

// Broadcast a message to all authenticated sessions
void SessionManager::broadcast(AsyncWebSocket& ws, const std::string& text) {
    for (auto& kv : sessions) {
        if (kv.second.authenticated) {
            AsyncWebSocketClient* client = ws.client(kv.first);
            if (client && client->status() == WS_CONNECTED) {
                client->text(text.c_str());
            }
        }
    }
}

// Check and store nonce for replay protection
bool SessionManager::checkAndStoreNonce(int clientId, const std::string& nonceB64, size_t maxCacheSize) {
    auto it = sessions.find(clientId);
    if (it == sessions.end()) return false; // no such session

    Session& s = it->second;

    // If nonce already seen → replay attack
    if (s.nonceCache.find(nonceB64) != s.nonceCache.end()) {
        Serial.printf("Replay detected for client %d, nonce=%s\n", clientId, nonceB64.c_str());
        return false;
    }

    // Otherwise store it
    s.nonceCache.insert(nonceB64);

    // Optional: cap cache size to avoid memory blowup
    if (s.nonceCache.size() > maxCacheSize) {
        // Simple strategy: erase oldest by clearing all (or implement a queue for true LRU)
        s.nonceCache.clear();
        s.nonceCache.insert(nonceB64);
        Serial.printf("Nonce cache pruned for client %d\n", clientId);
    }

    return true;
}