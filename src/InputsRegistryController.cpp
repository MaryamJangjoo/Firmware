#include "InputsRegistryController.h"

#include <Arduino.h>
#include <cstring>

#include "OutputsRegistryController.h"
#include "Outputs.hpp"
#include "Logging.h"

static const char* TAG = "INPUTS";

// ============================================================
// Default channel configuration
//
// All 16 input channels come from the two cascaded 74HC165
// chips (U3 and U5) on the EcoSmart CPU board. Every channel
// has a 10K pull-up, so a released switch reads HIGH and a
// pressed switch reads LOW. The default config therefore uses
// activeLow = false.
// ============================================================

namespace {

InputConfig makeDefaultConfig(uint8_t index)
{
    InputConfig cfg;

    cfg.pin        = INPUT_PIN_NONE;
    cfg.source     = InputSource::SHIFT_REG;
    cfg.mode       = InputMode::MOMENTARY;
    cfg.activeLow  = false;     // Pull-up: HIGH = released, LOW = pressed
    cfg.hasPullup  = false;     // External pull-ups on the board

    // All 16 channels are enabled by default.
    cfg.enabled = (index < INPUTS_NUMBER);

    return cfg;
}

}  // namespace

InputsRegistryController::InputsRegistryController()
{
    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        inputs_config[i] = makeDefaultConfig(i);
        inputs_state[i]  = InputState{};
        inputs_value[i]  = false;

        inputs_internal_mode[i]        = static_cast<uint8_t>(inputs_config[i].mode);
        inputs_internal_enabled[i]     = inputs_config[i].enabled ? 1 : 0;
        inputs_internal_event_count[i] = 0;
    }

    std::memset(sri_buffer, 0, sizeof(sri_buffer));
}

void InputsRegistryController::attachOutputs(OutputsRegistryController* outputs)
{
    outputs_ = outputs;
}

void InputsRegistryController::initShiftRegisters()
{
    // The board has two cascaded 74HC165 chips, providing 16
    // input channels.
    number_of_shift_register = 2;

    if (number_of_shift_register > MAX_SHIFT_REGISTERS) {
        number_of_shift_register = MAX_SHIFT_REGISTERS;
    }

    pinMode(PIN_SRI_LD,   OUTPUT);
    pinMode(PIN_SRI_CLK,  OUTPUT);
    pinMode(PIN_SRI_DATA, INPUT);

    digitalWrite(PIN_SRI_LD,  HIGH);
    digitalWrite(PIN_SRI_CLK, LOW);

    ECOSMART_LOGI(TAG,
        "74HC165 cascade: %u chip(s) | LD=%d CLK=%d DATA=%d",
        static_cast<unsigned>(number_of_shift_register),
        PIN_SRI_LD, PIN_SRI_CLK, PIN_SRI_DATA);
}

void InputsRegistryController::begin()
{
    // Configure any GPIO-sourced channels first. In the
    // current hardware all channels come from the 74HC165
    // cascade, so this loop is effectively a no-op.
    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        InputConfig& cfg = inputs_config[i];

        if (!cfg.enabled || cfg.source != InputSource::GPIO) {
            continue;
        }

        if (cfg.pin == INPUT_PIN_NONE) {
            continue;
        }

        const bool isInputOnly = (cfg.pin >= 34 && cfg.pin <= 39);

        if (cfg.hasPullup && !isInputOnly) {
            pinMode(cfg.pin, INPUT_PULLUP);
        } else {
            pinMode(cfg.pin, INPUT);
        }
    }

    initShiftRegisters();

    // Prime the shift-register cache before seeding the state.
    updateShiftRegisterCache();

    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        InputConfig& cfg = inputs_config[i];

        if (!cfg.enabled || cfg.source == InputSource::NONE) {
            continue;
        }

        const bool level = readNormalizedLevel(i);

        inputs_state[i].rawLevel    = level;
        inputs_state[i].stableLevel = level;
        inputs_state[i].ema         = level ? 1.0f : 0.0f;
        inputs_state[i].lastEdgeMs  = millis();

        inputs_value[i] = level;

        ECOSMART_LOGI(TAG,
            "Channel %u initialized: source=%u level=%d",
            static_cast<unsigned>(i),
            static_cast<unsigned>(cfg.source),
            static_cast<int>(level));
    }

    lastPollMs_ = millis();
}

uint8_t InputsRegistryController::shiftIn2(
    uint8_t dataPin,
    uint8_t clockPin,
    uint8_t bitOrder)
{
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (bitOrder == LSBFIRST) {
            value |= digitalRead(dataPin) << i;
        } else {
            value |= digitalRead(dataPin) << (7 - i);
        }

        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
    return value;
}

void InputsRegistryController::updateShiftRegisterCache()
{
    if (number_of_shift_register == 0) {
        return;
    }

    // Pulse the shared parallel-load pin to latch every input
    // bit into the 74HC165 internal shift registers.
    digitalWrite(PIN_SRI_LD, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SRI_LD, HIGH);
    delayMicroseconds(5);

    // Read one byte per chip. Because the chips are cascaded,
    // the first byte read belongs to U5 (channels 8..15) and
    // the second byte belongs to U3 (channels 0..7).
    for (int i = number_of_shift_register - 1; i >= 0; --i) {
        sri_buffer[i] = shiftIn2(PIN_SRI_DATA, PIN_SRI_CLK, MSBFIRST);
    }
}

bool InputsRegistryController::readGpioLevel(uint8_t index) const
{
    const InputConfig& cfg = inputs_config[index];
    if (cfg.pin == INPUT_PIN_NONE) {
        return false;
    }

    const int raw = digitalRead(cfg.pin);
    const bool high = (raw == HIGH);
    return cfg.activeLow ? !high : high;
}

bool InputsRegistryController::readShiftRegLevel(uint8_t index) const
{
    const InputConfig& cfg = inputs_config[index];

    const uint8_t chipIndex = index / 8;           // 0 for 0..7, 1 for 8..15
    // SN74HC165 with MSBFIRST shifts D7 out first and D0 last,
    // so D0 ends up in bit 7 of the byte we read back.
    const uint8_t bitIndex  = 7 - (index % 8);

    if (chipIndex >= number_of_shift_register) {
        return false;
    }

    const bool high = (sri_buffer[chipIndex] >> bitIndex) & 0x01;
    return cfg.activeLow ? !high : high;
}

bool InputsRegistryController::readNormalizedLevel(uint8_t index) const
{
    const InputConfig& cfg = inputs_config[index];

    switch (cfg.source) {
        case InputSource::GPIO:
            return readGpioLevel(index);

        case InputSource::SHIFT_REG:
            return readShiftRegLevel(index);

        default:
            return false;
    }
}

void InputsRegistryController::poll()
{
    const uint32_t now = millis();
    if ((now - lastPollMs_) < INPUT_SAMPLE_INTERVAL_MS) {
        return;
    }
    lastPollMs_ = now;

    // Refresh the shift-register cache once per poll cycle.
    updateShiftRegisterCache();

    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        InputConfig& cfg = inputs_config[i];
        InputState&  st  = inputs_state[i];

        if (!cfg.enabled || cfg.source == InputSource::NONE) {
            continue;
        }

        const bool level = readNormalizedLevel(i);
        st.rawLevel = level;

        // Stage 1: EMA low-pass filter.
        const float rawValue = level ? 1.0f : 0.0f;
        st.ema = INPUT_EMA_ALPHA * rawValue +
                 (1.0f - INPUT_EMA_ALPHA) * st.ema;

        // Stage 2: Schmitt trigger.
        bool newStableLevel = st.stableLevel;

        if (!st.stableLevel && st.ema >= INPUT_SCHMITT_HIGH) {
            newStableLevel = true;
        } else if (st.stableLevel && st.ema <= INPUT_SCHMITT_LOW) {
            newStableLevel = false;
        }

        if (newStableLevel == st.stableLevel) {
            continue;
        }

        if (i == INPUT_MASTER_INDEX &&
            (now - st.lastEdgeMs) < INPUT_MASTER_COOLDOWN_MS) {
            continue;
        }

        applyStableLevel(i, newStableLevel);
    }
}

void InputsRegistryController::applyStableLevel(uint8_t index, bool newLevel)
{
    InputState& st = inputs_state[index];
    const bool previousLevel = st.stableLevel;

    st.stableLevel = newLevel;
    st.lastEdgeMs  = millis();

    if (newLevel != previousLevel) {
        ++st.eventCount;
    }

    const InputConfig& cfg = inputs_config[index];

    switch (cfg.mode) {
        case InputMode::MOMENTARY:
            break;

        case InputMode::TOGGLE:
            st.stableLevel = !previousLevel;
            break;

        case InputMode::PULSE:
            st.stableLevel = false;
            break;

        case InputMode::LATCHED:
            if (newLevel) {
                st.stableLevel = true;
            }
            break;
    }

    inputs_value[index] = st.stableLevel;
    inputs_internal_event_count[index] = st.eventCount;

    ECOSMART_LOGI(TAG,
        "Channel %u stable=%d (prev=%d) ema=%.3f events=%u",
        static_cast<unsigned>(index),
        static_cast<int>(st.stableLevel),
        static_cast<int>(previousLevel),
        static_cast<double>(st.ema),
        static_cast<unsigned>(st.eventCount));

    if (index == INPUT_MASTER_INDEX) {
        if (outputs_ != nullptr) {
            const bool turnOn = st.stableLevel;

            for (size_t j = 0; j < OUTPUTS_NUMBER; ++j) {
                outputs_object[j].value = turnOn;
            }

            outputs_->applyToHardware();

            ECOSMART_LOGI(TAG,
                "MASTER switch -> %s (all outputs %s)",
                turnOn ? "ON" : "OFF",
                turnOn ? "enabled" : "disabled");
        } else {
            ECOSMART_LOGW(TAG,
                "MASTER switch changed but no outputs controller is attached");
        }

        return;
    }

    if (eventHook_ && st.stableLevel != previousLevel) {
        eventHook_(index, st.stableLevel);
    }
}

void InputsRegistryController::forceStableLevel(uint8_t index, bool level)
{
    if (index >= INPUTS_NUMBER) {
        return;
    }

    InputState& st = inputs_state[index];
    st.stableLevel = level;
    st.ema         = level ? 1.0f : 0.0f;
    st.lastEdgeMs  = millis();
    st.eventCount++;

    inputs_value[index] = level;
    inputs_internal_event_count[index] = st.eventCount;
}

std::vector<Registery_t*> InputsRegistryController::getCandidates()
{
    std::vector<Registery_t*> candidates;
    candidates.reserve(INPUTS_NUMBER);

    for (size_t i = 0; i < INPUTS_NUMBER; ++i) {
        candidates.push_back(&reg_module_input.state[i]);
    }

    return candidates;
}

void InputsRegistryController::onWrite(uint16_t regAddr)
{
    for (size_t i = 0; i < INPUTS_NUMBER; ++i) {
        if (reg_module_input.state[i].address == regAddr) {
            const bool s = inputs_value[i];
            inputs_state[i].stableLevel = s;
            inputs_state[i].ema         = s ? 1.0f : 0.0f;
            ECOSMART_LOGI(TAG, "Channel %u state forced -> %d",
                static_cast<unsigned>(i), static_cast<int>(s));
            return;
        }
    }
}