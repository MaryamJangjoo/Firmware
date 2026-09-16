#include "ecosmart_registeries.h"
#include "inputs.hpp"
#include "Outputs.hpp"
#include "audio.hpp"
#include "rgb.hpp"
#include "curtain.hpp"
#include "cloud.hpp"

reg_module_input_t reg_module_input;
reg_module_output_t reg_module_output;
reg_module_audio_t reg_module_audio;
reg_module_rgb_t reg_module_rgb;
reg_module_curtain_t reg_module_curtain;
reg_module_cloud_t reg_module_cloud;

void ecosmart_registery_init()
{
    // ---- Digital Inputs (0x0000..0x000F) - Read-only ----
    for (size_t i = 0; i < INPUTS_NUMBER; i++)
    {
        reg_module_input.state[i].address = REG_ADD_INPUT_STATE[i];
        reg_module_input.state[i].datatype = reg_datatype_bit;
        reg_module_input.state[i].size = sizeof(inputs_value[0]);
        reg_module_input.state[i].ref = &inputs_value[i];
        reg_module_input.state[i].writable = false;   // R (read-only)
    }

    // ---- Digital Outputs (0x8000..0x800F) - R/W ----
    // ---- Output Permanent Timers (0x8200..0x820F) - R/W ----
    // ---- Output Sleep Timers (0x8210..0x821F) - R/W ----
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++)
    {
        reg_module_output.state[i].address = REG_ADD_OUTPUT_STATE[i];
        reg_module_output.state[i].datatype = reg_datatype_bit;
        reg_module_output.state[i].size = sizeof(outputs_object[0].value);
        reg_module_output.state[i].ref = &outputs_object[i].value;
        reg_module_output.state[i].writable = true;

        reg_module_output.timer_permanent[i].address = REG_ADD_OUTPUT_TIMER_PERMANENT[i];
        reg_module_output.timer_permanent[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_permanent[i].size = sizeof(outputs_object[0].timer_permanent);
        reg_module_output.timer_permanent[i].ref = &outputs_object[i].timer_permanent;
        reg_module_output.timer_permanent[i].writable = true;

        reg_module_output.timer_sleep[i].address = REG_ADD_OUTPUT_TIMER_SLEEP[i];
        reg_module_output.timer_sleep[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_sleep[i].size = sizeof(outputs_object[0].timer_sleep);
        reg_module_output.timer_sleep[i].ref = &outputs_object[i].timer_sleep;
        reg_module_output.timer_sleep[i].writable = true;
    }

    // ---- Audio System ----
    reg_module_audio.mode.address  = REG_ADD_AUDIO_MODE;
    reg_module_audio.mode.datatype = reg_datatype_uint8;
    reg_module_audio.mode.size     = sizeof(audio_object.mode);
    reg_module_audio.mode.ref      = &audio_object.mode;
    reg_module_audio.mode.writable = true;

    reg_module_audio.control.address  = REG_ADD_AUDIO_CONTROL;
    reg_module_audio.control.datatype = reg_datatype_uint8;
    reg_module_audio.control.size     = sizeof(audio_object.control);
    reg_module_audio.control.ref      = &audio_object.control;
    reg_module_audio.control.writable = true;

    reg_module_audio.sleep_timer.address  = REG_ADD_AUDIO_SLEEP_TIMER;
    reg_module_audio.sleep_timer.datatype = reg_datatype_uint16;
    reg_module_audio.sleep_timer.size     = sizeof(audio_object.sleep_timer);
    reg_module_audio.sleep_timer.ref      = &audio_object.sleep_timer;
    reg_module_audio.sleep_timer.writable = true;

    reg_module_audio.station.address  = REG_ADD_AUDIO_STATION;
    reg_module_audio.station.datatype = reg_datatype_uint16;
    reg_module_audio.station.size     = sizeof(audio_object.station);
    reg_module_audio.station.ref      = &audio_object.station;
    reg_module_audio.station.writable = true;

    reg_module_audio.title.address  = REG_ADD_AUDIO_TITLE;
    reg_module_audio.title.datatype = reg_datatype_string;
    reg_module_audio.title.ref      = &audio_object.title;
    reg_module_audio.title.size     = 0;
    reg_module_audio.title.isString = true;
    reg_module_audio.title.writable = false;   // R (read-only)

    reg_module_audio.artist.address  = REG_ADD_AUDIO_ARTIST;
    reg_module_audio.artist.datatype = reg_datatype_string;
    reg_module_audio.artist.ref      = &audio_object.artist;
    reg_module_audio.artist.size     = 0;
    reg_module_audio.artist.isString = true;
    reg_module_audio.artist.writable = false;  // R (read-only)

    reg_module_audio.volume.address  = REG_ADD_AUDIO_VOLUME;
    reg_module_audio.volume.datatype = reg_datatype_uint8;
    reg_module_audio.volume.size     = sizeof(audio_object.volume);
    reg_module_audio.volume.ref      = &audio_object.volume;
    reg_module_audio.volume.writable = true;

    reg_module_audio.bass.address  = REG_ADD_AUDIO_BASS;
    reg_module_audio.bass.datatype = reg_datatype_uint8;
    reg_module_audio.bass.size     = sizeof(audio_object.bass);
    reg_module_audio.bass.ref      = &audio_object.bass;
    reg_module_audio.bass.writable = true;

    reg_module_audio.treble.address  = REG_ADD_AUDIO_TREBLE;
    reg_module_audio.treble.datatype = reg_datatype_uint8;
    reg_module_audio.treble.size     = sizeof(audio_object.treble);
    reg_module_audio.treble.ref      = &audio_object.treble;
    reg_module_audio.treble.writable = true;

    reg_module_audio.eq.address  = REG_ADD_AUDIO_EQ;
    reg_module_audio.eq.datatype = reg_datatype_uint8;
    reg_module_audio.eq.size     = sizeof(audio_object.eq);
    reg_module_audio.eq.ref      = &audio_object.eq;
    reg_module_audio.eq.writable = true;

    // ---- RGB Led Strips ----
    reg_module_rgb.mode.address  = REG_ADD_RGB_MODE;
    reg_module_rgb.mode.datatype = reg_datatype_uint8;
    reg_module_rgb.mode.size     = sizeof(rgb_object.mode);
    reg_module_rgb.mode.ref      = &rgb_object.mode;
    reg_module_rgb.mode.writable = true;

    reg_module_rgb.hue.address  = REG_ADD_RGB_HUE;
    reg_module_rgb.hue.datatype = reg_datatype_uint16;
    reg_module_rgb.hue.size     = sizeof(rgb_object.hue);
    reg_module_rgb.hue.ref      = &rgb_object.hue;
    reg_module_rgb.hue.writable = true;

    reg_module_rgb.saturation.address  = REG_ADD_RGB_SATURATION;
    reg_module_rgb.saturation.datatype = reg_datatype_uint8;
    reg_module_rgb.saturation.size     = sizeof(rgb_object.saturation);
    reg_module_rgb.saturation.ref      = &rgb_object.saturation;
    reg_module_rgb.saturation.writable = true;

    reg_module_rgb.lightness.address  = REG_ADD_RGB_LIGHTNESS;
    reg_module_rgb.lightness.datatype = reg_datatype_uint8;
    reg_module_rgb.lightness.size     = sizeof(rgb_object.lightness);
    reg_module_rgb.lightness.ref      = &rgb_object.lightness;
    reg_module_rgb.lightness.writable = true;

    // ---- Motorized Curtain Control ----
    reg_module_curtain.state.address  = REG_ADD_CURTAIN_STATE;
    reg_module_curtain.state.datatype = reg_datatype_uint8;
    reg_module_curtain.state.size     = sizeof(curtain_object.state);
    reg_module_curtain.state.ref      = &curtain_object.state;
    reg_module_curtain.state.writable = true;

    reg_module_curtain.timer_permanent.address  = REG_ADD_CURTAIN_PERMANENT_TIMER;
    reg_module_curtain.timer_permanent.datatype = reg_datatype_uint16;
    reg_module_curtain.timer_permanent.size     = sizeof(curtain_object.timer_permanent);
    reg_module_curtain.timer_permanent.ref      = &curtain_object.timer_permanent;
    reg_module_curtain.timer_permanent.writable = true;

    // ---- Cloud Connectivity (System) ----
    // Backed by cloud_object and persisted via CloudRegistryStore.
    reg_module_cloud.server_fqdn.address  = REG_ADD_CLOUD_SERVER_FQDN;
    reg_module_cloud.server_fqdn.datatype = reg_datatype_string;
    reg_module_cloud.server_fqdn.ref      = &cloud_object.server_fqdn;
    reg_module_cloud.server_fqdn.size     = 0;
    reg_module_cloud.server_fqdn.isString = true;
    reg_module_cloud.server_fqdn.writable = true;

    reg_module_cloud.server_ip.address  = REG_ADD_CLOUD_SERVER_IP;
    reg_module_cloud.server_ip.datatype = reg_datatype_string;
    reg_module_cloud.server_ip.ref      = &cloud_object.server_ip;
    reg_module_cloud.server_ip.size     = 0;
    reg_module_cloud.server_ip.isString = true;
    reg_module_cloud.server_ip.writable = true;

    reg_module_cloud.server_port.address  = REG_ADD_CLOUD_SERVER_PORT;
    reg_module_cloud.server_port.datatype = reg_datatype_uint16;
    reg_module_cloud.server_port.size     = sizeof(cloud_object.server_port);
    reg_module_cloud.server_port.ref      = &cloud_object.server_port;
    reg_module_cloud.server_port.writable = true;

    reg_module_cloud.device_id.address  = REG_ADD_CLOUD_DEVICE_ID;
    reg_module_cloud.device_id.datatype = reg_datatype_string;
    reg_module_cloud.device_id.ref      = &cloud_object.device_id;
    reg_module_cloud.device_id.size     = 0;
    reg_module_cloud.device_id.isString = true;
    reg_module_cloud.device_id.writable = false;  // R (read-only)

    reg_module_cloud.username.address  = REG_ADD_CLOUD_USERNAME;
    reg_module_cloud.username.datatype = reg_datatype_string;
    reg_module_cloud.username.ref      = &cloud_object.username;
    reg_module_cloud.username.size     = 0;
    reg_module_cloud.username.isString = true;
    reg_module_cloud.username.writable = true;

    reg_module_cloud.password.address  = REG_ADD_CLOUD_PASSWORD;
    reg_module_cloud.password.datatype = reg_datatype_string;
    reg_module_cloud.password.ref      = &cloud_object.password;
    reg_module_cloud.password.size     = 0;
    reg_module_cloud.password.isString = true;
    reg_module_cloud.password.writable = true;
}
