#ifndef REGISTRY_CONTROLLER_BASE_H
#define REGISTRY_CONTROLLER_BASE_H

#include <Arduino.h>
#include <vector>

#include "ecosmart_registeries.h"
#include "RawRegisterValue.h"
#include "mybus_value_codec.h"

class RegistryControllerBase {
public:
    virtual ~RegistryControllerBase() = default;

    Registery_t* findEntry(uint16_t regAddr)
    {
        const std::vector<Registery_t*> candidates = getCandidates();
        for (auto* entry : candidates) {
            if (entry != nullptr && entry->address == regAddr) {
                return entry;
            }
        }
        return nullptr;
    }

    // Marked virtual so controllers with special read semantics
    // (e.g. masking secrets) can override the default behavior.
    virtual bool read(uint16_t regAddr, RawRegisterValue& outValue)
    {
        Registery_t* entry = findEntry(regAddr);
        if (entry == nullptr || entry->ref == nullptr) {
            return false;
        }

        outValue.datatype = entry->datatype;
        outValue.isString = entry->isString;

        if (entry->isString) {
            outValue.stringValue = *static_cast<String*>(entry->ref);
            outValue.byteLen = 0;
            return true;
        }

        switch (entry->datatype) {
            case reg_datatype_bit: {
                outValue.bytes[0] = (*static_cast<bool*>(entry->ref)) ? 1 : 0;
                outValue.byteLen = 1;
                break;
            }
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

    bool write(uint16_t regAddr, const String& regVal)
    {
        Registery_t* entry = findEntry(regAddr);
        if (entry == nullptr || entry->ref == nullptr) {
            return false;
        }

        if (!entry->writable) {
            Serial.printf("[REG] 0x%04X is read-only\n", regAddr);
            return false;
        }

        if (entry->isString) {
            *static_cast<String*>(entry->ref) = regVal;
            onWrite(regAddr);
            return true;
        }

        uint8_t buf[8];
        size_t len = 0;

        if (!encodeRegValueString(
                regVal,
                static_cast<MyBusDataType>(entry->datatype),
                buf,
                sizeof(buf),
                len)) {
            Serial.printf("[REG] Failed to parse '%s'\n", regVal.c_str());
            return false;
        }

        if (len != entry->size) {
            Serial.printf("[REG] Size mismatch at 0x%04X\n", regAddr);
            return false;
        }

        memcpy(entry->ref, buf, len);

        onWrite(regAddr);

        return true;
    }

protected:
    virtual std::vector<Registery_t*> getCandidates() = 0;

    virtual void onWrite(uint16_t regAddr)
    {
        (void)regAddr;
    }
};

#endif