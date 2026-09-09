#include "ecosmart_registeries.h"
#include "inputs.hpp"
#include "Outputs.hpp"
#include "audio.hpp"


reg_module_input_t reg_module_input;
reg_module_output_t reg_module_output;
reg_module_audio_t reg_module_audio;


void ecosmart_registery_init()
{
    // Initialize inputs registery
    for (size_t i = 0; i < INPUTS_NUMBER; i++)
    {
        reg_module_input.state[i].address = REG_ADD_INPUT_STATE[i];
        reg_module_input.state[i].datatype = reg_datatype_bit;
        reg_module_input.state[i].size = sizeof(inputs_value[0]);
        reg_module_input.state[i].ref = &inputs_value[i];
    }
    // Initialize outputs registery
    for (size_t i = 0; i < OUTPUTS_NUMBER; i++)
    {
        reg_module_output.state[i].address = REG_ADD_OUTPUT_STATE[i];
        reg_module_output.state[i].datatype = reg_datatype_bit;
        reg_module_output.state[i].size = sizeof(outputs_object[0].value);
        reg_module_output.state[i].ref = &outputs_object[i].value;

        reg_module_output.timer_permanent[i].address = REG_ADD_OUTPUT_TIMER_PERMANENT[i];
        reg_module_output.timer_permanent[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_permanent[i].size = sizeof(outputs_object[0].timer_permanent);
        reg_module_output.timer_permanent[i].ref = &outputs_object[i].timer_permanent;
        

        reg_module_output.timer_sleep[i].address = REG_ADD_OUTPUT_TIMER_SLEEP[i];
        reg_module_output.timer_sleep[i].datatype = reg_datatype_uint16;
        reg_module_output.timer_sleep[i].size = sizeof(outputs_object[0].timer_sleep);
        reg_module_output.timer_sleep[i].ref = &outputs_object[i].timer_sleep;
    }
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
        reg_module_audio.title.writable = false; // read-only

        reg_module_audio.artist.address  = REG_ADD_AUDIO_ARTIST;
        reg_module_audio.artist.datatype = reg_datatype_string;
        reg_module_audio.artist.ref      = &audio_object.artist;
        reg_module_audio.artist.size     = 0;
        reg_module_audio.artist.isString = true;
        reg_module_audio.artist.writable = false; // read-only

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
    }   