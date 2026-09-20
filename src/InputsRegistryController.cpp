#include "InputsRegistryController.h"

#include <Arduino.h>

#include "OutputsRegistryController.h"
#include "Outputs.hpp"
#include "Logging.h"

static const char* TAG = "INPUTS";

// ============================================================
// Default channel configuration
//
// Only four ESP32-WROOM GPIOs are safe here: 34, 35, 36 and 39.
// GPIO 34 and 35 are wired to the I2S bus in AppController.h
// (PIN_I2S_FAULT and PIN_I2S_SDIN) and are therefore marked
// disabled by default. GPIO 36 and 39 are input-only with no
// internal pull-up, so an external pull-up to 3V3 is required
// for a traditional push-button to GND. Without one, the
// software-only EMA + Schmitt filter below is what keeps the
// input usable.
//
// All other channels are left unwired (pin == INPUT_PIN_NONE).
// ============================================================

namespace {

InputConfig makeDefaultConfig(uint8_t index)
{
    InputConfig cfg;

    switch (index) {
        case 0:
            // Master switch (traditional push-button to GND).
            // GPIO 36 has no internal pull-up, so the software
            // filter stack must do all the work.
            cfg.pin        = 36;
            cfg.mode       = InputMode::MOMENTARY;
            cfg.activeLow  = true;
            cfg.enabled    = true;
            cfg.hasPullup  = false;
            break;

        case 1:
            // Secondary input, also a traditional push-button.
            // GPIO 39 has no internal pull-up either.
            cfg.pin        = 39;
            cfg.mode       = InputMode::MOMENTARY;
            cfg.activeLow  = true;
            cfg.enabled    = true;
            cfg.hasPullup  = false;
            break;

        case 2:
            // Reserved for a future sensor on GPIO 34 (I2S
            // FAULT pin in the current hardware). Disabled by
            // default to avoid disturbing the amplifier.
            cfg.pin        = 34;
            cfg.mode       = InputMode::MOMENTARY;
            cfg.activeLow  = true;
            cfg.enabled    = false;
            cfg.hasPullup  = false;
            break;

        case 3:
            // Reserved for a future sensor on GPIO 35 (I2S
            // SDIN pin in the current hardware). Disabled by
            // default.
            cfg.pin        = 35;
            cfg.mode       = InputMode::MOMENTARY;
            cfg.activeLow  = true;
            cfg.enabled    = false;
            cfg.hasPullup  = false;
            break;

        default:
            // Unwired channel.
            cfg.pin     = INPUT_PIN_NONE;
            cfg.enabled = false;
            break;
    }

    return cfg;
}

}  // namespace

InputsRegistryController::InputsRegistryController()
{
    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        inputs_config[i] = makeDefaultConfig(i);
        inputs_state[i]  = InputState{};
        inputs_value[i]  = false;

        inputs_mode_value[i]        = static_cast<uint8_t>(inputs_config[i].mode);
        inputs_enabled_value[i]     = inputs_config[i].enabled ? 1 : 0;
        inputs_event_count_value[i] = 0;
    }
}

void InputsRegistryController::attachOutputs(OutputsRegistryController* outputs)
{
    outputs_ = outputs;
}

void InputsRegistryController::begin()
{
    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        InputConfig& cfg = inputs_config[i];

        if (!cfg.enabled || cfg.pin == INPUT_PIN_NONE) {
            continue;
        }

        // GPIO 34..39 are input-only and have no internal
        // pull-up or pull-down. Passing INPUT_PULLUP on those
        // pins is harmless but has no effect, so we skip it.
        const bool isInputOnly =
            (cfg.pin >= 34 && cfg.pin <= 39);

        if (cfg.hasPullup && !isInputOnly) {
            pinMode(cfg.pin, INPUT_PULLUP);
        } else {
            pinMode(cfg.pin, INPUT);
        }

        const bool level = readNormalizedLevel(i);

        inputs_state[i].rawLevel    = level;
        inputs_state[i].stableLevel = level;
        inputs_state[i].ema         = level ? 1.0f : 0.0f;
        inputs_state[i].lastEdgeMs  = millis();

        inputs_value[i] = level;

        ECOSMART_LOGI(TAG,
            "Channel %u initialized: pin=%d activeLow=%d pullup=%d level=%d",
            static_cast<unsigned>(i),
            cfg.pin,
            static_cast<int>(cfg.activeLow),
            static_cast<int>(cfg.hasPullup),
            static_cast<int>(level));
    }

    lastPollMs_ = millis();
}

bool InputsRegistryController::readNormalizedLevel(uint8_t index) const
{
    const InputConfig& cfg = inputs_config[index];
    if (cfg.pin == INPUT_PIN_NONE) {
        return false;
    }

    const int raw = digitalRead(cfg.pin);
    const bool high = (raw == HIGH);
    return cfg.activeLow ? !high : high;
}

void InputsRegistryController::poll()
{
    const uint32_t now = millis();
    if ((now - lastPollMs_) < INPUT_SAMPLE_INTERVAL_MS) {
        return;
    }
    lastPollMs_ = now;

    for (uint8_t i = 0; i < INPUTS_NUMBER; ++i) {
        InputConfig& cfg = inputs_config[i];
        InputState&  st  = inputs_state[i];

        if (!cfg.enabled || cfg.pin == INPUT_PIN_NONE) {
            continue;
        }

        const bool level = readNormalizedLevel(i);
        st.rawLevel = level;

        // --------------------------------------------------------
        // Stage 1: EMA low-pass filter
        //
        // filtered = alpha * raw + (1 - alpha) * filtered
        //
        // With alpha = 0.05 and 5 ms sampling, the effective
        // time constant is about 100 ms. Short RF bursts are
        // strongly attenuated; a real button press still passes
        // through within a fraction of a second.
        // --------------------------------------------------------

        const float rawValue = level ? 1.0f : 0.0f;
        st.ema = INPUT_EMA_ALPHA * rawValue +
                 (1.0f - INPUT_EMA_ALPHA) * st.ema;

        // --------------------------------------------------------
        // Stage 2: Schmitt trigger
        //
        // Flip stableLevel only when the filtered value crosses
        // one of the two thresholds. The dead band in between
        // prevents the output from toggling on borderline noise.
        // --------------------------------------------------------

        bool newStableLevel = st.stableLevel;

        if (!st.stableLevel && st.ema >= INPUT_SCHMITT_HIGH) {
            newStableLevel = true;
        } else if (st.stableLevel && st.ema <= INPUT_SCHMITT_LOW) {
            newStableLevel = false;
        }

        if (newStableLevel == st.stableLevel) {
            continue;
        }

        // --------------------------------------------------------
        // Master cooldown
        //
        // The master switch is expensive (full shift register
        // rewrite) and visually disruptive. Space its
        // transitions out.
        // --------------------------------------------------------

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

    // ---- Mode-specific side effects ----

    const InputConfig& cfg = inputs_config[index];

    switch (cfg.mode) {
        case InputMode::MOMENTARY:
            // stableLevel already reflects the new level.
            break;

        case InputMode::TOGGLE:
            // The stable level should mirror an internal latch
            // that flips on every edge.
            st.stableLevel = !previousLevel;
            break;

        case InputMode::PULSE:
            // Report the event but immediately clear the
            // stable level again so the next edge is treated
            // as a fresh pulse.
            st.stableLevel = false;
            break;

        case InputMode::LATCHED:
            // Keep the level latched HIGH once it has been
            // observed HIGH. A reset requires an explicit
            // write of 0 to the state register.
            if (newLevel) {
                st.stableLevel = true;
            }
            break;
    }

    inputs_value[index] = st.stableLevel;

    // Mirror the event counter into the registry process image.
    inputs_event_count_value[index] = st.eventCount;

    ECOSMART_LOGI(TAG,
        "Channel %u stable=%d (prev=%d) ema=%.3f events=%u",
        static_cast<unsigned>(index),
        static_cast<int>(st.stableLevel),
        static_cast<int>(previousLevel),
        static_cast<double>(st.ema),
        static_cast<unsigned>(st.eventCount));

    // ---- Master switch ----

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

    // ---- Non-master event hook ----

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
    inputs_event_count_value[index] = st.eventCount;
}

std::vector<Registery_t*> InputsRegistryController::getCandidates()
{
    std::vector<Registery_t*> candidates;
    candidates.reserve(INPUTS_NUMBER * 4);

    for (size_t i = 0; i < INPUTS_NUMBER; ++i) {
        candidates.push_back(&reg_module_input.state[i]);
        candidates.push_back(&reg_module_input.mode[i]);
        candidates.push_back(&reg_module_input.enabled[i]);
        candidates.push_back(&reg_module_input.event_count[i]);
    }

    return candidates;
}

void InputsRegistryController::onWrite(uint16_t regAddr)
{
    // The registry layer has already written the new value into
    // the corresponding process-image mirror (inputs_mode_value,
    // inputs_enabled_value, or inputs_value). This callback is
    // only responsible for reflecting that change back into the
    // controller's internal configuration and, when needed,
    // reconfiguring the GPIO.

    for (size_t i = 0; i < INPUTS_NUMBER; ++i) {
        if (reg_module_input.mode[i].address == regAddr) {
            const uint8_t m = inputs_mode_value[i];

            inputs_config[i].mode =
                (m <= static_cast<uint8_t>(InputMode::LATCHED))
                    ? static_cast<InputMode>(m)
                    : InputMode::MOMENTARY;

            ECOSMART_LOGI(TAG, "Channel %u mode -> %u",
                static_cast<unsigned>(i), static_cast<unsigned>(m));
            return;
        }

        if (reg_module_input.enabled[i].address == regAddr) {
            const bool enable = (inputs_enabled_value[i] != 0);
            inputs_config[i].enabled = enable;

            if (enable) {
                const InputConfig& cfg = inputs_config[i];
                if (cfg.pin != INPUT_PIN_NONE) {
                    const bool isInputOnly =
                        (cfg.pin >= 34 && cfg.pin <= 39);

                    if (cfg.hasPullup && !isInputOnly) {
                        pinMode(cfg.pin, INPUT_PULLUP);
                    } else {
                        pinMode(cfg.pin, INPUT);
                    }
                }
            }

            ECOSMART_LOGI(TAG, "Channel %u enabled -> %d",
                static_cast<unsigned>(i), static_cast<int>(enable));
            return;
        }

        if (reg_module_input.state[i].address == regAddr) {
            const bool s = inputs_value[i];

            // LATCHED channels can be cleared by writing 0 to
            // the state register.
            if (inputs_config[i].mode == InputMode::LATCHED && !s) {
                forceStableLevel(i, false);
                ECOSMART_LOGI(TAG, "Channel %u LATCHED reset",
                    static_cast<unsigned>(i));
            } else {
                inputs_state[i].stableLevel = s;
                inputs_state[i].ema         = s ? 1.0f : 0.0f;
                ECOSMART_LOGI(TAG, "Channel %u state forced -> %d",
                    static_cast<unsigned>(i), static_cast<int>(s));
            }
            return;
        }
    }
}
