#ifndef MYBUS_TRANSPORT_H
#define MYBUS_TRANSPORT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

#include "HttpTransport.h"
#include "MybusSession.h"

// Forward declarations to avoid pulling in mybus_frame.h, which
// defines MYBUS_MAX_PAYLOAD_SIZE as a macro and would collide with
// any identifier of the same name in this header.
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

    bool isSessionEstablished() const;

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

    // NOTE: this is a class-scoped constant, not the global macro
    // MYBUS_MAX_PAYLOAD_SIZE defined in mybus_frame.h. Keeping the
    // same name here was the source of a name collision that broke
    // the build, so it is renamed to kMaxPayloadSize to avoid any
    // textual substitution by the preprocessor.
    static constexpr size_t kMaxPayloadSize = 512;
};

#endif