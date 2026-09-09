#ifndef MYBUS_TRANSPORT_H
#define MYBUS_TRANSPORT_H

#include <Arduino.h>
#include <ArduinoJson.h>

#include "HttpTransport.h"
#include "MybusSession.h"

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