#include "PlaintextTokenStore.h"

#include <Arduino.h>
#include <string.h>

#include "crypto.hpp"

PlaintextTokenStore::PlaintextTokenStore()
{
    memset(entries_, 0, sizeof(entries_));
}

bool PlaintextTokenStore::issue(
    uint8_t outToken[TOKEN_LENGTH],
    uint32_t ttlMs)
{
    if (outToken == nullptr) {
        return false;
    }

    uint8_t randomBytes[TOKEN_LENGTH] = {0};
    if (!cryptoRandomBytes(randomBytes, sizeof(randomBytes))) {
        Serial.println("[TOKEN] ❌ RNG failed");
        return false;
    }

    Entry* slot = nullptr;
    const uint32_t now = millis();

    for (size_t i = 0; i < MAX_TOKENS; ++i) {
        if (!entries_[i].inUse) {
            slot = &entries_[i];
            break;
        }
        if (entries_[i].expiresAtMs <= now && slot == nullptr) {
            slot = &entries_[i];
        }
    }

    if (slot == nullptr) {
        Serial.println("[TOKEN] ❌ No free slot");
        return false;
    }

    memcpy(slot->bytes, randomBytes, TOKEN_LENGTH);
    slot->expiresAtMs = now + ttlMs;
    slot->inUse       = true;

    memcpy(outToken, randomBytes, TOKEN_LENGTH);

    Serial.printf("[TOKEN] ✅ Issued token %02X%02X%02X%02X (TTL %lu ms)\n",
                  randomBytes[0], randomBytes[1],
                  randomBytes[2], randomBytes[3],
                  static_cast<unsigned long>(ttlMs));

    return true;
}

bool PlaintextTokenStore::validate(const uint8_t token[TOKEN_LENGTH])
{
    if (token == nullptr) {
        return false;
    }

    const uint32_t now = millis();

    for (size_t i = 0; i < MAX_TOKENS; ++i) {
        Entry& e = entries_[i];
        if (!e.inUse) {
            continue;
        }
        if (e.expiresAtMs <= now) {
            e.inUse = false;
            continue;
        }
        if (memcmp(e.bytes, token, TOKEN_LENGTH) == 0) {
            return true;
        }
    }

    return false;
}

void PlaintextTokenStore::purgeExpired()
{
    const uint32_t now = millis();
    for (size_t i = 0; i < MAX_TOKENS; ++i) {
        if (entries_[i].inUse && entries_[i].expiresAtMs <= now) {
            entries_[i].inUse = false;
        }
    }
}