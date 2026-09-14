#include "OutputsRegistryController.h"
#include "Outputs.hpp"
#include "inputs.hpp"
#include "mybus_value_codec.h"

OutputsRegistryController::OutputsRegistryController(
    int pinLatch, int pinClock, int pinData)
    : pinLatch_(pinLatch), pinClock_(pinClock), pinData_(pinData)
{
}

void OutputsRegistryController::begin()
{
    pinMode(pinLatch_, OUTPUT);
    pinMode(pinClock_, OUTPUT);
    pinMode(pinData_, OUTPUT);
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

bool OutputsRegistryController::readLocal(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findOutputEntry(regAddr);

    if (entry == nullptr) {
        entry = findInputEntry(regAddr);
    }

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
        case reg_datatype_uint32: {
            uint32_t v = *static_cast<uint32_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int8: {
            int8_t v = *static_cast<int8_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int16: {
            int16_t v = *static_cast<int16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_int32: {
            int32_t v = *static_cast<int32_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        case reg_datatype_float: {
            float v = *static_cast<float*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        default:
            return false;
    }

    return true;
}

bool OutputsRegistryController::writeOutput(uint16_t regAddr, const String& regVal)
{
    Serial.printf("[REG] writeOutput called: 0x%04X = '%s'\n",
                  regAddr, regVal.c_str());

    Registery_t* entry = findOutputEntry(regAddr);

    if (entry == nullptr || entry->ref == nullptr) {
        Serial.printf("[REG] Entry not found for 0x%04X\n", regAddr);
        return false;
    }

    // Classify the entry: state, timer_permanent, or timer_sleep.
    enum class EntryKind { NONE, STATE, TIMER_PERMANENT, TIMER_SLEEP };
    EntryKind kind = EntryKind::NONE;
    size_t index = 0;

    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        if (&reg_module_output.state[i] == entry) {
            kind = EntryKind::STATE;
            index = i;
            break;
        }
        if (&reg_module_output.timer_permanent[i] == entry) {
            kind = EntryKind::TIMER_PERMANENT;
            index = i;
            break;
        }
        if (&reg_module_output.timer_sleep[i] == entry) {
            kind = EntryKind::TIMER_SLEEP;
            index = i;
            break;
        }
    }

    if (kind == EntryKind::NONE) {
        Serial.printf("[REG] Not an output/timer register: 0x%04X\n", regAddr);
        return false;
    }

    if (entry->datatype > reg_datatype_float) {
        Serial.printf("[REG] Unsupported datatype for 0x%04X\n", regAddr);
        return false;
    }

    uint8_t buf[8];
    size_t len = 0;

    if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                               buf, sizeof(buf), len)) {
        Serial.printf("[REG] Failed to parse '%s'\n", regVal.c_str());
        return false;
    }

    if (len != entry->size) {
        Serial.printf("[REG] Size mismatch for 0x%04X\n", regAddr);
        return false;
    }

    memcpy(entry->ref, buf, len);
    Serial.printf("[REG] 0x%04X written\n", regAddr);

    switch (kind) {
        case EntryKind::STATE:
            applyToHardware();
            break;

        case EntryKind::TIMER_PERMANENT:
            Serial.printf("[REG] timer_permanent[%zu] stored (metadata only)\n", index);
            break;

        case EntryKind::TIMER_SLEEP:
            Serial.printf("[REG] timer_sleep[%zu] stored (metadata only)\n", index);
            break;

        default:
            break;
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

void OutputsRegistryController::applyToHardware()
{
    Serial.println("!!! 🔄 applyToHardware CALLED !!!");

    uint8_t byteLow = 0;
    uint8_t byteHigh = 0;

    Serial.println("[OUTPUTS] Current states:");
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++) {
        Serial.printf("  [%zu] = %d (addr=0x%04X)\n",
                      i, outputs_object[i].value,
                      reg_module_output.state[i].address);
        if (outputs_object[i].value) {
            if (i < 8) {
                byteLow |= (1U << i);
            } else {
                byteHigh |= (1U << (i - 8));
            }
        }
    }

    Serial.printf("[OUTPUTS] Sending: byteHigh=0x%02X, byteLow=0x%02X\n", byteHigh, byteLow);

    digitalWrite(pinLatch_, LOW);
    shiftOut(pinData_, pinClock_, LSBFIRST, byteLow);
    shiftOut(pinData_, pinClock_, LSBFIRST, byteHigh);
    digitalWrite(pinLatch_, HIGH);

    Serial.println("[OUTPUTS] ✅ Shift register updated");
}