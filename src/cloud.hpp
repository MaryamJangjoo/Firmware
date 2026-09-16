#pragma once
#include <Arduino.h>

// ============================================================
// CloudObject
//
// Mirrors the Cloud Connectivity registers documented in the
// EcoSmart Registries Map:
//
//   0xC800  Server FQDN   string   R/W   SYS
//   0xC801  Server IP     string   R/W   SYS
//   0xC200  Server Port   u16      R/W   SYS
//   0x4800  Device ID     string   R     SYS
//   0xC802  Username      string   R/W   SYS
//   0xC803  Password      string   R/W   SYS
//
// The struct itself is the process image that CloudRegistryStore
// binds into. Values are persisted to NVS by the store.
// ============================================================

struct CloudObject
{
    String   server_fqdn;
    String   server_ip;
    uint16_t server_port   = 3000;
    String   device_id;
    String   username;
    String   password;
};

extern CloudObject cloud_object;