#pragma once
#include <Arduino.h>

// ============================================================
// Digital Input Configuration and State
//
// Each physical input channel is described by an InputConfig
// entry (pin, behavior, active level) and an InputState entry
// (raw GPIO level, EMA filter value, debounced stable level,
// last edge timestamp, event counter).
//
// Debounce strategy (software-only, for input-only GPIOs
// without external pull-up):
//
//   The problem with a floating GPIO on the ESP32-WROOM is
//   that it acts as an antenna. WiFi and BT radio bursts bias
//   the pin, producing noisy levels that are stable for tens
//   of milliseconds - long enough that simple time-based or
//   N-sample voting filters still let them through.
//
//   The fix is a classic two-stage analog-style filter:
//
//   Stage 1 - Exponential Moving Average (EMA):
//     filtered = alpha * raw + (1 - alpha) * filtered
//     This is a one-pole low-pass filter. With alpha = 0.05
//     and a 5 ms sample interval, the time constant is about
//     100 ms, which is long enough to strongly attenuate RF
//     noise but still feels instant for a human button press.
//
//   Stage 2 - Schmitt trigger:
//     stableLevel only flips HIGH when the filtered value
//     rises above SCHMITT_HIGH, and only flips LOW when it
//     falls below SCHMITT_LOW. The dead band in between
//     prevents the output from toggling on borderline noise.
//
// The three mirror arrays (inputs_mode_value,
// inputs_enabled_value, inputs_event_count_value) are the
// process-image bindings that the registry layer writes to and
// reads from. The controller keeps them in sync with
// inputs_config[] and inputs_state[].
// ============================================================

constexpr uint8_t INPUTS_NUMBER = 16;

// Sentinel meaning "this input channel is not wired".
constexpr int INPUT_PIN_NONE = -1;

// Behavior model for each input channel.
enum class InputMode : uint8_t {
    MOMENTARY = 0,  // state follows the level while pressed
    TOGGLE    = 1,  // each edge flips the stable state
    PULSE     = 2,  // each edge produces a short pulse, state auto-clears
    LATCHED   = 3,  // once HIGH, stays HIGH until explicit reset
};

// ---- Stage 1: EMA filter tuning ----
constexpr float    INPUT_EMA_ALPHA          = 0.05f;
constexpr uint32_t INPUT_SAMPLE_INTERVAL_MS = 5;
constexpr uint32_t INPUT_POLL_INTERVAL_MS   = INPUT_SAMPLE_INTERVAL_MS;

// ---- Stage 2: Schmitt trigger thresholds ----
constexpr float INPUT_SCHMITT_HIGH = 0.75f;
constexpr float INPUT_SCHMITT_LOW  = 0.25f;

// ---- Master cooldown ----
// The master switch drives the whole shift register, which is
// expensive and visually disruptive. After a master transition,
// ignore further master edges for a fixed cooldown window.
constexpr uint32_t INPUT_MASTER_COOLDOWN_MS = 300;

struct InputConfig {
    int      pin        = INPUT_PIN_NONE;
    InputMode mode      = InputMode::MOMENTARY;
    bool     activeLow  = true;
    bool     enabled    = false;
    bool     hasPullup  = true;
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

// Process-image mirrors used by the registry layer.
extern uint8_t  inputs_mode_value[];
extern uint8_t  inputs_enabled_value[];
extern uint16_t inputs_event_count_value[];

// Input channel index that acts as the global master switch.
// Setting the master LOW forces every output OFF; setting it
// HIGH forces every output ON.
constexpr uint8_t INPUT_MASTER_INDEX = 0;