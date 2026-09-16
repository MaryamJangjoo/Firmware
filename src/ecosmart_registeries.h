#ifndef ECOSMART_REGISTERIES_H
#define ECOSMART_REGISTERIES_H

#include "mybus_registry.h"
#include "inputs.hpp"
#include "Outputs.hpp"
#include "rgb.hpp"
#include "curtain.hpp"
#include "cloud.hpp"

typedef enum
{
    reg_datatype_bit = 0,
    reg_datatype_uint8 = 1,
    reg_datatype_uint16 = 2,
    reg_datatype_uint32 = 3,
    reg_datatype_int8 = 4,
    reg_datatype_int16 = 5,
    reg_datatype_int32 = 6,
    reg_datatype_float = 7,
    reg_datatype_string = 8,
    reg_datatype_struct = 9,
    reg_datatype_json = 10
} Reg_DataType_t;

typedef struct
{
    uint16_t address;
    Reg_DataType_t datatype;

    void *ref;
    size_t size;

    bool writable = false;
    bool isString = false;
} Registery_t;

typedef struct
{
    Registery_t state[INPUTS_NUMBER];
} reg_module_input_t;

typedef struct
{
    Registery_t state[OUTPUTS_NUMBER];
    Registery_t timer_permanent[OUTPUTS_NUMBER];
    Registery_t timer_sleep[OUTPUTS_NUMBER];
} reg_module_output_t;

typedef struct
{
    Registery_t mode;
    Registery_t control;
    Registery_t sleep_timer;
    Registery_t station;
    Registery_t title;
    Registery_t artist;
    Registery_t volume;
    Registery_t bass;
    Registery_t treble;
    Registery_t eq;
} reg_module_audio_t;

typedef struct
{
    Registery_t mode;
    Registery_t hue;
    Registery_t saturation;
    Registery_t lightness;
} reg_module_rgb_t;

typedef struct
{
    Registery_t state;
    Registery_t timer_permanent;
} reg_module_curtain_t;

typedef struct
{
    Registery_t server_fqdn;
    Registery_t server_ip;
    Registery_t server_port;
    Registery_t device_id;
    Registery_t username;
    Registery_t password;
} reg_module_cloud_t;

extern reg_module_input_t reg_module_input;
extern reg_module_output_t reg_module_output;
extern reg_module_audio_t reg_module_audio;
extern reg_module_rgb_t reg_module_rgb;
extern reg_module_curtain_t reg_module_curtain;
extern reg_module_cloud_t reg_module_cloud;

// ---- Digital Inputs (Read-only) ----
static constexpr uint16_t REG_ADD_INPUT_STATE[16] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F};

// ---- Digital Outputs ----
static constexpr uint16_t REG_ADD_OUTPUT_STATE[16] = {
    0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007,
    0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E, 0x800F};

static constexpr uint16_t REG_ADD_OUTPUT_TIMER_PERMANENT[16] = {
    0x8200, 0x8201, 0x8202, 0x8203, 0x8204, 0x8205, 0x8206, 0x8207,
    0x8208, 0x8209, 0x820A, 0x820B, 0x820C, 0x820D, 0x820E, 0x820F};

static constexpr uint16_t REG_ADD_OUTPUT_TIMER_SLEEP[16] = {
    0x8210, 0x8211, 0x8212, 0x8213, 0x8214, 0x8215, 0x8216, 0x8217,
    0x8218, 0x8219, 0x821A, 0x821B, 0x821C, 0x821D, 0x821E, 0x821F};

// ---- Digital Outputs (Physical mapping - verified by testing) ----
// index 0..9   (0x8000-0x8009) -> LED 1..10
// index 10..13 (0x800A-0x800D) -> spare / no physical wiring
// index 14..15 (0x800E-0x800F) -> curtain motor (Open / Close channels)
static constexpr size_t LED_OUTPUT_COUNT      = 10;
static constexpr size_t CURTAIN_OUTPUT_OPEN   = 14;
static constexpr size_t CURTAIN_OUTPUT_CLOSE  = 15;

// ---- Energy Monitoring (Read-only) ----
static constexpr uint16_t REG_ADD_VOLTAGE = 0x0201;
static constexpr uint16_t REG_ADD_CURRENT = 0x0701;
static constexpr uint16_t REG_ADD_POWER_FACTOR = 0x0702;
static constexpr uint16_t REG_ADD_ACTIVE_POWER = 0x0703;
static constexpr uint16_t REG_ADD_REACTIVE_POWER = 0x0704;

// ---- Cloud Connectivity (System) ----
static constexpr uint16_t REG_ADD_CLOUD_SERVER_FQDN = 0xC800;
static constexpr uint16_t REG_ADD_CLOUD_SERVER_IP   = 0xC801;
static constexpr uint16_t REG_ADD_CLOUD_SERVER_PORT = 0xC200;
static constexpr uint16_t REG_ADD_CLOUD_DEVICE_ID   = 0x4800;
static constexpr uint16_t REG_ADD_CLOUD_USERNAME    = 0xC802;
static constexpr uint16_t REG_ADD_CLOUD_PASSWORD    = 0xC803;

// ---- Motorized Curtain Control ----
static constexpr uint16_t REG_ADD_CURTAIN_STATE = 0x8100;
static constexpr uint16_t REG_ADD_CURTAIN_PERMANENT_TIMER = 0x8220;

// ---- Audio System ----
static constexpr uint16_t REG_ADD_AUDIO_MODE = 0x8101;
static constexpr uint16_t REG_ADD_AUDIO_CONTROL = 0x8102;
static constexpr uint16_t REG_ADD_AUDIO_SLEEP_TIMER = 0x8221;
static constexpr uint16_t REG_ADD_AUDIO_STATION = 0x8222;
static constexpr uint16_t REG_ADD_AUDIO_TITLE = 0x0800;
static constexpr uint16_t REG_ADD_AUDIO_ARTIST = 0x0801;
static constexpr uint16_t REG_ADD_AUDIO_VOLUME = 0x8103;
static constexpr uint16_t REG_ADD_AUDIO_BASS = 0x8104;
static constexpr uint16_t REG_ADD_AUDIO_TREBLE = 0x8105;
static constexpr uint16_t REG_ADD_AUDIO_EQ = 0x8106;

// ---- RGB Led Strips ----
static constexpr uint16_t REG_ADD_RGB_MODE = 0x8107;
static constexpr uint16_t REG_ADD_RGB_HUE = 0x8223;
static constexpr uint16_t REG_ADD_RGB_SATURATION = 0x8108;
static constexpr uint16_t REG_ADD_RGB_LIGHTNESS = 0x8109;

// ---- HVAC ----
static constexpr uint16_t REG_ADD_HVAC_TEMPERATURE = 0x0705;
static constexpr uint16_t REG_ADD_HVAC_SET_POINT = 0x810A;
static constexpr uint16_t REG_ADD_HVAC_REMAP_OUTPUT_REGISTRY = 0x8224;

// ---- FUNCTIONS ----
void ecosmart_registery_init();

#endif
