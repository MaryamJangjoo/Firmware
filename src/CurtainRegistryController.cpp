#include "CurtainRegistryController.h"

#include "curtain.hpp"
#include "Outputs.hpp"

CurtainRegistryController::CurtainRegistryController(
    OutputsRegistryController& outputs)
    : outputs_(outputs)
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
        // The curtain is driven by two complementary outputs:
        //   Open  -> index 14 (CURTAIN_OUTPUT_OPEN)
        //   Close -> index 15 (CURTAIN_OUTPUT_CLOSE)
        //
        // Both outputs are written on every state change so the
        // motor is never left energized in both directions at once.
        const bool open = (curtain_object.state != 0);

        outputs_object[CURTAIN_OUTPUT_OPEN].value  = open;
        outputs_object[CURTAIN_OUTPUT_CLOSE].value = !open;

        outputs_.applyToHardware();

        Serial.printf("[CURTAIN] State -> %s (open=%d, close=%d)\n",
                      open ? "OPEN" : "CLOSE",
                      static_cast<int>(open),
                      static_cast<int>(!open));
    } else if (regAddr == REG_ADD_CURTAIN_PERMANENT_TIMER) {
        Serial.printf("[CURTAIN] Timer stored: %u (metadata only)\n",
                      curtain_object.timer_permanent);
    }
}