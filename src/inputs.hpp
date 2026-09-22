#ifndef INPUTS_HPP
#define INPUTS_HPP

#include <Arduino.h>

constexpr uint8_t INPUTS_NUMBER = 16;

constexpr int INPUT_PIN_NONE = -1;

enum class InputMode : uint8_t {
    MOMENTARY = 0,
    TOGGLE    = 1,
    PULSE     = 2,
    LATCHED   = 3,
};

enum class InputSource : uint8_t {
    GPIO      = 0,
    SHIFT_REG = 1,
    NONE      = 2,
};

constexpr float    INPUT_EMA_ALPHA          = 0.05f;
constexpr uint32_t INPUT_SAMPLE_INTERVAL_MS = 5;
constexpr uint32_t INPUT_POLL_INTERVAL_MS   = INPUT_SAMPLE_INTERVAL_MS;

constexpr float INPUT_SCHMITT_HIGH = 0.75f;
constexpr float INPUT_SCHMITT_LOW  = 0.25f;

constexpr uint32_t INPUT_MASTER_COOLDOWN_MS = 300;

constexpr int PIN_SRI_LD   = 2;    
constexpr int PIN_SRI_CLK  = 18;   
constexpr int PIN_SRI_DATA = 19;  

constexpr size_t MAX_SHIFT_REGISTERS = 2;

struct InputConfig {
    int         pin        = INPUT_PIN_NONE;
    InputMode   mode       = InputMode::MOMENTARY;
    InputSource source     = InputSource::NONE;
    bool        activeLow  = false;
    bool        enabled    = false;
    bool        hasPullup  = false;
};

struct InputState {
    bool     rawLevel     = false;
    bool     stableLevel  = false;
    float    ema          = 0.0f;
    uint32_t lastEdgeMs   = 0;
    uint16_t eventCount   = 0;
};

extern bool        inputs_value[];
extern InputConfig inputs_config[];
extern InputState  inputs_state[];

extern uint8_t  inputs_internal_mode[INPUTS_NUMBER];
extern uint8_t  inputs_internal_enabled[INPUTS_NUMBER];
extern uint16_t inputs_internal_event_count[INPUTS_NUMBER];

constexpr uint8_t INPUT_MASTER_INDEX = 8;

#endif