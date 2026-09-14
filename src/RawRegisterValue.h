#ifndef RAW_REGISTER_VALUE_H
#define RAW_REGISTER_VALUE_H

#include <Arduino.h>
#include "ecosmart_registeries.h"

struct RawRegisterValue {
    Reg_DataType_t datatype = reg_datatype_uint8;
    bool isString = false;
    String stringValue;
    uint8_t bytes[8] = {0};
    size_t byteLen = 0;
};

#endif