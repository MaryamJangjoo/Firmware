#ifndef MYBUS_VALUE_CODEC_H
#define MYBUS_VALUE_CODEC_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "mybus_frame.h"

// regVal (متن) -> بایت خام، بر اساس MyBusDataType.
// outLen: ورودی = ظرفیت outBuf، خروجی = طول واقعی نوشته‌شده.
bool encodeRegValueString(const String& regVal, MyBusDataType type,
                           uint8_t* outBuf, size_t outCapacity, size_t& outLen);

// بایت خام -> doc["value"] با نوع مناسب (bool/int/float/String).
void decodeRegValueToJson(JsonDocument& doc, const uint8_t* value,
                           size_t valueLen, MyBusDataType type);

#endif