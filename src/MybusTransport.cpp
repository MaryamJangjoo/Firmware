#include "MybusTransport.h"

#include <Arduino.h>
#include <string.h>
#include <vector>

#include "crypto.hpp"
#include "mybus_frame.h"
#include "mybus_registry.h"
#include "mybus_protocol_constants.h"

// ============================================================
// Constructor
// ============================================================

MybusTransport::MybusTransport(
    HttpTransport& transport,
    MybusSession& session)
    : transport_(transport),
      session_(session)
{
}

// ============================================================
// Address validation
// ============================================================

bool MybusTransport::validateAddress(
    uint8_t deviceId,
    uint8_t zone) const
{
    if (deviceId == 0 || deviceId == 255) {
        Serial.printf(
            "[mYBUS] ❌ Invalid deviceId: %u\n",
            deviceId
        );

        return false;
    }

    if (zone == 0 || zone == 255) {
        Serial.printf(
            "[mYBUS] ❌ Invalid zone: %u\n",
            zone
        );

        return false;
    }

    return true;
}

// ============================================================
// sendMybusBinaryFrame
// ============================================================

bool MybusTransport::sendMybusBinaryFrame(
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
    JsonDocument* outResponse)
{
    if (payloadLen > MYBUS_MAX_PAYLOAD_SIZE) {
        Serial.printf(
            "[mYBUS] ❌ Payload too large: %u\n",
            static_cast<unsigned>(payloadLen)
        );

        return false;
    }

    if (!session_.isEstablished()) {
        Serial.println(
            "[mYBUS] ❌ No secure session"
        );

        return false;
    }

    if (!validateAddress(
            deviceId,
            zone)) {

        return false;
    }

    // --------------------------------------------------------
    // SCU request
    // --------------------------------------------------------

    flags |=
        (1U << MYBUS_FLAG_SCU_BIT);

    // --------------------------------------------------------
    // Build header
    // --------------------------------------------------------

    MyBusHeader hdr;

    hdr.protocolVersion = MYBUS_PROTOCOL_VERSION;
    hdr.length          = 0;
    hdr.sequence        = sequence;
    hdr.interfaceId     = interfaceId;
    hdr.zone            = zone;
    hdr.deviceId        = deviceId;
    hdr.requestNumber   = requestNumber;
    hdr.qos             = qos;
    hdr.options         = options;
    hdr.flags            = flags;
    hdr.security         = security;
    hdr.compression      = compression;
    hdr.command          = command;

    // --------------------------------------------------------
    // Allocate plain/cipher buffers
    // --------------------------------------------------------

    const size_t maxFrameSize =
        MYBUS_HEADER_SIZE +
        MYBUS_MAX_PAYLOAD_SIZE +
        MYBUS_CRC_SIZE;

    std::vector<uint8_t> plainFrame(
        maxFrameSize
    );

    std::vector<uint8_t> ciphertext(
        maxFrameSize
    );

    // --------------------------------------------------------
    // Header + payload + CRC
    // --------------------------------------------------------

    const size_t plainLen =
        mybus_buildFrame(
            hdr,
            payload,
            payloadLen,
            plainFrame.data(),
            plainFrame.size()
        );

    if (plainLen == 0) {
        Serial.println(
            "[mYBUS] ❌ Frame build failed"
        );

        return false;
    }

    // --------------------------------------------------------
    // Debug plain frame
    // --------------------------------------------------------

    Serial.printf(
        "[mYBUS] Plain frame (%u bytes): ",
        static_cast<unsigned>(plainLen)
    );

    Serial.println(
        cryptoBytesToHex(
            plainFrame.data(),
            plainLen
        )
    );

    // --------------------------------------------------------
    // AES-256-GCM
    // --------------------------------------------------------

    uint8_t iv[MYBUS_AES_IV_SIZE] = {0};
    uint8_t tag[MYBUS_AES_TAG_SIZE] = {0};

    if (!mybus_encryptFrame(
            plainFrame.data(),
            plainLen,
            session_.sessionKey(),
            ciphertext.data(),
            iv,
            tag)) {

        Serial.println(
            "[mYBUS] ❌ Encryption failed"
        );

        return false;
    }

    // --------------------------------------------------------
    // Wire packet
    // [IV][Ciphertext][TAG]
    // --------------------------------------------------------

    const size_t wireCapacity =
        MYBUS_AES_IV_SIZE +
        plainLen +
        MYBUS_AES_TAG_SIZE;

    std::vector<uint8_t> wireMsg(
        wireCapacity
    );

    const size_t wireLen =
        mybus_packWireMessage(
            iv,
            tag,
            ciphertext.data(),
            plainLen,
            wireMsg.data(),
            wireMsg.size()
        );

    if (wireLen == 0) {
        Serial.println(
            "[mYBUS] ❌ Wire packing failed"
        );

        return false;
    }

    Serial.printf(
        "[mYBUS] Wire packet (%u bytes): ",
        static_cast<unsigned>(wireLen)
    );

    Serial.println(
        cryptoBytesToHex(
            wireMsg.data(),
            wireLen
        )
    );

    // --------------------------------------------------------
    // HTTP
    // --------------------------------------------------------

    std::vector<uint8_t> responseBytes;
    JsonDocument jsonError;

    const bool sent =
        transport_.sendRawBinaryToBackend(
            wireMsg.data(),
            wireLen,
            responseBytes,
            &jsonError
        );

    if (!sent) {
        if (outResponse != nullptr &&
            jsonError.size() > 0) {

            *outResponse = jsonError;
        }

        Serial.println(
            "[mYBUS] 📡 Result: ❌ FAILED"
        );

        return false;
    }

    // --------------------------------------------------------
    // Parse response
    //
    // ✅ رفع باگ: قبلاً deviceId و requestNumber به تابع دیکد پاس
    // داده نمی‌شدند و اعتبارسنجی نمی‌شدند. الان مقادیر مورد انتظار
    // (همان‌هایی که در این درخواست فرستادیم) پاس داده می‌شوند تا
    // پاسخ‌های قدیمی/جابه‌جاشده رد شوند.
    // --------------------------------------------------------

    if (outResponse != nullptr &&
        !responseBytes.empty()) {

        if (!decryptAndParseMybusResponse(
                responseBytes.data(),
                responseBytes.size(),
                deviceId,
                requestNumber,
                *outResponse)) {

            Serial.println(
                "[mYBUS] ⚠️ Failed to decrypt/parse response"
            );

            outResponse->clear();

            (*outResponse)["rawData"] =
                cryptoBytesToHex(
                    responseBytes.data(),
                    responseBytes.size()
                );

            (*outResponse)["rawLength"] =
                responseBytes.size();
        }
    }

    Serial.println(
        "[mYBUS] 📡 Result: ✅ SUCCESS"
    );

    return true;
}

// ============================================================
// Response decrypt + parse
// ============================================================

bool MybusTransport::decryptAndParseMybusResponse(
    const uint8_t* wireData,
    size_t wireLen,
    uint8_t expectedDeviceId,
    uint16_t expectedRequestNumber,
    JsonDocument& outDoc)
{
    if (!session_.isEstablished()) {
        Serial.println(
            "[mYBUS] ❌ No secure session"
        );

        return false;
    }

    const size_t minimumWireLen =
        MYBUS_AES_IV_SIZE +
        MYBUS_MIN_FRAME_SIZE +
        MYBUS_AES_TAG_SIZE;

    if (wireData == nullptr ||
        wireLen < minimumWireLen) {

        Serial.printf(
            "[mYBUS] ❌ Response too short: %u\n",
            static_cast<unsigned>(wireLen)
        );

        return false;
    }

    // --------------------------------------------------------
    // Wire parsing
    // --------------------------------------------------------

    const uint8_t* iv =
        wireData;

    const uint8_t* cipher =
        wireData +
        MYBUS_AES_IV_SIZE;

    const size_t cipherLen =
        wireLen -
        MYBUS_AES_IV_SIZE -
        MYBUS_AES_TAG_SIZE;

    const uint8_t* tag =
        wireData +
        MYBUS_AES_IV_SIZE +
        cipherLen;

    // --------------------------------------------------------
    // Decrypt
    // --------------------------------------------------------

    std::vector<uint8_t> plainFrame(
        cipherLen
    );

    if (!mybus_decryptFrame(
            cipher,
            cipherLen,
            session_.sessionKey(),
            iv,
            tag,
            plainFrame.data())) {

        Serial.println(
            "[mYBUS] ❌ Response GCM authentication failed"
        );

        return false;
    }

    Serial.printf(
        "[mYBUS] Response plain frame (%u bytes): ",
        static_cast<unsigned>(plainFrame.size())
    );

    Serial.println(
        cryptoBytesToHex(
            plainFrame.data(),
            plainFrame.size()
        )
    );

    // --------------------------------------------------------
    // Parse frame
    // --------------------------------------------------------

    MyBusHeader hdr;

    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;

    if (!mybus_parseFrame(
            plainFrame.data(),
            plainFrame.size(),
            hdr,
            &payload,
            &payloadLen)) {

        Serial.println(
            "[mYBUS] ❌ Response frame parse failed"
        );

        return false;
    }

    // --------------------------------------------------------
    // Response should have RSP
    // --------------------------------------------------------

    if ((hdr.flags & (1U << MYBUS_FLAG_RSP_BIT)) == 0) {
        Serial.println(
            "[mYBUS] ❌ Response frame has no RSP flag"
        );

        return false;
    }

    // --------------------------------------------------------
    // Validate basic response identity
    // --------------------------------------------------------

    if (hdr.interfaceId != session_.interfaceId()) {
        Serial.printf(
            "[mYBUS] ❌ Response interface mismatch: %u\n",
            hdr.interfaceId
        );

        return false;
    }

    if (hdr.zone != session_.zone()) {
        Serial.printf(
            "[mYBUS] ❌ Response zone mismatch: %u\n",
            hdr.zone
        );

        return false;
    }

    // ✅ اضافه شد: اعتبارسنجی deviceId و requestNumber پاسخ، تا یک
    // پاسخ قدیمی یا اشتباه که فقط interface/zone یکسان دارد پذیرفته
    // نشود.

    if (hdr.deviceId != expectedDeviceId) {
        Serial.printf(
            "[mYBUS] ❌ Response deviceId mismatch: got %u, expected %u\n",
            hdr.deviceId,
            expectedDeviceId
        );

        return false;
    }

    if (hdr.requestNumber != expectedRequestNumber) {
        Serial.printf(
            "[mYBUS] ❌ Response requestNumber mismatch: got %u, expected %u\n",
            hdr.requestNumber,
            expectedRequestNumber
        );

        return false;
    }

    // --------------------------------------------------------
    // Response result
    // --------------------------------------------------------

    const bool responseSuccess =
        (hdr.flags &
         (1U << MYBUS_FLAG_SF_BIT)) == 0;

    outDoc.clear();

    outDoc["protocolVersion"] =
        hdr.protocolVersion;

    outDoc["interface"] =
        hdr.interfaceId;

    outDoc["zone"] =
        hdr.zone;

    outDoc["deviceId"] =
        hdr.deviceId;

    outDoc["command"] =
        hdr.command;

    outDoc["flags"] =
        hdr.flags;

    outDoc["security"] =
        hdr.security;

    outDoc["requestNumber"] =
        hdr.requestNumber;

    outDoc["success"] =
        responseSuccess;

    outDoc["payloadLen"] =
        payloadLen;

    if (payloadLen > 0) {
        outDoc["payloadHex"] =
            cryptoBytesToHex(
                payload,
                payloadLen
            );
    }

    if (!responseSuccess &&
        payloadLen == 1) {

        outDoc["errorCode"] =
            payload[0];
    }

    return true;
}


void MybusTransport::decodeRegistryResponseValue(
    JsonDocument& doc,
    uint16_t regAddr)
{
    doc["regAddr"] = regAddr;

    const bool success =
        doc["success"] | false;

    if (!success) {
        return;
    }

    if (!doc["payloadHex"].is<const char*>()) {
        return;
    }

    const String hex =
        doc["payloadHex"].as<String>();

    if (hex.isEmpty() ||
        (hex.length() % 2) != 0) {

        return;
    }

    const size_t valueLen =
        hex.length() / 2;

    if (valueLen == 0 ||
        valueLen > MYBUS_MAX_PAYLOAD_SIZE) {

        return;
    }

    uint8_t value[
        MYBUS_MAX_PAYLOAD_SIZE
    ];

    if (!cryptoHexToBytes(
            hex,
            value,
            valueLen)) {

        return;
    }

    bool regIsWrite = false;
    bool regIsSystem = false;
    bool regIsArray = false;
    MyBusDataType dataType = DT_STRING;
    uint8_t regShortAddr = 0;

    decodeRegistryAddress(
        regAddr,
        regIsWrite,
        regIsSystem,
        regIsArray,
        dataType,
        regShortAddr
    );

    (void)regIsWrite;
    (void)regIsSystem;
    (void)regIsArray;
    (void)regShortAddr;

    switch (dataType) {

        case DT_BIT:
            if (valueLen >= 1) {
                doc["value"] =
                    (value[0] != 0);
            }
            break;

        case DT_UINT8:
            if (valueLen >= sizeof(uint8_t)) {
                doc["value"] =
                    value[0];
            }
            break;

        case DT_UINT16:
            if (valueLen >= sizeof(uint16_t)) {
                uint16_t v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_UINT32:
            if (valueLen >= sizeof(uint32_t)) {
                uint32_t v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_INT8:
            if (valueLen >= sizeof(int8_t)) {
                int8_t v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_INT16:
            if (valueLen >= sizeof(int16_t)) {
                int16_t v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_INT32:
            if (valueLen >= sizeof(int32_t)) {
                int32_t v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_FLOAT:
            if (valueLen >= sizeof(float)) {
                float v;

                memcpy(
                    &v,
                    value,
                    sizeof(v)
                );

                doc["value"] = v;
            }
            break;

        case DT_STRING:
        case DT_JSON:
        case DT_STRUCT:
        default: {
            String s;

            s.reserve(
                valueLen + 1
            );

            for (size_t i = 0;
                 i < valueLen;
                 ++i) {

                s +=
                    static_cast<char>(
                        value[i]
                    );
            }

            doc["value"] = s;
            break;
        }
    }
}

// ============================================================
// Registry frame
//
// ✅ رفع باگ: isWrite قبلاً کاملاً نادیده گرفته می‌شد ((void)isWrite;)
// و read/write فقط با خالی‌بودن regVal/valueLen در لایه‌ی بالاتر
// تشخیص داده می‌شد. الان اینجا هم یک اعتبارسنجی سازگاری انجام
// می‌شود: اگر isWrite=true باشد اما valueLen صفر باشد (یا برعکس)،
// درخواست رد می‌شود تا ناسازگاری بین قصد فراخواننده و داده‌ی واقعی
// زودتر مشخص شود.
// ============================================================

bool MybusTransport::sendRegistryFrame(
    uint16_t regAddr,
    const uint8_t* regValue,
    size_t valueLen,
    bool isWrite,
    uint8_t busDeviceId,
    uint32_t requestNumber,
    JsonDocument* outResponse)
{
    if (!session_.isEstablished()) {
        Serial.println(
            "[mYBUS] ❌ No secure session"
        );

        return false;
    }

    if (busDeviceId == 0 ||
        busDeviceId == 255) {

        Serial.printf(
            "[mYBUS] ❌ Invalid busDeviceId: %u\n",
            busDeviceId
        );

        return false;
    }

    if (valueLen >
        MYBUS_MAX_PAYLOAD_SIZE - 2) {

        Serial.printf(
            "[mYBUS] ❌ Registry value too large: %u\n",
            static_cast<unsigned>(valueLen)
        );

        return false;
    }

    if (isWrite && valueLen == 0) {
        Serial.println(
            "[mYBUS] ❌ isWrite=true اما valueLen صفر است"
        );

        return false;
    }

    if (!isWrite && valueLen != 0) {
        Serial.println(
            "[mYBUS] ❌ isWrite=false اما valueLen غیرصفر است (Read نباید Value داشته باشد)"
        );

        return false;
    }

    // --------------------------------------------------------
    // Current backend registry codec format:
    // [AddrLow][AddrHigh][Value...]
    // --------------------------------------------------------

    const size_t payloadLen =
        2 + valueLen;

    std::vector<uint8_t> payload(
        payloadLen
    );

    payload[0] =
        static_cast<uint8_t>(
            regAddr & 0xFFU
        );

    payload[1] =
        static_cast<uint8_t>(
            (regAddr >> 8U) & 0xFFU
        );

    if (valueLen > 0 &&
        regValue != nullptr) {

        memcpy(
            payload.data() + 2,
            regValue,
            valueLen
        );
    }

    uint8_t flags =
        mybus_proto::FLAG_REQUEST;

    flags |=
        (1U << MYBUS_FLAG_SCU_BIT);

    // --------------------------------------------------------
    // IMPORTANT:
    // interface and zone come from Session
    // --------------------------------------------------------

    const bool ok =
        sendMybusBinaryFrame(
            0,                              // sequence
            session_.interfaceId(),         // ✅ interface
            session_.zone(),                // ✅ zone
            busDeviceId,                    // ✅ numeric device ID
            static_cast<uint16_t>(
                requestNumber & 0xFFFFU
            ),
            mybus_proto::QOS_DEFAULT,
            mybus_proto::OPTIONS_DEFAULT,
            flags,
            mybus_proto::SECURITY_ENCRYPTED,
            mybus_proto::COMPRESSION_NONE,
            mybus_proto::COMMAND_REGISTRY,
            payload.data(),
            payload.size(),
            outResponse
        );

    if (ok &&
        outResponse != nullptr) {

        decodeRegistryResponseValue(
            *outResponse,
            regAddr
        );
    }

    return ok;
}

// ============================================================
// sendMybusData
//
// ✅ رفع باگ: قبلاً سوییچ نوشتن مقدار برای DT_UINT32/DT_INT8/DT_INT16
// هیچ case ای نداشت و به default (رشته‌ی خام) می‌افتاد، درحالی‌که
// decodeRegistryResponseValue این تایپ‌ها را کامل پشتیبانی می‌کند.
// ============================================================

bool MybusTransport::sendMybusData(
    JsonDocument& data,
    uint32_t requestNumber,
    JsonDocument* outResponse)
{
    if (!session_.isEstablished()) {
        Serial.println(
            "[mYBUS] ❌ No secure session"
        );

        return false;
    }

    const uint16_t regAddr =
        data["RegAdd"] | 0;

    const String regVal =
        data["RegVal"] | "";

    const uint8_t busDeviceId =
        data["DeviceId"] | 0;

    if (busDeviceId == 0 ||
        busDeviceId == 255) {

        Serial.println(
            "[mYBUS] ❌ Invalid DeviceId"
        );

        return false;
    }

    uint8_t value[64] = {0};
    size_t valueLen = 0;

    const MyBusDataType dataType =
        static_cast<MyBusDataType>(
            (regAddr >> 8) & 0x0F
        );

    if (!regVal.isEmpty()) {

        switch (dataType) {

            case DT_FLOAT: {
                char* endPtr = nullptr;

                const float f =
                    strtof(
                        regVal.c_str(),
                        &endPtr
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0')) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid float: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                memcpy(
                    value,
                    &f,
                    sizeof(float)
                );

                valueLen =
                    sizeof(float);

                break;
            }

            case DT_UINT32: {
                char* endPtr = nullptr;

                const unsigned long num =
                    strtoul(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0')) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid uint32: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                const uint32_t num32 =
                    static_cast<uint32_t>(num);

                memcpy(
                    value,
                    &num32,
                    sizeof(num32)
                );

                valueLen =
                    sizeof(num32);

                break;
            }

            case DT_INT32: {
                char* endPtr = nullptr;

                const long num =
                    strtol(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0')) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid int32: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                const int32_t num32 =
                    static_cast<int32_t>(num);

                memcpy(
                    value,
                    &num32,
                    sizeof(num32)
                );

                valueLen =
                    sizeof(num32);

                break;
            }

            case DT_UINT16: {
                char* endPtr = nullptr;

                const long num =
                    strtol(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0') ||
                    num < 0 ||
                    num > 0xFFFFL) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid uint16: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                const uint16_t num16 =
                    static_cast<uint16_t>(num);

                memcpy(
                    value,
                    &num16,
                    sizeof(num16)
                );

                valueLen =
                    sizeof(num16);

                break;
            }

            case DT_INT16: {
                char* endPtr = nullptr;

                const long num =
                    strtol(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0') ||
                    num < -32768L ||
                    num > 32767L) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid int16: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                const int16_t num16 =
                    static_cast<int16_t>(num);

                memcpy(
                    value,
                    &num16,
                    sizeof(num16)
                );

                valueLen =
                    sizeof(num16);

                break;
            }

            case DT_UINT8: {
                char* endPtr = nullptr;

                const long num =
                    strtol(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0') ||
                    num < 0 ||
                    num > 255) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid uint8: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                value[0] =
                    static_cast<uint8_t>(num);

                valueLen = 1;

                break;
            }

            case DT_INT8: {
                char* endPtr = nullptr;

                const long num =
                    strtol(
                        regVal.c_str(),
                        &endPtr,
                        10
                    );

                if (endPtr == regVal.c_str() ||
                    (endPtr != nullptr &&
                     *endPtr != '\0') ||
                    num < -128L ||
                    num > 127L) {

                    Serial.printf(
                        "[mYBUS] ❌ Invalid int8: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                value[0] =
                    static_cast<uint8_t>(
                        static_cast<int8_t>(num)
                    );

                valueLen = 1;

                break;
            }

            case DT_BIT: {
                const bool isTrue =
                    regVal.equalsIgnoreCase("true") ||
                    regVal == "1";

                const bool isFalse =
                    regVal.equalsIgnoreCase("false") ||
                    regVal == "0";

                if (!isTrue && !isFalse) {
                    Serial.printf(
                        "[mYBUS] ❌ Invalid bool: %s\n",
                        regVal.c_str()
                    );

                    return false;
                }

                value[0] =
                    isTrue ? 1 : 0;

                valueLen = 1;

                break;
            }

            case DT_STRING:
            default: {
                valueLen =
                    min(
                        regVal.length(),
                        sizeof(value)
                    );

                memcpy(
                    value,
                    regVal.c_str(),
                    valueLen
                );

                break;
            }
        }
    }

    // Empty RegVal = READ
    // Non-empty RegVal = WRITE
    const bool isWrite =
        !regVal.isEmpty();

    return sendRegistryFrame(
        regAddr,
        value,
        valueLen,
        isWrite,
        busDeviceId,
        requestNumber,
        outResponse
    );
}