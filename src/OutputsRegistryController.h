#ifndef OUTPUTS_REGISTRY_CONTROLLER_H
#define OUTPUTS_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include "RegistryControllerBase.h"

// ============================================================
// OutputsRegistryController
//
// Owns the read/write logic for Digital Input and Digital Output
// registers (state + timer_permanent + timer_sleep) and is the
// ONLY place allowed to write to the 74HC595 shift register
// (applyToHardware). Any other module that wants to toggle a
// bit in outputs_object[] (e.g. the curtain) must go through
// this class to avoid overwriting the entire register.
//
// NOTE: readLocal() also handles input registers, which are not
// part of the base class's candidate list. This is intentional:
// inputs are read-only and don't fit the write-oriented base.
// ============================================================

class OutputsRegistryController : public RegistryControllerBase {
public:
    OutputsRegistryController(int pinLatch, int pinClock, int pinData);

    void begin();

    Registery_t* findOutputEntry(uint16_t regAddr);
    Registery_t* findInputEntry(uint16_t regAddr);

    // Read a local register (output OR input). The base class
    // read() only covers output candidates; this method extends
    // it to also cover input registers.
    bool readLocal(uint16_t regAddr, RawRegisterValue& outValue);

    // Alias for the base class write(), kept for compatibility
    // with the existing call sites.
    bool writeOutput(uint16_t regAddr, const String& regVal)
    {
        return write(regAddr, regVal);
    }

    String getValue(uint16_t regAddr);

    void applyToHardware();

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    int pinLatch_;
    int pinClock_;
    int pinData_;
};

#endif