#pragma once
#include <Arduino.h>

// ============================================================
// RgbObject — Process image for the RGB LED registry
//
// Mirrors the RGB Led Strips registers documented in the
// EcoSmart Registries Map:
//
//   0x8107  Mode        u8   R/W
//   0x8223  Hue         u16  R/W
//   0x8108  Saturation  u8   R/W
//   0x8109  Lightness   u8   R/W
// ============================================================

enum class RgbMode : uint8_t {
    OFF     = 0,
    SOLID   = 1,
    AUDIO   = 2,
};

struct RgbObject
{
    uint8_t  mode       = 0;
    uint16_t hue        = 0;
    uint8_t  saturation = 255;
    uint8_t  lightness  = 100;
};

extern RgbObject rgb_object;