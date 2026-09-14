#pragma once
#include <Arduino.h>

struct RgbObject
{
    uint8_t  mode       = 0;    // 0=off, 1=solid, 2=... (طبق نیاز پروژه)
    uint16_t hue        = 0;    // 0-360 یا 0-255 بسته به مپینگ CHSV
    uint8_t  saturation = 255;
    uint8_t  lightness  = 128;  // اینجا به‌عنوان brightness/value استفاده می‌شود
};

extern RgbObject rgb_object;