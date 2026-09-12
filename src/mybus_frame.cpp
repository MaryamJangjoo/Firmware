#include "mybus_frame.h"
#include "mbedtls/gcm.h"

// CRC32 - IEEE 802.3, polynomial 0x04C11DB7

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

uint32_t mybus_crc32(const uint8_t *data, size_t len) {
    if (!crc32_table_ready) crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Little-Endian Helpers

static void putU16LE(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint16_t getU16LE(const uint8_t *buf) {
    return (uint16_t)(buf[0] | (buf[1] << 8));
}

static void putU32LE(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)((v >> 8) & 0xFF);
    buf[2] = (uint8_t)((v >> 16) & 0xFF);
    buf[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t getU32LE(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

// Frame Builder

size_t mybus_buildFrame(MyBusHeader &hdr, const uint8_t *payload,
                         size_t payloadLen, uint8_t *outFrame,
                         size_t outFrameCapacity) {
    size_t totalLen = MYBUS_HEADER_SIZE + payloadLen + MYBUS_CRC_SIZE;
    if (totalLen > outFrameCapacity) return 0;

    hdr.length = (uint16_t)totalLen;

    outFrame[0] = hdr.protocolVersion;
    putU16LE(outFrame + 1, hdr.length);
    outFrame[3] = hdr.sequence;
    outFrame[4] = hdr.interfaceId;
    outFrame[5] = hdr.zone;
    outFrame[6] = hdr.deviceId;
    outFrame[7] = 0; // Reserved
    putU16LE(outFrame + 8, hdr.requestNumber);
    outFrame[10] = hdr.qos;
    outFrame[11] = hdr.options;
    outFrame[12] = hdr.flags;
    outFrame[13] = hdr.security;
    outFrame[14] = hdr.compression;
    outFrame[15] = hdr.command;

    if (payloadLen > 0 && payload != nullptr) {
        memcpy(outFrame + MYBUS_HEADER_SIZE, payload, payloadLen);
    }

    uint32_t crc = mybus_crc32(outFrame, MYBUS_HEADER_SIZE + payloadLen);
    putU32LE(outFrame + MYBUS_HEADER_SIZE + payloadLen, crc);

    return totalLen;
}

// Frame Parser

bool mybus_parseFrame(const uint8_t *plainFrame, size_t frameLen,
                       MyBusHeader &outHdr, const uint8_t **outPayload,
                       size_t *outPayloadLen) {
    if (frameLen < MYBUS_MIN_FRAME_SIZE) return false;

    uint16_t declaredLen = getU16LE(plainFrame + 1);
    if (declaredLen != frameLen) return false;

    size_t payloadLen = frameLen - MYBUS_MIN_FRAME_SIZE;
    uint32_t expectedCrc = mybus_crc32(plainFrame, MYBUS_HEADER_SIZE + payloadLen);
    uint32_t actualCrc = getU32LE(plainFrame + MYBUS_HEADER_SIZE + payloadLen);

    if (expectedCrc != actualCrc) return false;

    outHdr.protocolVersion = plainFrame[0];
    outHdr.length = declaredLen;
    outHdr.sequence = plainFrame[3];
    outHdr.interfaceId = plainFrame[4];
    outHdr.zone = plainFrame[5];
    outHdr.deviceId = plainFrame[6];
    outHdr.reserved = plainFrame[7];
    outHdr.requestNumber = getU16LE(plainFrame + 8);
    outHdr.qos = plainFrame[10];
    outHdr.options = plainFrame[11];
    outHdr.flags = plainFrame[12];
    outHdr.security = plainFrame[13];
    outHdr.compression = plainFrame[14];
    outHdr.command = plainFrame[15];

    if (outHdr.protocolVersion != MYBUS_PROTOCOL_VERSION) return false;

    *outPayload = plainFrame + MYBUS_HEADER_SIZE;
    *outPayloadLen = payloadLen;
    return true;
}

// AES-256-GCM Encryption

bool mybus_encryptFrame(const uint8_t *plainFrame, size_t plainLen,
                         const uint8_t *key, uint8_t *outCipher,
                         uint8_t *outIv, uint8_t *outTag) {
    for (int i = 0; i < MYBUS_AES_IV_SIZE; i++) {
        outIv[i] = (uint8_t)esp_random();
    }

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key,
                                 MYBUS_AES_KEY_SIZE * 8);
    if (rc != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    rc = mbedtls_gcm_crypt_and_tag(
        &ctx, MBEDTLS_GCM_ENCRYPT, plainLen,
        outIv, MYBUS_AES_IV_SIZE,
        nullptr, 0,
        plainFrame, outCipher,
        MYBUS_AES_TAG_SIZE, outTag
    );

    mbedtls_gcm_free(&ctx);
    return rc == 0;
}

// AES-256-GCM Decryption

bool mybus_decryptFrame(const uint8_t *cipher, size_t cipherLen,
                         const uint8_t *key, const uint8_t *iv,
                         const uint8_t *tag, uint8_t *outPlain) {
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key,
                                 MYBUS_AES_KEY_SIZE * 8);
    if (rc != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    rc = mbedtls_gcm_auth_decrypt(
        &ctx, cipherLen,
        iv, MYBUS_AES_IV_SIZE,
        nullptr, 0,
        tag, MYBUS_AES_TAG_SIZE,
        cipher, outPlain
    );

    mbedtls_gcm_free(&ctx);
    return rc == 0;
}

// Wire Packing

size_t mybus_packWireMessage(const uint8_t *iv, const uint8_t *tag,
                              const uint8_t *cipher, size_t cipherLen,
                              uint8_t *outWire, size_t outWireCapacity) {
    size_t total = MYBUS_AES_IV_SIZE + cipherLen + MYBUS_AES_TAG_SIZE;
    if (total > outWireCapacity) return 0;

    size_t offset = 0;
    memcpy(outWire + offset, iv, MYBUS_AES_IV_SIZE);
    offset += MYBUS_AES_IV_SIZE;
    memcpy(outWire + offset, cipher, cipherLen);
    offset += cipherLen;
    memcpy(outWire + offset, tag, MYBUS_AES_TAG_SIZE);
    offset += MYBUS_AES_TAG_SIZE;

    return total;
}

// Registry Payload Builders

size_t mybus_buildReadRegistryPayload(
    uint16_t regAddr,
    uint8_t *outPayload,
    size_t outCapacity) {

    if (outCapacity < 2) return 0;

    // [AddrLow][AddrHigh]
    outPayload[0] = (uint8_t)(regAddr & 0xFF);
    outPayload[1] = (uint8_t)((regAddr >> 8) & 0xFF);

    return 2;
}

size_t mybus_buildWriteRegistryPayload(
    uint16_t regAddr,
    const uint8_t *value,
    size_t valueLen,
    uint8_t *outPayload,
    size_t outCapacity) {

    size_t totalLen = 2 + valueLen;
    if (totalLen > outCapacity) return 0;

    // [AddrLow][AddrHigh][Value...]
    outPayload[0] = (uint8_t)(regAddr & 0xFF);
    outPayload[1] = (uint8_t)((regAddr >> 8) & 0xFF);

    if (valueLen > 0 && value != nullptr) {
        memcpy(outPayload + 2, value, valueLen);
    }

    return totalLen;
}

bool mybus_parseRegistryPayload(
    const uint8_t *payload,
    size_t payloadLen,
    uint16_t &outRegAddr,
    const uint8_t **outValue,
    size_t &outValueLen) {

    if (payloadLen < 2) return false;

    // Read address (Little-Endian)
    outRegAddr = (uint16_t)(payload[0] | (payload[1] << 8));

    // Value starts at byte 2
    outValueLen = payloadLen - 2;
    *outValue = (outValueLen > 0) ? (payload + 2) : nullptr;

    return true;
}

// ============================================================
// Incoming Frame Validation
// ============================================================

const char* mybus_frameErrorToString(MyBusFrameError err) {
    switch (err) {
        case MyBusFrameError::NONE:                       return "OK";
        case MyBusFrameError::EMPTY_FRAME:                return "Empty frame";
        case MyBusFrameError::PROTOCOL_VERSION_MISMATCH:  return "Protocol version mismatch";
        case MyBusFrameError::FRAME_TOO_SHORT:            return "Frame shorter than minimum size";
        case MyBusFrameError::LENGTH_FIELD_MISMATCH:      return "Declared length != actual length";
        case MyBusFrameError::CRC_MISMATCH:               return "CRC32 mismatch";
        case MyBusFrameError::INTERFACE_MISMATCH:         return "Interface mismatch";
        case MyBusFrameError::RESERVED_FLAG_SET:          return "Reserved flag bit is set";
        case MyBusFrameError::COMMAND_NOT_ALLOWED:        return "Command not allowed (only Read/Write Registry)";
        default:                                          return "Unknown error";
    }
}

bool mybus_validateFrame(
    const uint8_t* frame,
    size_t frameLen,
    uint8_t expectedInterfaceId,
    const uint8_t* allowedCommands,
    size_t allowedCommandsCount,
    MyBusHeader& outHdr,
    const uint8_t** outPayload,
    size_t* outPayloadLen,
    MyBusFrameError& outError)
{
    outError = MyBusFrameError::NONE;
    if (outPayload)    *outPayload = nullptr;
    if (outPayloadLen) *outPayloadLen = 0;

    // ---- 1. دریافت آرایه بایت ----
    if (frame == nullptr || frameLen == 0) {
        outError = MyBusFrameError::EMPTY_FRAME;
        return false;
    }

    // ---- 2. چک نسخه پروتکل (آفست 0، فقط 1 بایت لازم است) ----
    if (frame[0] != MYBUS_PROTOCOL_VERSION) {
        outError = MyBusFrameError::PROTOCOL_VERSION_MISMATCH;
        return false;
    }

    // ---- 3. چک حداقل طول بسته ----
    if (frameLen < MYBUS_MIN_FRAME_SIZE) {
        outError = MyBusFrameError::FRAME_TOO_SHORT;
        return false;
    }

    const uint16_t declaredLen = getU16LE(frame + 1);

    if (declaredLen != frameLen) {
        outError = MyBusFrameError::LENGTH_FIELD_MISMATCH;
        return false;
    }

    const size_t payloadLen = frameLen - MYBUS_MIN_FRAME_SIZE;

    // ---- 4. چک CRC32 ----
    const uint32_t expectedCrc =
        mybus_crc32(frame, MYBUS_HEADER_SIZE + payloadLen);

    const uint32_t actualCrc =
        getU32LE(frame + MYBUS_HEADER_SIZE + payloadLen);

    if (expectedCrc != actualCrc) {
        outError = MyBusFrameError::CRC_MISMATCH;
        return false;
    }

    // ---- هدر را پر کن (فریم تا اینجا معتبر است) ----
    outHdr.protocolVersion = frame[0];
    outHdr.length          = declaredLen;
    outHdr.sequence        = frame[3];
    outHdr.interfaceId     = frame[4];
    outHdr.zone            = frame[5];
    outHdr.deviceId        = frame[6];
    outHdr.reserved        = frame[7];
    outHdr.requestNumber   = getU16LE(frame + 8);
    outHdr.qos             = frame[10];
    outHdr.options         = frame[11];
    outHdr.flags           = frame[12];
    outHdr.security        = frame[13];
    outHdr.compression     = frame[14];
    outHdr.command         = frame[15];

    // ---- 5. چک اینترفیس ----
    if (outHdr.interfaceId != expectedInterfaceId) {
        outError = MyBusFrameError::INTERFACE_MISMATCH;
        return false;
    }

    // ---- 6. چک پرچم‌ها (بیت‌های RSV طبق مستند پروتکل: 7،4،3،1 باید صفر باشند) ----
    static constexpr uint8_t MYBUS_FLAGS_RESERVED_MASK =
        (1U << 7) | (1U << 4) | (1U << 3) | (1U << 1);

    if ((outHdr.flags & MYBUS_FLAGS_RESERVED_MASK) != 0) {
        outError = MyBusFrameError::RESERVED_FLAG_SET;
        return false;
    }

    // ---- 7. چک Command (فقط در لیست مجاز) ----
    bool commandAllowed = false;
    for (size_t i = 0; i < allowedCommandsCount; ++i) {
        if (outHdr.command == allowedCommands[i]) {
            commandAllowed = true;
            break;
        }
    }

    if (!commandAllowed) {
        outError = MyBusFrameError::COMMAND_NOT_ALLOWED;
        return false;
    }

    if (outPayload)    *outPayload    = (payloadLen > 0) ? (frame + MYBUS_HEADER_SIZE) : nullptr;
    if (outPayloadLen) *outPayloadLen = payloadLen;

    return true;
}