#include "AudioRegistryController.h"

#include "audio.hpp"

AudioRegistryController::AudioRegistryController(
    tas5805m& amp,
    btAudio& bta)
    : amp_(amp),
      bta_(bta)
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

void AudioRegistryController::onWrite(uint16_t regAddr)
{
    if (regAddr == REG_ADD_AUDIO_VOLUME) {
        uint8_t vol = audio_object.volume;
        if (vol > 124) {
            vol = 124;
        }
        esp_err_t ret = tas5805m_set_volume_pct(vol);
        Serial.printf("[AUDIO] Volume -> %u%% (%s)\n",
                      vol,
                      ret == ESP_OK ? "OK" : "FAIL");
        (void)amp_;
    } else if (regAddr == REG_ADD_AUDIO_CONTROL) {
        switch (audio_object.control) {
            case 1:
                bta_.reconnect();
                Serial.println("[AUDIO] Play / Reconnect");
                break;
            case 0:
            case 2:
                Serial.printf("[AUDIO] Command %u\n", audio_object.control);
                break;
            default:
                Serial.printf("[AUDIO] Unknown control: %u\n",
                              audio_object.control);
                break;
        }
    } else if (regAddr == REG_ADD_AUDIO_BASS) {
        Serial.printf("[AUDIO] Bass -> %u (not wired)\n", audio_object.bass);
    } else if (regAddr == REG_ADD_AUDIO_TREBLE) {
        Serial.printf("[AUDIO] Treble -> %u (not wired)\n", audio_object.treble);
    } else if (regAddr == REG_ADD_AUDIO_EQ) {
        Serial.printf("[AUDIO] EQ -> %u (not wired)\n", audio_object.eq);
    } else if (regAddr == REG_ADD_AUDIO_MODE) {
        Serial.printf("[AUDIO] Mode -> %u (not wired)\n", audio_object.mode);
    } else if (regAddr == REG_ADD_AUDIO_STATION) {
        Serial.printf("[AUDIO] Station -> %u (not wired)\n", audio_object.station);
    } else if (regAddr == REG_ADD_AUDIO_SLEEP_TIMER) {
        Serial.printf("[AUDIO] Sleep timer -> %u min (not wired)\n",
                      audio_object.sleep_timer);
    }
}