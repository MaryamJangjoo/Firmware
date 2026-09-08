#include "mybus_value_codec.h"
#include <string.h>

bool encodeRegValueString(const String& regVal, MyBusDataType type,
                           uint8_t* outBuf, size_t outCapacity, size_t& outLen)
{
    outLen = 0;
    if (regVal.isEmpty()) return false;

    switch (type) {
        case DT_FLOAT: {
            if (outCapacity < sizeof(float)) return false;
            char* endPtr = nullptr;
            float f = strtof(regVal.c_str(), &endPtr);
            if (endPtr == regVal.c_str() || *endPtr != '\0') return false;
            memcpy(outBuf, &f, sizeof(f));
            outLen = sizeof(f);
            return true;
        }
        case DT_UINT32: {
            if (outCapacity < sizeof(uint32_t)) return false;
            char* endPtr = nullptr;
            unsigned long n = strtoul(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0') return false;
            uint32_t v = static_cast<uint32_t>(n);
            memcpy(outBuf, &v, sizeof(v));
            outLen = sizeof(v);
            return true;
        }
        case DT_INT32: {
            if (outCapacity < sizeof(int32_t)) return false;
            char* endPtr = nullptr;
            long n = strtol(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0') return false;
            int32_t v = static_cast<int32_t>(n);
            memcpy(outBuf, &v, sizeof(v));
            outLen = sizeof(v);
            return true;
        }
        case DT_UINT16: {
            if (outCapacity < sizeof(uint16_t)) return false;
            char* endPtr = nullptr;
            long n = strtol(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0' || n < 0 || n > 0xFFFF) return false;
            uint16_t v = static_cast<uint16_t>(n);
            memcpy(outBuf, &v, sizeof(v));
            outLen = sizeof(v);
            return true;
        }
        case DT_INT16: {
            if (outCapacity < sizeof(int16_t)) return false;
            char* endPtr = nullptr;
            long n = strtol(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0' || n < -32768L || n > 32767L) return false;
            int16_t v = static_cast<int16_t>(n);
            memcpy(outBuf, &v, sizeof(v));
            outLen = sizeof(v);
            return true;
        }
        case DT_UINT8: {
            if (outCapacity < 1) return false;
            char* endPtr = nullptr;
            long n = strtol(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0' || n < 0 || n > 255) return false;
            outBuf[0] = static_cast<uint8_t>(n);
            outLen = 1;
            return true;
        }
        case DT_INT8: {
            if (outCapacity < 1) return false;
            char* endPtr = nullptr;
            long n = strtol(regVal.c_str(), &endPtr, 10);
            if (endPtr == regVal.c_str() || *endPtr != '\0' || n < -128L || n > 127L) return false;
            outBuf[0] = static_cast<uint8_t>(static_cast<int8_t>(n));
            outLen = 1;
            return true;
        }
        case DT_BIT: {
            if (outCapacity < 1) return false;
            bool isTrue = regVal.equalsIgnoreCase("true") || regVal == "1";
            bool isFalse = regVal.equalsIgnoreCase("false") || regVal == "0";
            if (!isTrue && !isFalse) return false;
            outBuf[0] = isTrue ? 1 : 0;
            outLen = 1;
            return true;
        }
        case DT_STRING:
        default: {
            size_t len = min(regVal.length(), outCapacity);
            memcpy(outBuf, regVal.c_str(), len);
            outLen = len;
            return true;
        }
    }
}

void decodeRegValueToJson(JsonDocument& doc, const uint8_t* value,
                           size_t valueLen, MyBusDataType type)
{
    switch (type) {
        case DT_BIT:
            if (valueLen >= 1) doc["value"] = (value[0] != 0);
            break;
        case DT_UINT8:
            if (valueLen >= 1) doc["value"] = value[0];
            break;
        case DT_UINT16:
            if (valueLen >= sizeof(uint16_t)) { uint16_t v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_UINT32:
            if (valueLen >= sizeof(uint32_t)) { uint32_t v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_INT8:
            if (valueLen >= 1) { int8_t v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_INT16:
            if (valueLen >= sizeof(int16_t)) { int16_t v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_INT32:
            if (valueLen >= sizeof(int32_t)) { int32_t v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_FLOAT:
            if (valueLen >= sizeof(float)) { float v; memcpy(&v, value, sizeof(v)); doc["value"] = v; }
            break;
        case DT_STRING:
        case DT_JSON:
        case DT_STRUCT:
        default: {
            String s;
            s.reserve(valueLen + 1);
            for (size_t i = 0; i < valueLen; ++i) s += static_cast<char>(value[i]);
            doc["value"] = s;
            break;
        }
    }
}