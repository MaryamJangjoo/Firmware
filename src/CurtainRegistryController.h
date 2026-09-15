#ifndef CURTAIN_REGISTRY_CONTROLLER_H
#define CURTAIN_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include "RegistryControllerBase.h"
#include "OutputsRegistryController.h"

class CurtainRegistryController : public RegistryControllerBase {
public:
    CurtainRegistryController(
        OutputsRegistryController& outputs,
        size_t outputIndex);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    OutputsRegistryController& outputs_;
    size_t outputIndex_;
};

#endif 