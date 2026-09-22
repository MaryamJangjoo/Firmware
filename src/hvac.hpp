#ifndef HVAC_HPP
#define HVAC_HPP

#include <Arduino.h>

// ============================================================
// HvacObject - process image for the HVAC registry
//
// Mirrors the HVAC section of the EcoSmart Registries Map:
//
//   0x0705  Temperature   float  R
//   0x0706  Humidity      float  R
//   0x810A  Set Point     u8     R/W
//   0x8224  Remap Output  u16    R/W
// ============================================================

struct HvacObject
{
    float    temperature  = 0.0f;
    float    humidity     = 0.0f;
    uint8_t  set_point    = 24;
    uint16_t remap_output = 0;
};

extern HvacObject hvac_object;

#endif