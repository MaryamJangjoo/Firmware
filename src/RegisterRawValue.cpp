#ifndef REGISTER_RAW_VALUE_H
#define REGISTER_RAW_VALUE_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// ============================================================
// نمایش خام و بدون‌وابستگی‌به‌JSON یک مقدار رجیستر.
// هم AppController (تولیدکننده) و هم CloudWebSocketServer
// (مصرف‌کننده‌ی نهایی برای پاسخ WS) از همین تایپ استفاده می‌کنند،
// تا JsonDocument از مسیر داخلی انتقال مقدار کاملاً حذف شود.
// ============================================================

enum class RegRawType : uint8_t {
    BIT = 0,
    UINT8,
    UINT16,
    UINT32,
    INT8,
    INT16,
    INT32,
    FLOAT,
    STRING
};

struct RegisterRawValue {
    RegRawType type = RegRawType::UINT8;
    String stringValue;      // فقط وقتی type == STRING
    uint8_t bytes[8] = {0};  // بایت خام (little-endian) برای انواع عددی
    size_t byteLen = 0;

    bool isString() const { return type == RegRawType::STRING; }
};

#endif