#pragma once
#include <Arduino.h>

const uint8_t OUTPUTS_NUMBER = 16;

typedef struct OutputObject
{
    bool value;
    uint16_t timer_permanent;
    uint16_t timer_sleep;
};


extern OutputObject outputs_object[];