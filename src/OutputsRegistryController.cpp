#include "OutputsRegistryController.h"

#include "Outputs.hpp"
#include "inputs.hpp"
#include "Logging.h"

static const char* TAG = "OUTPUTS";

OutputsRegistryController::OutputsRegistryController(
    int pinLatch,
    int pinClock,
    int pinData)
    : pinLatch_(pinLatch),
      pinClock_(pinClock),
      pinData_(pinData)
{
}

void OutputsRegistryController::begin()
{
    pinMode(pinLatch_, OUTPUT);
    pinMode(pinClock_, OUTPUT);
    pinMode(pinData_, OUTPUT);

    digitalWrite(pinLatch_, LOW);
    digitalWrite(pinClock_, LOW);
    digitalWrite(pinData_, LOW);
}

std::vector<Registery_t*> OutputsRegistryController::getCandidates()
{
    std::vector<Registery_t*> candidates;
    candidates.reserve(OUTPUTS_NUMBER * 3);

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        candidates.push_back(&reg_module_output.state[i]);
        candidates.push_back(&reg_module_output.timer_permanent[i]);
        candidates.push_back(&reg_module_output.timer_sleep[i]);
    }

    return candidates;
}

Registery_t* OutputsRegistryController::findOutputEntry(uint16_t regAddr)
{
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.state[i].address == regAddr) {
            return &reg_module_output.state[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_permanent[i].address == regAddr) {
            return &reg_module_output.timer_permanent[i];
        }
    }
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (reg_module_output.timer_sleep[i].address == regAddr) {
            return &reg_module_output.timer_sleep[i];
        }
    }
    return nullptr;
}

Registery_t* OutputsRegistryController::findInputEntry(uint16_t regAddr)
{
    for (size_t i = 0; i < INPUTS_NUMBER; i++) {
        if (reg_module_input.state[i].address == regAddr) {
            return &reg_module_input.state[i];
        }
    }
    return nullptr;
}

bool OutputsRegistryController::readLocal(
    uint16_t regAddr,
    RawRegisterValue& outValue)
{
    // First try the output candidates via the base class logic.
    if (read(regAddr, outValue)) {
        return true;
    }

    // Fall back to the input registers (read-only).
    Registery_t* entry = findInputEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = false;

    switch (entry->datatype) {
        case reg_datatype_bit:
            outValue.bytes[0] = (*static_cast<bool*>(entry->ref)) ? 1 : 0;
            outValue.byteLen = 1;
            break;
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

String OutputsRegistryController::getValue(uint16_t regAddr)
{
    RawRegisterValue rv;

    if (!readLocal(regAddr, rv)) {
        return "";
    }

    if (rv.isString) {
        return rv.stringValue;
    }

    switch (rv.datatype) {
        case reg_datatype_bit:
            return String(rv.bytes[0] != 0 ? 1 : 0);
        case reg_datatype_uint8:
            return String(rv.bytes[0]);
        case reg_datatype_uint16: {
            uint16_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_uint32: {
            uint32_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int8: {
            int8_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int16: {
            int16_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_int32: {
            int32_t v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v);
        }
        case reg_datatype_float: {
            float v; memcpy(&v, rv.bytes, sizeof(v));
            return String(v, 6);
        }
        default:
            return "";
    }
}

void OutputsRegistryController::onWrite(uint16_t regAddr)
{
    Registery_t* entry = findOutputEntry(regAddr);
    if (entry == nullptr) {
        return;
    }

    // Detect which array the entry belongs to.
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.state[i] == entry) {
            applyToHardware();
            return;
        }
    }

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.timer_permanent[i] == entry) {
            ECOSMART_LOGI(TAG, "timer_permanent[%zu] stored (metadata only)", i);
            return;
        }
    }

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.timer_sleep[i] == entry) {
            ECOSMART_LOGI(TAG, "timer_sleep[%zu] stored (metadata only)", i);
            return;
        }
    }
}

void OutputsRegistryController::applyToHardware()
{
    // Hardware mapping (confirmed by testing):
    //   - When byteLow is shifted out first and byteHigh second,
    //     the logical output index maps directly to the physical
    //     LED/relay position:
    //       output 0..9   -> LED 1..10 (with output 0 also driving a relay)
    //       output 14..15 -> curtain
    //   - The previously tried "byteHigh first" order scrambled
    //     the mapping, so it has been reverted.
    uint8_t byteLow  = 0;
    uint8_t byteHigh = 0;

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (outputs_object[i].value) {
            if (i < 8) {
                byteLow |= (1U << i);
            } else {
                byteHigh |= (1U << (i - 8));
            }
        }
    }

    digitalWrite(pinLatch_, LOW);
    shiftOut(pinData_, pinClock_, MSBFIRST, byteHigh);
    shiftOut(pinData_, pinClock_, MSBFIRST, byteLow);
    digitalWrite(pinLatch_, HIGH);
}