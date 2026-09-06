#ifndef SESSION_HPP
#define SESSION_HPP

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstdint>

class AsyncWebSocket;   // forward declare

struct Session {
    int clientId;
    std::array<uint8_t,65> pwaPubRaw;
    std::array<uint8_t,32> hmacKey;
    bool authenticated;
    uint64_t lastActiveMs;
    std::unordered_set<std::string> nonceCache;
};

class SessionManager {
public:
    bool add(int clientId);
    void remove(int clientId);
    Session* get(int clientId);
    void touch(int clientId);
    void expireIdle(uint64_t nowMs, uint64_t timeoutMs);
    void setAuthenticated(int clientId, bool ok);

    // New: broadcast to all authenticated sessions
    void broadcast(AsyncWebSocket& ws, const std::string& text);

    bool checkAndStoreNonce(int clientId, const std::string& nonceB64, size_t maxCacheSize = 32);

private:
    std::unordered_map<int, Session> sessions;
};
#endif // SESSION_HPP
