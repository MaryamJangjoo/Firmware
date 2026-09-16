#ifndef PLAINTEXT_TOKEN_STORE_H
#define PLAINTEXT_TOKEN_STORE_H

#include <Arduino.h>
#include <stdint.h>

// ============================================================
// PlaintextTokenStore
//
// In-memory store for plaintext-channel authentication tokens.
// Tokens are 4 bytes (32 bits) and are issued by the
// /auth/login HTTP endpoint after successful credential
// verification against /users.json.
//
// Design notes:
//   - Tokens live only in RAM. After a reboot, all tokens are
//     invalid and clients must re-authenticate.
//   - Up to MAX_TOKENS tokens can be active at once.
//   - Each token has its own expiry timestamp in millis().
//   - This is NOT cryptographically strong, but combined with
//     the ESP32's limited compute power and the rate limit in
//     CloudWebSocketServer it prevents casual unauthorized
//     access on the LAN.
// ============================================================

class PlaintextTokenStore {
public:
    static constexpr size_t   TOKEN_LENGTH    = 4;
    static constexpr size_t   MAX_TOKENS      = 8;
    static constexpr uint32_t DEFAULT_TTL_MS  = 60UL * 60UL * 1000UL;  // 1 hour

    PlaintextTokenStore();

    bool issue(uint8_t outToken[TOKEN_LENGTH],
               uint32_t ttlMs = DEFAULT_TTL_MS);

    bool validate(const uint8_t token[TOKEN_LENGTH]);

    void purgeExpired();

private:
    struct Entry {
        bool      inUse       = false;
        uint8_t   bytes[TOKEN_LENGTH] = {0};
        uint32_t  expiresAtMs = 0;
    };

    Entry entries_[MAX_TOKENS];
};

#endif // PLAINTEXT_TOKEN_STORE_H