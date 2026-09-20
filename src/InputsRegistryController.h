#ifndef INPUTS_REGISTRY_CONTROLLER_H
#define INPUTS_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <functional>
#include <vector>

#include "RegistryControllerBase.h"
#include "inputs.hpp"

// ============================================================
// InputsRegistryController
//
// Owns the physical GPIO scanning and debouncing for the
// digital input channels, and exposes the input registers to
// the registry framework (WS reads, cloud reads).
//
// Each channel is described by an InputConfig (pin, mode,
// activeLow, enabled, debounce) and produces a debounced
// stable level plus an event counter in InputState.
//
// The controller cooperates with OutputsRegistryController:
// when the channel at INPUT_MASTER_INDEX changes state, every
// output is driven to the new state via applyToHardware().
//
// Polling model:
//   - begin()  configures each enabled GPIO with the correct
//              pull mode and initializes the debounce state.
//   - poll()   must be called from the main loop. It returns
//              immediately if fewer than INPUT_POLL_INTERVAL_MS
//              have elapsed since the previous scan.
// ============================================================

class OutputsRegistryController;

class InputsRegistryController : public RegistryControllerBase {
public:
    // Called for every debounced transition of a non-master
    // input. The hook is NOT invoked for the master channel,
    // which is handled internally.
    using InputEventHook = std::function<void(uint8_t index, bool newLevel)>;

    InputsRegistryController();

    // Optional late binding to the outputs controller. If left
    // unset, the master channel will still update inputs_value[]
    // but will not be able to drive the outputs.
    void attachOutputs(OutputsRegistryController* outputs);

    // Optional hook for input events (non-master channels).
    void onInputEvent(InputEventHook hook) { eventHook_ = hook; }

    // Configure GPIOs and initialize debounce state.
    void begin();

    // Scan inputs. Must be called regularly from the main loop.
    void poll();

    // Explicitly set a channel's stable level without touching
    // the GPIO (used by tests, TOGGLE mode, LATCHED reset, ...).
    void forceStableLevel(uint8_t index, bool level);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    // Read the physical level of a channel, normalized to a
    // logical level (true == "active").
    bool readNormalizedLevel(uint8_t index) const;

    // Apply a new debounced level to a channel and dispatch
    // side effects (master switch, event hook).
    void applyStableLevel(uint8_t index, bool newLevel);

    OutputsRegistryController* outputs_ = nullptr;
    InputEventHook             eventHook_;

    uint32_t lastPollMs_ = 0;
};

#endif
