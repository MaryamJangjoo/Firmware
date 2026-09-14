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

constexpr uint8_t COMMAND_READ_REGISTRY =
    MYBUS_CMD_READ_REGISTRY;  // 0x00

constexpr uint8_t COMMAND_WRITE_REGISTRY =
    MYBUS_CMD_WRITE_REGISTRY; // 0x01

constexpr uint8_t INTERFACE_WIFI =
    1;


constexpr uint8_t COMMAND_WS_STATUS      = 20;
constexpr uint8_t COMMAND_WS_USERS_LIST  = 21;
constexpr uint8_t COMMAND_WS_SITE_INFO   = 22;
constexpr uint8_t COMMAND_WS_WELCOME     = 23;
constexpr uint8_t COMMAND_WS_ERROR       = 24;

constexpr uint8_t REASON_TRANSPORT_ERROR = 1;
constexpr uint8_t REASON_BACKEND_ERROR   = 2;
constexpr uint8_t REASON_NOT_FOUND       = 3;
constexpr uint8_t REASON_BAD_REQUEST     = 4;
constexpr uint8_t REASON_UNKNOWN         = 5;

}

#endif