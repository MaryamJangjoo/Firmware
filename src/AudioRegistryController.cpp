#include "AudioRegistryController.h"
#include "audio.hpp"

AudioRegistryController::AudioRegistryController(
    tas5805m& amp, btAudio& bta)
    : amp_(amp), bta_(bta)
{
}

std::vector<Registery_t*> AudioRegistryController::getCandidates()
{
    return {
        &reg_module_audio.mode,
        &reg_module_audio.control,
        &reg_module_audio.sleep_timer,
        &reg_module_audio.station,
        &reg_module_audio.title,
        &reg_module_audio.artist,
        &reg_module_audio.volume,
        &reg_module_audio.bass,
        &reg_module_audio.treble,
        &reg_module_audio.eq,
    };
}

// Probe results from diagnoseEqGainRange on this hardware/library
// build:
//   valid range is [-15, +15] dB
//   first rejected values are -18 and +18
// Therefore map the 0..255 user value linearly into [-15, +15] to
// stay strictly inside the valid range and avoid the
// ESP_ERR_INVALID_ARG error from tas5805m_set_eq_gain_channel.
static int userValueToEqGainDb(uint8_t value)
{
    return map(value, 0, 255, -15, 15);
}

void AudioRegistryController::onWrite(uint16_t regAddr)
{
    if (regAddr == REG_ADD_AUDIO_VOLUME) {
        uint8_t vol = audio_object.volume;
        if (vol > 124) vol = 124;
        tas5805m_set_volume_pct(vol);
        Serial.printf("[AUDIO] Volume -> %u%%\n", vol);
    }
    else if (regAddr == REG_ADD_AUDIO_BASS) {
        enableEqIfNeeded();

        const int gain_db = userValueToEqGainDb(audio_object.bass);

        for (int band = 0; band < 5; band++) {
            amp_.setEqGain(TAS5805M_EQ_CHANNELS_LEFT,  band, gain_db);
            amp_.setEqGain(TAS5805M_EQ_CHANNELS_RIGHT, band, gain_db);
        }

        Serial.printf("[AUDIO] Bass -> %u (gain %d dB)\n",
                      audio_object.bass, gain_db);
    }
    else if (regAddr == REG_ADD_AUDIO_TREBLE) {
        enableEqIfNeeded();

        const int gain_db = userValueToEqGainDb(audio_object.treble);

        for (int band = 10; band < 15; band++) {
            amp_.setEqGain(TAS5805M_EQ_CHANNELS_LEFT,  band, gain_db);
            amp_.setEqGain(TAS5805M_EQ_CHANNELS_RIGHT, band, gain_db);
        }

        Serial.printf("[AUDIO] Treble -> %u (gain %d dB)\n",
                      audio_object.treble, gain_db);
    }
    else if (regAddr == REG_ADD_AUDIO_EQ) {
        Serial.printf("[AUDIO] EQ -> %u\n", audio_object.eq);
    }
    else if (regAddr == REG_ADD_AUDIO_CONTROL) {
        switch (audio_object.control) {
            case 1: bta_.reconnect(); break;
            default: break;
        }
    }
    else if (regAddr == REG_ADD_AUDIO_MODE) {
        Serial.printf("[AUDIO] Mode -> %u\n", audio_object.mode);
    }
    else if (regAddr == REG_ADD_AUDIO_STATION) {
        Serial.printf("[AUDIO] Station -> %u\n", audio_object.station);
    }
    else if (regAddr == REG_ADD_AUDIO_SLEEP_TIMER) {
        Serial.printf("[AUDIO] Sleep timer -> %u min\n", audio_object.sleep_timer);
    }
}

void AudioRegistryController::enableEqIfNeeded()
{
    if (eqEnabled_) return;

    esp_err_t ret = amp_.setEqMode(TAS5805M_EQ_MODE_ON);
    if (ret == ESP_OK) {
        delay(50);
        eqEnabled_ = true;
        Serial.println("[AUDIO] EQ mode ON");
    } else {
        Serial.printf("[AUDIO] ❌ setEqMode failed: 0x%04X\n", ret);
    }
}