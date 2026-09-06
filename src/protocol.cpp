#include "protocol.hpp"
#include <ArduinoJson.h>
#include <Arduino.h>

// Build JSON messages

bool buildDevicePubkey(const std::string& curve,
                       const std::string& pubB64,
                       std::string& outJson) {
    JsonDocument doc;
    doc["type"] = "device_pubkey";
    doc["curve"] = curve;
    doc["pub"] = pubB64;
    serializeJson(doc, outJson);
    return true;
}

bool buildReadyForKdfAck(std::string& outJson) {
    JsonDocument doc;
    doc["type"] = "ready_for_kdf_ack";
    serializeJson(doc, outJson);
    return true;
}

bool buildChallengeReply(const std::string& nonceB64,
                         const std::string& hmacB64,
                         std::string& outJson) {
    JsonDocument doc;
    doc["type"] = "challenge_reply";
    doc["nonce"] = nonceB64;
    doc["hmac"] = hmacB64;
    serializeJson(doc, outJson);
    return true;
}

// Parse JSON messages

bool parseHello(const std::string& json,
                std::string& proto,
                std::string& curve,
                int& version) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    proto = doc["proto"] | "";
    curve = doc["curve"] | "";
    version = doc["version"] | 1;
    return true;
}

bool parsePwaPubkey(const std::string& json,
                    std::string& curve,
                    std::string& pubRawB64,
                    std::string& saltB64,
                    std::string& infoB64) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    curve = doc["curve"] | "";
    pubRawB64 = doc["pubRaw"] | "";
    saltB64 = doc["hkdfSalt"] | "";
    infoB64 = doc["hkdfInfo"] | "";
    return true;
}
