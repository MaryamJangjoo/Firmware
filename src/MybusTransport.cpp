#include "MybusTransport.h"

#include <Arduino.h>
#include <string.h>
#include <vector>

#include "crypto.hpp"
#include "mybus_frame.h"
#include "mybus_protocol_constants.h"

MybusTransport::MybusTransport(HttpTransport& transport, MybusSession& session)
    : transport_(transport), session_(session)
{
}

// ============================================================
// sendMybusBinaryFrame
// ============================================================

bool MybusTransport::sendMybusBinaryFrame(
    uint8_t sequence, uint8_t interfaceId, uint8_t zone, uint8_t deviceId,
    uint16_t requestNumber, uint8_t qos, uint8_t options, uint8_t flags,
    uint8_t security, uint8_t compression, uint8_t command,
    const uint8_t* payload, size_t payloadLen, JsonDocument* outResponse)
{
    if (payloadLen > MYBUS_MAX_PAYLOAD_SIZE) {
        Serial.printf("[mYBUS] ❌ Payload too large: %u (max %u)\n",
                      static_cast<unsigned>(payloadLen),
                      static_cast<unsigned>(MYBUS_MAX_PAYLOAD_SIZE));
        return false;
    }

    if (!session_.isEstablished()) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    flags |= (1 << MYBUS_FLAG_SCU_BIT);

    MyBusHeader hdr;
    hdr.protocolVersion = MYBUS_PROTOCOL_VERSION;
    hdr.length = 0;
    hdr.sequence = sequence;
    hdr.interfaceId = interfaceId;
    hdr.zone = zone;
    hdr.deviceId = deviceId;
    hdr.requestNumber = requestNumber;
    hdr.qos = qos;
    hdr.options = options;
    hdr.flags = flags;
    hdr.security = security;
    hdr.compression = compression;
    hdr.command = command;

    const size_t maxFrameSize = MYBUS_HEADER_SIZE + MYBUS_MAX_PAYLOAD_SIZE + MYBUS_CRC_SIZE + 64;

    std::vector<uint8_t> plainFrame(maxFrameSize, 0);
    std::vector<uint8_t> ciphertext(maxFrameSize, 0);

    size_t plainLen = mybus_buildFrame(hdr, payload, payloadLen, plainFrame.data(), maxFrameSize);
    if (plainLen == 0) {
        Serial.println("[mYBUS] ❌ Frame build failed");
        return false;
    }

    uint8_t iv[MYBUS_AES_IV_SIZE];
    uint8_t tag[MYBUS_AES_TAG_SIZE];
    memset(iv, 0, sizeof(iv));
    memset(tag, 0, sizeof(tag));

    if (!mybus_encryptFrame(plainFrame.data(), plainLen, session_.sessionKey(),
                             ciphertext.data(), iv, tag)) {
        Serial.println("[mYBUS] ❌ Encryption failed");
        return false;
    }

    // ✅ به‌جای بسته‌بندی دستی [IV][Ciphertext][Tag] (که در نسخه‌ی قدیمی
    // به‌صورت تکراری با mybus_packWireMessage انجام می‌شد)، مستقیماً از
    // همان تابع کمکی استفاده می‌کنیم.
    std::vector<uint8_t> wireMsg(MYBUS_AES_IV_SIZE + plainLen + MYBUS_AES_TAG_SIZE);
    size_t wireLen = mybus_packWireMessage(iv, tag, ciphertext.data(), plainLen,
                                            wireMsg.data(), wireMsg.size());
    if (wireLen == 0) {
        Serial.println("[mYBUS] ❌ Wire packing failed");
        return false;
    }

    std::vector<uint8_t> responseBytes;
    JsonDocument jsonError;
    bool sent = transport_.sendRawBinaryToBackend(wireMsg.data(), wireLen, responseBytes, &jsonError);

    if (!sent) {
        if (outResponse != nullptr && jsonError.size() > 0) {
            *outResponse = jsonError;
        }
        Serial.println("[mYBUS] 📡 Result: ❌ FAILED");
        return false;
    }

    if (outResponse != nullptr && !responseBytes.empty()) {
        if (!decryptAndParseMybusResponse(responseBytes.data(), responseBytes.size(), *outResponse)) {
            Serial.println("[mYBUS] ⚠️ Failed to decrypt/parse response frame");
            outResponse->clear();
            (*outResponse)["rawData"] = cryptoBytesToHex(responseBytes.data(), responseBytes.size());
            (*outResponse)["rawLength"] = responseBytes.size();
        }
    }

    Serial.println("[mYBUS] 📡 Result: ✅ SUCCESS");
    return true;
}

// ============================================================
// decryptAndParseMybusResponse
// ============================================================

bool MybusTransport::decryptAndParseMybusResponse(const uint8_t* wireData, size_t wireLen,
                                                    JsonDocument& outDoc)
{
    if (!session_.isEstablished()) {
        Serial.println("[mYBUS] ❌ Cannot parse response: no secure session");
        return false;
    }

    if (wireLen < MYBUS_AES_IV_SIZE + MYBUS_AES_TAG_SIZE + MYBUS_MIN_FRAME_SIZE) {
        Serial.printf("[mYBUS] ❌ Response too short: %u bytes\n",
                      static_cast<unsigned>(wireLen));
        return false;
    }

    const uint8_t* iv     = wireData;
    const uint8_t* cipher = wireData + MYBUS_AES_IV_SIZE;
    size_t cipherLen      = wireLen - MYBUS_AES_IV_SIZE - MYBUS_AES_TAG_SIZE;
    const uint8_t* tag    = wireData + MYBUS_AES_IV_SIZE + cipherLen;

    const size_t maxPlainLen = MYBUS_HEADER_SIZE + MYBUS_MAX_PAYLOAD_SIZE + MYBUS_CRC_SIZE + 64;
    if (cipherLen == 0 || cipherLen > maxPlainLen) {
        Serial.printf("[mYBUS] ❌ Response ciphertext length invalid: %u\n",
                      static_cast<unsigned>(cipherLen));
        return false;
    }

    std::vector<uint8_t> plainFrame(cipherLen);

    if (!mybus_decryptFrame(cipher, cipherLen, session_.sessionKey(), iv, tag, plainFrame.data())) {
        Serial.println("[mYBUS] ❌ Response decryption failed (bad key or tampered data)");
        return false;
    }

    MyBusHeader hdr;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;

    if (!mybus_parseFrame(plainFrame.data(), plainFrame.size(), hdr, &payload, &payloadLen)) {
        Serial.println("[mYBUS] ❌ Response frame parse failed (bad CRC or protocol version)");
        return false;
    }

    outDoc.clear();
    outDoc["command"] = hdr.command;
    outDoc["flags"] = hdr.flags;
    outDoc["security"] = hdr.security;
    outDoc["requestNumber"] = hdr.requestNumber;
    outDoc["payloadLen"] = payloadLen;

    bool responseSuccess = ((hdr.flags >> MYBUS_FLAG_SF_BIT) & 0x01) == 0;
    outDoc["success"] = responseSuccess;

    if (payloadLen > 0) {
        outDoc["payloadHex"] = cryptoBytesToHex(payload, payloadLen);
    }

    if (!responseSuccess && payloadLen == 1) {
        outDoc["errorCode"] = payload[0];
    }

    return true;
}

// ============================================================
// decodeRegistryResponseValue
// ============================================================

void MybusTransport::decodeRegistryResponseValue(JsonDocument& doc, uint16_t regAddr)
{
    doc["regAddr"] = regAddr;

    bool success = doc["success"] | false;
    if (!success) return;

    if (!doc["payloadHex"].is<const char*>()) return;

    String hex = doc["payloadHex"].as<String>();
    size_t valueLen = hex.length() / 2;
    if (valueLen == 0 || hex.length() % 2 != 0) return;

    uint8_t value[MYBUS_MAX_PAYLOAD_SIZE];
    if (valueLen > sizeof(value)) return;

    if (!cryptoHexToBytes(hex, value, valueLen)) return;

    MyBusDataType dataType = static_cast<MyBusDataType>((regAddr >> 8) & 0x0F);

    switch (dataType) {
        case DT_FLOAT: {
            if (valueLen >= sizeof(float)) {
                float f;
                memcpy(&f, value, sizeof(float));
                doc["value"] = f;
            }
            break;
        }
        case DT_INT32: {
            if (valueLen >= sizeof(int32_t)) {
                int32_t v;
                memcpy(&v, value, sizeof(int32_t));
                doc["value"] = v;
            }
            break;
        }
        case DT_UINT16: {
            if (valueLen >= sizeof(uint16_t)) {
                uint16_t v;
                memcpy(&v, value, sizeof(uint16_t));
                doc["value"] = v;
            }
            break;
        }
        case DT_BIT: {
            doc["value"] = (value[0] != 0);
            break;
        }
        case DT_STRING:
        default: {
            String s;
            s.reserve(valueLen + 1);
            for (size_t i = 0; i < valueLen; ++i) {
                s += static_cast<char>(value[i]);
            }
            doc["value"] = s;
            break;
        }
    }
}

// ============================================================
// sendRegistryFrame
// ✅ VLA حذف شد؛ به‌جایش std::vector با سایز runtime
// ============================================================

bool MybusTransport::sendRegistryFrame(
    uint16_t regAddr, const uint8_t* regValue, size_t valueLen, bool isWrite,
    uint8_t busDeviceId, uint32_t requestNumber, JsonDocument* outResponse)
{
    if (valueLen > MYBUS_MAX_PAYLOAD_SIZE - 2) {
        Serial.printf("[mYBUS] ❌ Value too large: %u (max %u)\n",
                      static_cast<unsigned>(valueLen),
                      static_cast<unsigned>(MYBUS_MAX_PAYLOAD_SIZE - 2));
        return false;
    }

    // ⚠️ isWrite همچنان مثل نسخه‌ی قدیمی در منطق زیر استفاده نمی‌شود
    // (کد کامند Read/Write هر دو یکسان‌اند: MYBUS_CMD_READ/WRITE_REGISTRY=2).
    // برای مستندسازی صریح نگه داشته شده؛ اگر قرار است رفتار واقعی روی آن
    // شرط بگذارد، اینجا باید اضافه شود.
    (void)isWrite;

    std::vector<uint8_t> payload(2 + valueLen);
    payload[0] = regAddr & 0xFF;
    payload[1] = (regAddr >> 8) & 0xFF;
    if (regValue != nullptr && valueLen > 0) {
        memcpy(payload.data() + 2, regValue, valueLen);
    }

    uint8_t flags = mybus_proto::FLAG_REQUEST;
    flags |= (1 << MYBUS_FLAG_SCU_BIT);

    bool ok = sendMybusBinaryFrame(
        0,
        0, // ⚠️ interfaceId اینجا صفر است، برخلاف نسخه‌ی قدیمی که
           // MYBUS_INTERFACE_WIFI را برای فریم‌های Registry هم می‌فرستاد.
           // اگر بک‌اند این فیلد را چک می‌کند، باید interfaceId واقعی را
           // (مثلاً به این متد اضافه کنید یا از MybusSession بگیرید) پاس بدهید.
        0, // zone - همان ملاحظه‌ی بالا
        busDeviceId,
        static_cast<uint16_t>(requestNumber),
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

    if (ok && outResponse != nullptr) {
        decodeRegistryResponseValue(*outResponse, regAddr);
    }

    return ok;
}

// ============================================================
// sendMybusData
// ============================================================

bool MybusTransport::sendMybusData(JsonDocument& data, uint32_t requestNumber,
                                    JsonDocument* outResponse)
{
    if (!session_.isEstablished()) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    uint16_t regAddr = data["RegAdd"] | 0;
    String regVal = data["RegVal"] | "";
    uint8_t busDeviceId = data["DeviceId"] | 1;

    uint8_t value[32];
    size_t valueLen = 0;

    MyBusDataType dataType = static_cast<MyBusDataType>((regAddr >> 8) & 0x0F);

    if (!regVal.isEmpty()) {
        switch (dataType) {
            case DT_FLOAT: {
                char* endPtr = nullptr;
                float f = strtod(regVal.c_str(), &endPtr);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0')) {
                    Serial.printf("[mYBUS] ⚠️ Invalid float value: '%s' (sent as 0)\n", regVal.c_str());
                }
                memcpy(value, &f, sizeof(float));
                valueLen = sizeof(float);
                break;
            }
            case DT_INT32: {
                char* endPtr = nullptr;
                long num = strtol(regVal.c_str(), &endPtr, 10);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0')) {
                    Serial.printf("[mYBUS] ⚠️ Invalid int32 value: '%s' (sent as 0)\n", regVal.c_str());
                }
                int32_t num32 = static_cast<int32_t>(num);
                memcpy(value, &num32, sizeof(int32_t));
                valueLen = sizeof(int32_t);
                break;
            }
            case DT_UINT16: {
                char* endPtr = nullptr;
                long num = strtol(regVal.c_str(), &endPtr, 10);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0') || num < 0 || num > 0xFFFF) {
                    Serial.printf("[mYBUS] ⚠️ Invalid uint16 value: '%s'\n", regVal.c_str());
                }
                uint16_t num16 = (uint16_t)num;
                memcpy(value, &num16, sizeof(uint16_t));
                valueLen = sizeof(uint16_t);
                break;
            }
            case DT_BIT: {
                bool isTrue = regVal.equalsIgnoreCase("true") || regVal == "1";
                bool isFalse = regVal.equalsIgnoreCase("false") || regVal == "0";
                if (!isTrue && !isFalse) {
                    Serial.printf("[mYBUS] ⚠️ Ambiguous bool value: '%s' (treated as false)\n", regVal.c_str());
                }
                value[0] = isTrue ? 1 : 0;
                valueLen = 1;
                break;
            }
            case DT_STRING:
            default: {
                valueLen = min(regVal.length(), sizeof(value) - 1);
                memcpy(value, regVal.c_str(), valueLen);
                break;
            }
        }
    }

    return sendRegistryFrame(regAddr, value, valueLen, true, busDeviceId, requestNumber, outResponse);
}