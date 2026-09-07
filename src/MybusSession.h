#ifndef MYBUS_SESSION_H
#define MYBUS_SESSION_H

#include <Arduino.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecp.h>
#include <ArduinoJson.h>

#include "HttpTransport.h"

class MybusSession {
public:
    MybusSession(
        HttpTransport& transport,
        const String& deviceId,
        uint8_t interfaceId,
        uint8_t zone
    );

    ~MybusSession();

    bool initializeDeviceKeypair();

    bool performHandshake(uint32_t requestNumber);

    bool authenticateHandshakeSession(uint32_t requestNumber);

    bool generateHandshakeNonce();

    bool createChallengeHmac(String& hmacHexOut);

    bool computeSessionKey();

    void clear();

    bool isEstablished() const
    {
        return sessionKeyValid_;
    }

    const uint8_t* sessionKey() const
    {
        return sessionKey_;
    }

    uint8_t interfaceId() const
    {
        return interfaceId_;
    }

    uint8_t zone() const
    {
        return zone_;
    }

    const String& deviceId() const
    {
        return deviceId_;
    }

    void setInterfaceId(uint8_t interfaceId)
    {
        interfaceId_ = interfaceId;
    }

    void setZone(uint8_t zone)
    {
        zone_ = zone;
    }

    // ✅ رفع باگ: deviceId_ در این کلاس یک کپی مستقل است (نه رفرنس).
    // اگر CloudManager::deviceId_ بعداً تغییر کند (مثلاً بعد از لاگین
    // یا preferences)، این کپی به‌طور خودکار sync نمی‌شود. این متد
    // باید صراحتاً بعد از هر تغییر deviceId در CloudManager صدا زده شود
    // (نگاه کنید به CloudManager.cpp: constructor، loginUser، performHandshake،
    // setDeviceId).
    void setDeviceId(const String& deviceId)
    {
        deviceId_ = deviceId;
    }

private:
    String createNonce();

    String hmacHex(
        const uint8_t* key,
        size_t keyLen,
        const uint8_t* msg,
        size_t msgLen
    );

    HttpTransport& transport_;

    String deviceId_;

    uint8_t interfaceId_;
    uint8_t zone_;

    bool deviceKeypairInitialized_ = false;
    bool sessionKeyValid_ = false;

    mbedtls_ecp_keypair deviceKeypair_;

    uint8_t sessionKey_[32] = {0};

    String devicePublicKeyPem_;
    String devicePrivateKeyPem_;
    String serverPublicKeyPem_;
    String handshakeNonce_;
};

#endif