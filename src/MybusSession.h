#ifndef MYBUS_SESSION_H
#define MYBUS_SESSION_H

#include <Arduino.h>
#include <mbedtls/ecp.h>
#include "HttpTransport.h"

class MybusSession {
public:
    MybusSession(HttpTransport& transport, const String& deviceId,
                 uint8_t interfaceId, uint8_t zone);

    bool initializeDeviceKeypair();
    bool performHandshake(uint32_t requestNumber);

    bool isEstablished() const { return sessionKeyValid_; }
    void clear();

    const uint8_t* sessionKey() const { return sessionKey_; }

private:
    bool computeSessionKey();
    bool authenticateHandshakeSession(uint32_t requestNumber);
    bool generateHandshakeNonce();
    bool createChallengeHmac(String& hmacHexOut);
    String createNonce();
    String hmacHex(const uint8_t* key, size_t keyLen,
                    const uint8_t* msg, size_t msgLen);

    HttpTransport& transport_;
    const String& deviceId_;
    uint8_t interfaceId_;
    uint8_t zone_;

    mbedtls_ecp_keypair deviceKeypair_;
    bool deviceKeypairInitialized_ = false;
    String devicePublicKeyPem_, devicePrivateKeyPem_;
    String serverPublicKeyPem_;
    String handshakeNonce_;

    uint8_t sessionKey_[32];
    bool sessionKeyValid_ = false;
};

#endif