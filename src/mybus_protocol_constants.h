#ifndef MYBUS_PROTOCOL_CONSTANTS_H
#define MYBUS_PROTOCOL_CONSTANTS_H

#include <stdint.h>

#include "mybus_frame.h"

namespace mybus_proto {

constexpr uint8_t QOS_DEFAULT =
    0;

constexpr uint8_t OPTIONS_DEFAULT =
    0;

constexpr uint8_t FLAG_REQUEST =
    0;

constexpr uint8_t SECURITY_HANDSHAKE =
    1;

constexpr uint8_t SECURITY_ENCRYPTED =
    2;

constexpr uint8_t COMPRESSION_NONE =
    0;

constexpr uint8_t COMMAND_HANDSHAKE =
    250;

constexpr uint8_t COMMAND_REGISTRY =
    2;

constexpr uint8_t INTERFACE_WIFI =
    1;

} // namespace mybus_proto

#endif