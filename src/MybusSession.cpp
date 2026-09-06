#include "MybusSession.h"

#include <Arduino.h>
#include <string.h>
#include <ArduinoJson.h>

#include "crypto.hpp"
#include "mybus_frame.h"
#include "mybus_protocol_constants.h"

// ============================================================
// Constructor
// ============================================================

MybusSession::MybusSession(
    HttpTransport& transport,
    const String& deviceId,
    uint8_t interfaceId,
    uint8_t zone)
    : transport_(transport),
      deviceId_(deviceId),
      interfaceId_(interfaceId),
      zone_(zone)
{
    mbedtls_ecp_keypair_init(&deviceKeypair_);
    memset(sessionKey_, 0, sizeof(sessionKey_));
}

// ============================================================
// Destructor
// ============================================================

MybusSession::~MybusSession()
{
    clear();
    mbedtls_ecp_keypair_free(&deviceKeypair_);
}

// ============================================================
// Device keypair
// ============================================================

bool MybusSession::initializeDeviceKeypair()
{
    if (deviceKeypairInitialized_) {
        return true;
    }

    if (!cryptoInit()) {
        Serial.println("[mYBUS] cryptoInit failed");
        return false;
    }

    if (!loadDeviceKeypair(deviceKeypair_)) {
        Serial.println("[mYBUS] Failed to load/generate device keypair");
        return false;
    }

    deviceKeypairInitialized_ = true;

    devicePublicKeyPem_ = "";

    if (!cryptoExportPublicKeyPem(
            deviceKeypair_,
            devicePublicKeyPem_)) {

        Serial.println("[mYBUS] Public key PEM export failed");

        mbedtls_ecp_keypair_free(&deviceKeypair_);
        mbedtls_ecp_keypair_init(&deviceKeypair_);

        deviceKeypairInitialized_ = false;
        return false;
    }

    devicePrivateKeyPem_ = "";

    if (!cryptoExportPrivateKeyPem(
            deviceKeypair_,
            devicePrivateKeyPem_)) {

        Serial.println("[mYBUS] Private key PEM export failed");

        mbedtls_ecp_keypair_free(&deviceKeypair_);
        mbedtls_ecp_keypair_init(&deviceKeypair_);

        deviceKeypairInitialized_ = false;
        return false;
    }

    if (devicePublicKeyPem_.isEmpty() ||
        devicePrivateKeyPem_.isEmpty()) {

        Serial.println("[mYBUS] PEM export produced empty key");

        mbedtls_ecp_keypair_free(&deviceKeypair_);
        mbedtls_ecp_keypair_init(&deviceKeypair_);

        deviceKeypairInitialized_ = false;
        return false;
    }

    Serial.println("[mYBUS] Device ECDH keypair ready");

    return true;
}

// ============================================================
// Nonce
// ============================================================

String MybusSession::createNonce()
{
    uint8_t randomBytes[32] = {0};

    if (!cryptoRandomBytes(
            randomBytes,
            sizeof(randomBytes))) {

        cryptoSecureZero(
            randomBytes,
            sizeof(randomBytes)
        );

        return "";
    }

    String nonce =
        cryptoBytesToHex(
            randomBytes,
            sizeof(randomBytes)
        );

    cryptoSecureZero(
        randomBytes,
        sizeof(randomBytes)
    );

    return nonce;
}

bool MybusSession::generateHandshakeNonce()
{
    handshakeNonce_ = createNonce();

    Serial.printf(
        "[mYBUS] 🔑 Nonce generated: %s\n",
        handshakeNonce_.c_str()
    );

    return !handshakeNonce_.isEmpty();
}

// ============================================================
// HMAC
// ============================================================

String MybusSession::hmacHex(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* msg,
    size_t msgLen)
{
    uint8_t mac[32] = {0};

    if (!hmacSha256(
            key,
            keyLen,
            msg,
            msgLen,
            mac)) {

        cryptoSecureZero(
            mac,
            sizeof(mac)
        );

        return "";
    }

    String result =
        cryptoBytesToHex(
            mac,
            sizeof(mac)
        );

    cryptoSecureZero(
        mac,
        sizeof(mac)
    );

    return result;
}

bool MybusSession::createChallengeHmac(String& hmacHexOut)
{
    hmacHexOut = "";

    if (!sessionKeyValid_ ||
        handshakeNonce_.isEmpty()) {

        Serial.println(
            "[mYBUS] ❌ Cannot create HMAC: session invalid or nonce empty"
        );

        return false;
    }

    hmacHexOut =
        hmacHex(
            sessionKey_,
            sizeof(sessionKey_),
            reinterpret_cast<const uint8_t*>(
                handshakeNonce_.c_str()),
            handshakeNonce_.length()
        );

    return hmacHexOut.length() == 64;
}

// ============================================================
// Session key
// ============================================================

bool MybusSession::computeSessionKey()
{
    Serial.println(
        "[mYBUS] 🔐 Computing Session Key"
    );

    if (serverPublicKeyPem_.isEmpty()) {
        Serial.println(
            "[mYBUS] ❌ Missing server public key"
        );
        return false;
    }

    if (!deviceKeypairInitialized_ ||
        devicePrivateKeyPem_.isEmpty()) {

        Serial.println(
            "[mYBUS] ❌ Device keypair not initialized"
        );

        return false;
    }

    String sharedSecretHex;

    if (!cryptoComputeSharedSecret(
            devicePrivateKeyPem_,
            serverPublicKeyPem_,
            sharedSecretHex) ||
        sharedSecretHex.isEmpty()) {

        Serial.println(
            "[mYBUS] ❌ ECDH shared secret failed"
        );

        return false;
    }

    uint8_t sharedSecret[32] = {0};

    if (!cryptoHexToBytes(
            sharedSecretHex,
            sharedSecret,
            sizeof(sharedSecret))) {

        cryptoSecureZero(
            sharedSecret,
            sizeof(sharedSecret)
        );

        Serial.println(
            "[mYBUS] ❌ Shared secret conversion failed"
        );

        return false;
    }

    static constexpr char kHkdfSalt[] =
        "mYBUS-v2-Salt";

    static constexpr char kHkdfInfo[] =
        "mYBUS-v2-Session";

    cryptoSecureZero(
        sessionKey_,
        sizeof(sessionKey_)
    );

    const bool success =
        hkdfSha256(
            sharedSecret,
            sizeof(sharedSecret),

            reinterpret_cast<const uint8_t*>(
                kHkdfSalt),
            strlen(kHkdfSalt),

            reinterpret_cast<const uint8_t*>(
                kHkdfInfo),
            strlen(kHkdfInfo),

            sessionKey_,
            sizeof(sessionKey_)
        );

    cryptoSecureZero(
        sharedSecret,
        sizeof(sharedSecret)
    );

    if (!success) {
        cryptoSecureZero(
            sessionKey_,
            sizeof(sessionKey_)
        );

        Serial.println(
            "[mYBUS] ❌ HKDF derivation failed"
        );

        return false;
    }

    sessionKeyValid_ = true;

    Serial.println(
        "[mYBUS] ✅ Session key derived successfully"
    );

    return true;
}

// ============================================================
// Phase 2
// ============================================================

bool MybusSession::authenticateHandshakeSession(
    uint32_t requestNumber)
{
    Serial.println(
        "[mYBUS] 🔑 PHASE 2: HMAC Verification Started"
    );

    String hmac;

    if (!createChallengeHmac(hmac)) {
        Serial.println(
            "[mYBUS] ❌ Challenge HMAC creation failed"
        );

        return false;
    }

    JsonDocument phase2;

    phase2["protocolVersion"] =
        MYBUS_PROTOCOL_VERSION;

    phase2["communicationInterface"] =
        interfaceId_;

    phase2["zoneId"] =
        zone_;

    phase2["deviceId"] =
        deviceId_;

    phase2["requestNumber"] =
        requestNumber;

    phase2["flags"] = 0;

    phase2["security"] =
        mybus_proto::SECURITY_HANDSHAKE;

    phase2["command"] =
        mybus_proto::COMMAND_HANDSHAKE;

    JsonObject data =
        phase2["data"].to<JsonObject>();

    data["nonce"] =
        handshakeNonce_;

    data["hmac"] =
        hmac;

    String body;

    serializeJson(
        phase2,
        body
    );

    String response =
        transport_.sendRequest(
            "/devices/handshake",
            "POST",
            body,
            true
        );

    if (response.isEmpty()) {
        Serial.println(
            "[mYBUS] ❌ Phase 2 failed - empty response"
        );

        return false;
    }

    JsonDocument responseDoc;

    if (deserializeJson(
            responseDoc,
            response) != DeserializationError::Ok) {

        Serial.println(
            "[mYBUS] ❌ Phase 2 JSON parse error"
        );

        return false;
    }

    const bool authenticated =
        responseDoc["isAuthenticated"].is<bool>() &&
        responseDoc["isAuthenticated"].as<bool>();

    if (!authenticated) {
        Serial.println(
            "[mYBUS] ❌ Server rejected Phase 2"
        );

        return false;
    }

    Serial.println(
        "[mYBUS] ✅ Phase 2 authenticated"
    );

    return true;
}

// ============================================================
// Full handshake
// ============================================================

bool MybusSession::performHandshake(
    uint32_t requestNumber)
{
    Serial.println(
        "========== mYBUS v2 HANDSHAKE =========="
    );

    if (!deviceKeypairInitialized_ &&
        !initializeDeviceKeypair()) {

        Serial.println(
            "[mYBUS] ❌ Device keypair unavailable"
        );

        return false;
    }

    clear();

    if (!generateHandshakeNonce()) {
        Serial.println(
            "[mYBUS] ❌ Nonce generation failed"
        );

        return false;
    }

    // --------------------------------------------------------
    // Phase 1
    // --------------------------------------------------------

    JsonDocument phase1;

    phase1["protocolVersion"] =
        MYBUS_PROTOCOL_VERSION;

    phase1["communicationInterface"] =
        interfaceId_;

    phase1["zoneId"] =
        zone_;

    phase1["deviceId"] =
        deviceId_;

    phase1["requestNumber"] =
        requestNumber;

    phase1["flags"] = 0;

    phase1["security"] =
        mybus_proto::SECURITY_HANDSHAKE;

    phase1["command"] =
        mybus_proto::COMMAND_HANDSHAKE;

    JsonObject data =
        phase1["data"].to<JsonObject>();

    data["publicKeyPem"] =
        devicePublicKeyPem_;

    data["nonce"] =
        handshakeNonce_;

    String body;

    serializeJson(
        phase1,
        body
    );

    Serial.println(
        "[mYBUS] Sending Phase 1 (ECDH)"
    );

    String response =
        transport_.sendRequest(
            "/devices/handshake",
            "POST",
            body,
            true
        );

    if (response.isEmpty()) {
        Serial.println(
            "[mYBUS] ❌ Phase 1 failed - empty response"
        );

        clear();
        return false;
    }

    JsonDocument responseDoc;

    if (deserializeJson(
            responseDoc,
            response) != DeserializationError::Ok) {

        Serial.println(
            "[mYBUS] ❌ Phase 1 JSON parse error"
        );

        clear();
        return false;
    }

    const char* serverKey =
        responseDoc["serverPublicKeyPem"];

    if (serverKey == nullptr ||
        strlen(serverKey) == 0) {

        Serial.println(
            "[mYBUS] ❌ Missing/empty server public key"
        );

        clear();
        return false;
    }

    serverPublicKeyPem_ =
        String(serverKey);

    Serial.println(
        "[mYBUS] ✅ Server public key received"
    );

    // --------------------------------------------------------
    // ECDH + HKDF
    // --------------------------------------------------------

    if (!computeSessionKey()) {
        Serial.println(
            "[mYBUS] ❌ Session key derivation failed"
        );

        clear();
        return false;
    }

    // --------------------------------------------------------
    // Phase 2
    // --------------------------------------------------------

    if (!authenticateHandshakeSession(
            requestNumber)) {

        Serial.println(
            "[mYBUS] ❌ Phase 2 authentication failed"
        );

        clear();
        return false;
    }

    Serial.println(
        "[mYBUS] ✅ Secure session established"
    );

    Serial.println(
        "========================================"
    );

    return true;
}

// ============================================================
// Clear session
// ============================================================

void MybusSession::clear()
{
    cryptoSecureZero(
        sessionKey_,
        sizeof(sessionKey_)
    );

    sessionKeyValid_ = false;

    serverPublicKeyPem_ = "";
    handshakeNonce_ = "";
}