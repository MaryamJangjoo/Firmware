#ifndef ECOSMART_REGISTRIES_H
#define ECOSMART_REGISTRIES_H

#include "mybus_registry.h"

// ============================================================
// ⚠️ اصلاحیه: در سند اصلی "Audio Title/Artist" روی آدرس‌های 0x0700/0x0701
// با TYP=string بودند، ولی طبق طرح بیتی پروتکل mYBUS این دو آدرس واقعاً
// FLOAT دیکد می‌شوند و 0x0701 با "Current" در Energy Monitoring تصادم دارد.
// این دو رجیستر اینجا به 0x0800/0x0801 منتقل شده‌اند (باید در بک‌اند هم
// همین اصلاح اعمال شده باشد - نگاه کنید به mybus-registry-map.ts).
// ============================================================

// ---- Digital Inputs (Read-only) ----
static constexpr uint16_t REG_INPUT_STATE[16] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F
};

// ---- Digital Outputs ----
static constexpr uint16_t REG_OUTPUT_STATE[16] = {
    0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007,
    0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E, 0x800F
};

static constexpr uint16_t REG_PERMANENT_TIMER[16] = {
    0x8200, 0x8201, 0x8202, 0x8203, 0x8204, 0x8205, 0x8206, 0x8207,
    0x8208, 0x8209, 0x820A, 0x820B, 0x820C, 0x820D, 0x820E, 0x820F
};

static constexpr uint16_t REG_SLEEP_TIMER[16] = {
    0x8210, 0x8211, 0x8212, 0x8213, 0x8214, 0x8215, 0x8216, 0x8217,
    0x8218, 0x8219, 0x821A, 0x821B, 0x821C, 0x821D, 0x821E, 0x821F
};

// ---- Energy Monitoring (Read-only) ----
static constexpr uint16_t REG_VOLTAGE          = 0x0201; // u16
static constexpr uint16_t REG_CURRENT          = 0x0701; // float
static constexpr uint16_t REG_POWER_FACTOR     = 0x0702; // float
static constexpr uint16_t REG_ACTIVE_POWER     = 0x0703; // float
static constexpr uint16_t REG_REACTIVE_POWER   = 0x0704; // float

// ---- Cloud Connectivity (System) ----
static constexpr uint16_t REG_SERVER_FQDN      = 0xC800; // string, R/W
static constexpr uint16_t REG_SERVER_IP        = 0xC801; // string, R/W
static constexpr uint16_t REG_SERVER_PORT      = 0xC200; // u16,   R/W
static constexpr uint16_t REG_DEVICE_ID        = 0x4800; // string, R
static constexpr uint16_t REG_USERNAME         = 0xC802; // string, R/W
static constexpr uint16_t REG_PASSWORD         = 0xC803; // string, R/W

// ---- Motorized Curtain Control ----
static constexpr uint16_t REG_CURTAIN_STATE            = 0x8100; // u8
static constexpr uint16_t REG_CURTAIN_PERMANENT_TIMER  = 0x8220; // u16

// ---- Audio System (طبق داکیومنت - ۸ رجیستر) ----
static constexpr uint16_t REG_AUDIO_MODE         = 0x8101; // u8,   R/W
static constexpr uint16_t REG_AUDIO_CONTROL      = 0x8102; // u8,   R/W
static constexpr uint16_t REG_AUDIO_SLEEP_TIMER  = 0x8221; // u16,  R/W
static constexpr uint16_t REG_AUDIO_STATION      = 0x8222; // u16,  R/W
static constexpr uint16_t REG_AUDIO_TITLE        = 0x0800; // string, R  ← اصلاح‌شده
static constexpr uint16_t REG_AUDIO_ARTIST       = 0x0801; // string, R  ← اصلاح‌شده
static constexpr uint16_t REG_AUDIO_VOLUME       = 0x8103; // u8,   R/W
static constexpr uint16_t REG_AUDIO_BASS         = 0x8104; // u8,   R/W

// ---- RGB Led Strips ----
static constexpr uint16_t REG_RGB_MODE       = 0x8107; // u8
static constexpr uint16_t REG_RGB_HUE        = 0x8223; // u16
static constexpr uint16_t REG_RGB_SATURATION = 0x8108; // u8
static constexpr uint16_t REG_RGB_LIGHTNESS  = 0x8109; // u8

// ---- HVAC ----
static constexpr uint16_t REG_HVAC_TEMPERATURE           = 0x0705; // float, R
static constexpr uint16_t REG_HVAC_SET_POINT             = 0x810A; // u8,   R/W
static constexpr uint16_t REG_HVAC_REMAP_OUTPUT_REGISTRY = 0x8224; // u16,  R/W

#endif