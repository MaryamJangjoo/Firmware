#ifndef REGISTER_RAW_VALUE_H
#define REGISTER_RAW_VALUE_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

enum class RegRawType : uint8_t {
    BIT = 0,
    UINT8,
    UINT16,
    UINT32,
    INT8,
    INT16,
    INT32,
    FLOAT,
    STRING
};

struct RegisterRawValue {
    RegRawType type = RegRawType::UINT8;
    String stringValue;      
    uint8_t bytes[8] = {0};  
    size_t byteLen = 0;

    bool isString() const { return type == RegRawType::STRING; }
};

#endif