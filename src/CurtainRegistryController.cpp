#include "CurtainRegistryController.h"
#include "curtain.hpp"
#include "Outputs.hpp"
#include "mybus_value_codec.h"

CurtainRegistryController::CurtainRegistryController(
    OutputsRegistryController& outputs, size_t outputIndex)
    : outputs_(outputs), outputIndex_(outputIndex)
{
}

Registery_t* CurtainRegistryController::findEntry(uint16_t regAddr)
{
    Registery_t* candidates[] = {
        &reg_module_curtain.state,
        &reg_module_curtain.timer_permanent
    };

    for (auto* entry : candidates) {
        if (entry->address == regAddr) {
            return entry;
        }
    }
    return nullptr;
}

bool CurtainRegistryController::read(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = false;

    switch (entry->datatype) {
        case reg_datatype_uint8:
            outValue.bytes[0] = *static_cast<uint8_t*>(entry->ref);
            outValue.byteLen = sizeof(uint8_t);
            break;
        case reg_datatype_uint16: {
            uint16_t v = *static_cast<uint16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        default:
            return false;
    }

    return true;
}

bool CurtainRegistryController::write(uint16_t regAddr, const String& regVal)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    if (!entry->writable) {
        Serial.printf("[CURTAIN] ❌ 0x%04X read-only\n", regAddr);
        return false;
    }

    uint8_t buf[8];
    size_t len = 0;

    if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                               buf, sizeof(buf), len)) {
        Serial.printf("[CURTAIN] ❌ Parse failed: '%s'\n", regVal.c_str());
        return false;
    }

    if (len != entry->size) {
        Serial.printf("[CURTAIN] ❌ Size mismatch at 0x%04X\n", regAddr);
        return false;
    }

    memcpy(entry->ref, buf, len);

    if (regAddr == REG_ADD_CURTAIN_STATE) {
        // ⚠️⚠️ outputIndex_ هنوز از schematic تایید نشده.
        outputs_object[outputIndex_].value = (curtain_object.state != 0);
        outputs_.applyToHardware();

        Serial.printf("[CURTAIN] State -> %s (output index %zu)\n",
                      curtain_object.state ? "OPEN" : "CLOSE",
                      outputIndex_);
    } else if (regAddr == REG_ADD_CURTAIN_PERMANENT_TIMER) {
        Serial.printf("[CURTAIN] Timer stored: %u (metadata only)\n",
                      curtain_object.timer_permanent);
    }

    return true;
}