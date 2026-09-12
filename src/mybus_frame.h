/*
 * mYBUS Protocol V2 - Binary frame builder/parser for ESP32 (device side)
 *
 * Frame layout (16-byte header, Little-Endian):
 *   0     Protocol Version   u8
 *   1-2   Length             u16
 *   3     Sequence           u8
 *   4     Interface          u8
 *   5     Zone               u8
 *   6     Device ID          u8
 *   7     Reserved           u8
 *   8-9   Request Number     u16
 *   10    QoS                u8
 *   11    Options            u8
 *   12    Flags              u8
 *   13    Security           u8
 *   14    Compression        u8
 *   15    Command            u8
 *   16..  Payload            variable
 *   last 4 CRC32             u32
 */

#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// Constants

#define MYBUS_PROTOCOL_VERSION   2
#define MYBUS_HEADER_SIZE        16
#define MYBUS_CRC_SIZE           4
#define MYBUS_MIN_FRAME_SIZE     (MYBUS_HEADER_SIZE + MYBUS_CRC_SIZE) // 20
#define MYBUS_MAX_PAYLOAD_SIZE   128

// Commands

#define MYBUS_CMD_READ_REGISTRY    0x02   // Read Registry
#define MYBUS_CMD_WRITE_REGISTRY   0x03   // Write Registry

// Flags

#define MYBUS_FLAG_RSP_BIT   0   // Response flag
#define MYBUS_FLAG_SF_BIT    2   // Success/Fail flag (0=Success, 1=Fail)
#define MYBUS_FLAG_SCU_BIT   5   // SCU message flag

// Crypto Constants

#define MYBUS_AES_KEY_SIZE   32
#define MYBUS_AES_IV_SIZE    12
#define MYBUS_AES_TAG_SIZE   16

// Data Types (matching Registry Address Map)

enum MyBusDataType : uint8_t {
    DT_BIT = 0, 
    DT_UINT8 = 1, 
    DT_UINT16 = 2, 
    DT_UINT32 = 3,
    DT_INT8 = 4, 
    DT_INT16 = 5, 
    DT_INT32 = 6, 
    DT_FLOAT = 7,
    DT_STRING = 8, 
    DT_STRUCT = 9, 
    DT_JSON = 10
};

// Frame Header (16 bytes)

struct MyBusHeader {
    uint8_t  protocolVersion;  // offset 0
    uint16_t length;           // offset 1-2
    uint8_t  sequence;         // offset 3
    uint8_t  interfaceId;      // offset 4
    uint8_t  zone;             // offset 5
    uint8_t  deviceId;         // offset 6
    uint8_t  reserved;         // offset 7 (must be 0)
    uint16_t requestNumber;    // offset 8-9
    uint8_t  qos;              // offset 10
    uint8_t  options;          // offset 11
    uint8_t  flags;            // offset 12
    uint8_t  security;         // offset 13
    uint8_t  compression;      // offset 14
    uint8_t  command;          // offset 15
};


// CRC32
uint32_t mybus_crc32(const uint8_t *data, size_t len);

// Frame Builder

size_t mybus_buildFrame(
    MyBusHeader &hdr,
    const uint8_t *payload,
    size_t payloadLen,
    uint8_t *outFrame,
    size_t outFrameCapacity
);

// Frame Parser (after decryption)

bool mybus_parseFrame(
    const uint8_t *plainFrame,
    size_t frameLen,
    MyBusHeader &outHdr,
    const uint8_t **outPayload,
    size_t *outPayloadLen
);

// AES-256-GCM Encryption / Decryption

bool mybus_encryptFrame(
    const uint8_t *plainFrame,
    size_t plainLen,
    const uint8_t *key,
    uint8_t *outCipher,
    uint8_t *outIv,
    uint8_t *outTag
);

bool mybus_decryptFrame(
    const uint8_t *cipher,
    size_t cipherLen,
    const uint8_t *key,
    const uint8_t *iv,
    const uint8_t *tag,
    uint8_t *outPlain
);

// Wire Packing: [IV(12)][CIPHERTEXT(N)][TAG(16)]

size_t mybus_packWireMessage(
    const uint8_t *iv,
    const uint8_t *tag,
    const uint8_t *cipher,
    size_t cipherLen,
    uint8_t *outWire,
    size_t outWireCapacity
);

// Registry Payload Builders

/**
 * Builds READ registry payload: [AddrLow][AddrHigh]
 */
size_t mybus_buildReadRegistryPayload(
    uint16_t regAddr,
    uint8_t *outPayload,
    size_t outCapacity
);

/**
 * Builds WRITE registry payload: [AddrLow][AddrHigh][Value...]
 */
size_t mybus_buildWriteRegistryPayload(
    uint16_t regAddr,
    const uint8_t *value,
    size_t valueLen,
    uint8_t *outPayload,
    size_t outCapacity
);

/**
 * Parses registry response payload
 * Returns: regAddr, value pointer, value length
 */
bool mybus_parseRegistryPayload(
    const uint8_t *payload,
    size_t payloadLen,
    uint16_t &outRegAddr,
    const uint8_t **outValue,
    size_t &outValueLen
);

// ============================================================
// Incoming Frame Validation
//
// اعتبارسنجی یک فریم خام (رمزگشایی‌شده/رمزنگاری‌نشده) دقیقاً طبق مراحل زیر:
// 1. دریافت آرایه بایت
// 2. چک نسخه پروتکل
// 3. چک حداقل طول بسته
// 4. چک CRC32   (پیش‌نیاز فنی: تطبیق length اعلام‌شده با طول واقعی)
// 5. چک اینترفیس
// 6. چک پرچم‌ها (بیت‌های رزرو باید صفر باشند)
// 7. چک Command (فقط در لیست مجاز)
// ============================================================

// لیست کدهای مجاز برای فریم‌های ورودی.
//
// ⚠️ توجه: پیاده‌سازی فعلی MybusTransport::sendRegistryFrame همیشه کد
// mybus_proto::COMMAND_REGISTRY (=2) را چه برای Read چه برای Write می‌فرستد؛
// کدهای MYBUS_CMD_READ_REGISTRY(0x02)/MYBUS_CMD_WRITE_REGISTRY(0x03) در این
// آرایه صرفاً برای مواقعی است که این تفکیک روی سیم واقعاً پیاده شود. تا آن
// زمان، چون هر دو مقدار در عمل با 2 برابرند، این لیست همان یک کد را پوشش
// می‌دهد.
static constexpr uint8_t MYBUS_ALLOWED_COMMANDS[] = {
    MYBUS_CMD_READ_REGISTRY,   // 0x02
    MYBUS_CMD_WRITE_REGISTRY,  // 0x03
};
static constexpr size_t MYBUS_ALLOWED_COMMANDS_COUNT =
    sizeof(MYBUS_ALLOWED_COMMANDS) / sizeof(MYBUS_ALLOWED_COMMANDS[0]);

enum class MyBusFrameError : uint8_t {
    NONE = 0,
    EMPTY_FRAME,
    PROTOCOL_VERSION_MISMATCH,
    FRAME_TOO_SHORT,
    LENGTH_FIELD_MISMATCH,
    CRC_MISMATCH,
    INTERFACE_MISMATCH,
    RESERVED_FLAG_SET,
    COMMAND_NOT_ALLOWED
};

const char* mybus_frameErrorToString(MyBusFrameError err);

bool mybus_validateFrame(
    const uint8_t* frame,
    size_t frameLen,
    uint8_t expectedInterfaceId,
    const uint8_t* allowedCommands,
    size_t allowedCommandsCount,
    MyBusHeader& outHdr,
    const uint8_t** outPayload,
    size_t* outPayloadLen,
    MyBusFrameError& outError
);