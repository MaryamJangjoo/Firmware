/*
 * mybus_frame.h
 * mYBUS Protocol V2 - Binary frame builder/parser for ESP32 (device side)
 *
 * Frame layout (16-byte header, Little-Endian):
 *   0     Protocol Version   u8
 *   1-2   Length             u16   (total frame length, header+payload+crc)
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
 *   16..  Payload            variable (Length - 20 bytes)
 *   last 4 CRC32             u32 (IEEE 802.3, poly 0x04C11DB7, computed over
 *                                 header+payload BEFORE encryption)
 *
 * The full plain frame (header+payload+crc) is what gets AES-256-GCM
 * encrypted before it is ever put on the wire.
 *
 * ✅ Wire format (HTTP POST body): [IV(12)][CIPHERTEXT(N)][TAG(16)]
 * This matches the backend expectation.
 *
 * ⚠️ NOTE: The actual registry payload format used on this device is built
 * directly in CloudManager::sendRegistryFrame() as [AddrLow][AddrHigh][Value],
 * WITHOUT a ValueLen byte. There used to be two helper functions here
 * (mybus_buildPayload_ReadRegistry / mybus_buildPayload_WriteRegistry) that
 * built a DIFFERENT, incompatible payload format ([RegAddr u16][ValueLen u8]
 * [RegVal]). They were never called anywhere and have been removed to avoid
 * accidental misuse. If you need a payload builder again, base it on
 * CloudManager::sendRegistryFrame's format, not the old one.
 */

#pragma once
#include <Arduino.h>
#include <stdint.h>

// ---- Protocol constants ----
#define MYBUS_PROTOCOL_VERSION   2
#define MYBUS_HEADER_SIZE        16
#define MYBUS_CRC_SIZE           4
#define MYBUS_MIN_FRAME_SIZE     (MYBUS_HEADER_SIZE + MYBUS_CRC_SIZE) // 20

// Commands
#define MYBUS_CMD_SET_ADDRESS     1
#define MYBUS_CMD_READ_REGISTRY   2
#define MYBUS_CMD_WRITE_REGISTRY  2  // same command code, RegVal empty = read
#define MYBUS_CMD_WHO_IS          3
#define MYBUS_CMD_PING            4

// Flags bit positions (Table: Flags)
#define MYBUS_FLAG_RSP_BIT   0  // 0=Request, 1=Response
#define MYBUS_FLAG_SF_BIT    2  // 0=Success, 1=Fail (ignored in requests)
#define MYBUS_FLAG_SCU_BIT   5  // 0=EGD message, 1=SCU message

// AES-256-GCM sizes
#define MYBUS_AES_KEY_SIZE   32
#define MYBUS_AES_IV_SIZE    12
#define MYBUS_AES_TAG_SIZE   16

// Data types (Registry Address Map DT0-DT3 field)
enum MyBusDataType : uint8_t {
  DT_BIT = 0, DT_UINT8 = 1, DT_UINT16 = 2, DT_UINT32 = 3,
  DT_INT8 = 4, DT_INT16 = 5, DT_INT32 = 6, DT_FLOAT = 7,
  DT_STRING = 8, DT_JSON = 9, DT_STRUCT = 10
};

// Plain (unencrypted) header fields, used to build/parse a frame
struct MyBusHeader {
  uint8_t  protocolVersion;
  uint16_t length;          // filled automatically by buildFrame()
  uint8_t  sequence;
  uint8_t  interfaceId;
  uint8_t  zone;
  uint8_t  deviceId;
  uint16_t requestNumber;
  uint8_t  qos;
  uint8_t  options;
  uint8_t  flags;
  uint8_t  security;
  uint8_t  compression;
  uint8_t  command;
};

// ---- CRC32 (IEEE 802.3, poly 0x04C11DB7, reflected impl / standard zlib crc32) ----
uint32_t mybus_crc32(const uint8_t *data, size_t len);

// ---- Frame builder ----
// Builds the PLAIN frame (header + payload + crc) into outFrame.
// outFrame must have capacity >= MYBUS_MIN_FRAME_SIZE + payloadLen.
// Returns total plain frame length, or 0 on error (buffer too small).
size_t mybus_buildFrame(MyBusHeader &hdr, const uint8_t *payload,
                         size_t payloadLen, uint8_t *outFrame,
                         size_t outFrameCapacity);

// ---- AES-256-GCM encrypt/decrypt of a full plain frame ----
// key must be 32 bytes. iv (12 bytes) and tag (16 bytes) are output params
// on encrypt, input params on decrypt.
// outCipher must have capacity >= plainLen.
bool mybus_encryptFrame(const uint8_t *plainFrame, size_t plainLen,
                         const uint8_t *key /*32 bytes*/,
                         uint8_t *outCipher, uint8_t *outIv /*12 bytes*/,
                         uint8_t *outTag /*16 bytes*/);

bool mybus_decryptFrame(const uint8_t *cipher, size_t cipherLen,
                         const uint8_t *key /*32 bytes*/,
                         const uint8_t *iv /*12 bytes*/,
                         const uint8_t *tag /*16 bytes*/,
                         uint8_t *outPlain);

// ---- Wire packing: [IV(12)][CIPHERTEXT(N)][TAG(16)] ----
// This is the exact byte layout sent as the raw HTTP body
// (Content-Type: application/octet-stream). Returns total bytes written.
size_t mybus_packWireMessage(const uint8_t *iv, const uint8_t *tag,
                              const uint8_t *cipher, size_t cipherLen,
                              uint8_t *outWire, size_t outWireCapacity);

// ---- Parse a plain frame's header out of raw bytes (after decryption) ----
// Returns false if buffer too short, CRC invalid, or protocol version mismatch.
bool mybus_parseFrame(const uint8_t *plainFrame, size_t frameLen,
                       MyBusHeader &outHdr, const uint8_t **outPayload,
                       size_t *outPayloadLen);