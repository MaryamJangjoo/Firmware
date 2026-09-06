#ifndef MYBUS_PROTOCOL_CONSTANTS_H
#define MYBUS_PROTOCOL_CONSTANTS_H

#include <stdint.h>
#include "mybus_frame.h"

// ✅ این مقادیر مستقیماً از سورس بک‌اند تأیید شده‌اند:
//   - src/infrastructure/mybus/interfaces/mybus-frame.interface.ts
//   - HandshakeRequestDto / SecureDataRequestDto (class-validator @IsIn)
// دیگر حدسی نیستند.
namespace mybus_proto {

constexpr uint8_t QOS_DEFAULT        = 0;  // ⚠️ هنوز تأیید نشده - در هیچ DTO ای دیده نشد
constexpr uint8_t OPTIONS_DEFAULT    = 0;  // ⚠️ هنوز تأیید نشده - در هیچ DTO ای دیده نشد
constexpr uint8_t FLAG_REQUEST       = 0;  // ✅ HandshakeRequestDto: flags=0 یعنی Request

constexpr uint8_t SECURITY_HANDSHAKE = 1;  // ✅ MybusSecurity.AUTH_ONLY - برای Phase 1 و Phase 2 هر دو
constexpr uint8_t SECURITY_ENCRYPTED = 2;  // ✅ MybusSecurity.AUTH_ENCRYPT - برای فریم‌های Registry

constexpr uint8_t COMPRESSION_NONE   = 0;

constexpr uint8_t COMMAND_HANDSHAKE  = 250; // ✅ 0xFA - HandshakeRequestDto: @IsIn([250])
constexpr uint8_t COMMAND_REGISTRY   = 2;   // ✅ MybusCommand.READ_WRITE_REGISTRY

constexpr uint8_t INTERFACE_WIFI     = 1;   // ✅ MybusInterface.WEBSOCKET (مقدار پیش‌فرض/مثال DTO)
constexpr uint8_t ZONE_DEFAULT       = 0;   // ⚠️ هنوز تأیید نشده - به نظر می‌رسد per-device پیکربندی می‌شود
                                             // (مثال DTO: zoneId=10)، نه یک مقدار ثابت سراسری

} // namespace mybus_proto

#endif