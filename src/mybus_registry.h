#ifndef MYBUS_REGISTRY_H
#define MYBUS_REGISTRY_H

#include <Arduino.h>
#include "mybus_frame.h"  // ← استفاده از enum از mybus_frame.h

// ============================================================
// Registry Address Encoding/Decoding
// ============================================================

inline uint16_t encodeRegistryAddress(
    bool isWrite,
    bool isSystem,
    bool isArray,
    MyBusDataType dataType,  // ← از mybus_frame.h استفاده می‌کند
    uint8_t address
) {
    uint16_t regAdd = 0;

    regAdd |= (isWrite ? 1U : 0U) << 15;
    regAdd |= (isSystem ? 1U : 0U) << 14;
    regAdd |= (isArray ? 1U : 0U) << 13;
    regAdd |= (static_cast<uint16_t>(dataType) & 0x0F) << 8;
    regAdd |= (static_cast<uint16_t>(address) & 0xFF);

    return regAdd;
}

inline void decodeRegistryAddress(
    uint16_t regAdd,
    bool& isWrite,
    bool& isSystem,
    bool& isArray,
    MyBusDataType& dataType,
    uint8_t& address
) {
    isWrite = ((regAdd >> 15) & 0x01) != 0;
    isSystem = ((regAdd >> 14) & 0x01) != 0;
    isArray = ((regAdd >> 13) & 0x01) != 0;
    dataType = static_cast<MyBusDataType>((regAdd >> 8) & 0x0F);
    address = regAdd & 0xFF;
}

#endif