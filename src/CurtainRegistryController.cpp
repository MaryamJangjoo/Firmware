#include "CurtainRegistryController.h"

#include "curtain.hpp"
#include "Outputs.hpp"

CurtainRegistryController::CurtainRegistryController(
    OutputsRegistryController& outputs,
    size_t outputIndex)
    : outputs_(outputs),
      outputIndex_(outputIndex)
{
}

std::vector<Registery_t*> CurtainRegistryController::getCandidates()
{
    return {
        &reg_module_curtain.state,
        &reg_module_curtain.timer_permanent,
    };
}

void CurtainRegistryController::onWrite(uint16_t regAddr)
{
    if (regAddr == REG_ADD_CURTAIN_STATE) {
        // TODO: outputIndex_ must be confirmed from the schematic
        // before connecting to the real curtain motor.
        outputs_object[outputIndex_].value = (curtain_object.state != 0);
        outputs_.applyToHardware();

        Serial.printf("[CURTAIN] State -> %s (output index %zu)\n",
                      curtain_object.state ? "OPEN" : "CLOSE",
                      outputIndex_);
    } else if (regAddr == REG_ADD_CURTAIN_PERMANENT_TIMER) {
        Serial.printf("[CURTAIN] Timer stored: %u (metadata only)\n",
                      curtain_object.timer_permanent);
    }
}