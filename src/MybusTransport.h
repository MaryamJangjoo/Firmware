#ifndef MYBUS_TRANSPORT_H
#define MYBUS_TRANSPORT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "HttpTransport.h"
#include "MybusSession.h"

// ⚠️ عمداً mybus_frame.h اینجا include نمی‌شود: آن فایل ماکروی
// MYBUS_MAX_PAYLOAD_SIZE (=128) را تعریف می‌کند که با نام عضو کلاس
// پایین (constexpr size_t MYBUS_MAX_PAYLOAD_SIZE = 512;) تداخل متنی
// پیدا می‌کند. به‌جایش فقط forward-declare می‌کنیم.
struct MyBusHeader;
enum class MyBusFrameError : uint8_t;

class MybusTransport {
public:
    MybusTransport(
        HttpTransport& transport,
        MybusSession& session
    );

    bool sendMybusBinaryFrame(
        uint8_t sequence,
        uint8_t interfaceId,
        uint8_t zone,
        uint8_t deviceId,
        uint16_t requestNumber,
        uint8_t qos,
        uint8_t options,
        uint8_t flags,
        uint8_t security,
        uint8_t compression,
        uint8_t command,
        const uint8_t* payload,
        size_t payloadLen,
        JsonDocument* outResponse = nullptr
    );

    bool sendRegistryFrame(
        uint16_t regAddr,
        const uint8_t* regValue,
        size_t valueLen,
        bool isWrite,
        uint8_t busDeviceId,
        uint32_t requestNumber,
        JsonDocument* outResponse = nullptr
    );

    bool sendMybusData(
        JsonDocument& data,
        uint32_t requestNumber,
        JsonDocument* outResponse = nullptr
    );

    // ========================================================
    // Generic control-frame helpers - بدون هیچ I/O روی HTTP.
    //
    // این‌ها توسط CloudWebSocketServer استفاده می‌شوند تا همان فرمت
    // سیمی رمزنگاری‌شده‌ی mYBUS v2 ([IV][CIPHERTEXT][TAG]) را مستقیماً
    // روی کانال محلی WebSocket پیاده کنند (به‌جای رفتن از مسیر
    // HttpTransport::sendRawBinaryToBackend).
    //
    // هر دو نیاز دارند session_.isEstablished() true باشد (یعنی
    // هندشیک ECDH با بک‌اند روی HTTP قبلاً کامل شده باشد).
    // ========================================================

    bool buildControlFrame(
        uint8_t command,
        uint8_t flags,
        uint16_t requestNumber,
        const uint8_t* payload,
        size_t payloadLen,
        std::vector<uint8_t>& outWire
    );

    bool parseControlFrame(
        const uint8_t* wireData,
        size_t wireLen,
        const uint8_t* allowedCommands,
        size_t allowedCommandsCount,
        MyBusHeader& outHdr,
        std::vector<uint8_t>& outPayload,
        MyBusFrameError& outError
    );

private:
 
    bool decryptAndParseMybusResponse(
        const uint8_t* wireData,
        size_t wireLen,
        uint8_t expectedDeviceId,
        uint16_t expectedRequestNumber,
        JsonDocument& outDoc
    );

    void decodeRegistryResponseValue(
        JsonDocument& doc,
        uint16_t regAddr
    );

    bool validateAddress(
        uint8_t deviceId,
        uint8_t zone
    ) const;

    HttpTransport& transport_;
    MybusSession& session_;

    static constexpr size_t MYBUS_MAX_PAYLOAD_SIZE = 512;
};

#endif