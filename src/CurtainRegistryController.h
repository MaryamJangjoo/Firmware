#ifndef CURTAIN_REGISTRY_CONTROLLER_H
#define CURTAIN_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include "RegistryControllerBase.h"
#include "OutputsRegistryController.h"

// ============================================================
// CurtainRegistryController
//
// Controls the motorized curtain via two dedicated outputs:
//   index 14 (0x800E) -> Open channel
//   index 15 (0x800F) -> Close channel
//
// These indices are defined in ecosmart_registeries.h as
// CURTAIN_OUTPUT_OPEN and CURTAIN_OUTPUT_CLOSE.
// ============================================================

class CurtainRegistryController : public RegistryControllerBase {
public:
    explicit CurtainRegistryController(OutputsRegistryController& outputs);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    OutputsRegistryController& outputs_;
};

#endif