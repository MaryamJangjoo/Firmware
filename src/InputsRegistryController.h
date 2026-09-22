#ifndef INPUTS_REGISTRY_CONTROLLER_H
#define INPUTS_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <functional>
#include <vector>

#include "RegistryControllerBase.h"
#include "inputs.hpp"

class OutputsRegistryController;

class InputsRegistryController : public RegistryControllerBase {
public:
    using InputEventHook = std::function<void(uint8_t index, bool newLevel)>;

    InputsRegistryController();

    void attachOutputs(OutputsRegistryController* outputs);
    void onInputEvent(InputEventHook hook) { eventHook_ = hook; }

    void begin();
    void poll();

    void forceStableLevel(uint8_t index, bool level);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    bool readGpioLevel(uint8_t index) const;

    void     initShiftRegisters();
    void     updateShiftRegisterCache();
    uint8_t  shiftIn2(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder);
    bool     readShiftRegLevel(uint8_t index) const;

    bool readNormalizedLevel(uint8_t index) const;

    void applyStableLevel(uint8_t index, bool newLevel);

    OutputsRegistryController* outputs_ = nullptr;
    InputEventHook             eventHook_;

    uint32_t lastPollMs_ = 0;

    uint8_t sri_buffer[MAX_SHIFT_REGISTERS];
    uint8_t number_of_shift_register = 0;
};

#endif