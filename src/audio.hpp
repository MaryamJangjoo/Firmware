#pragma once
#include <Arduino.h>

struct AudioObject
{
    uint8_t  mode        = 0;
    uint8_t  control     = 0;
    uint16_t sleep_timer = 0;
    uint16_t station     = 0;
    String   title;
    String   artist;
    uint8_t  volume      = 70;
    uint8_t  bass        = 50;
    uint8_t  treble      = 50;
    uint8_t  eq          = 0;
};

extern AudioObject audio_object;