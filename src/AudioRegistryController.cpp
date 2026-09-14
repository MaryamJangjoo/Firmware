#include "AudioRegistryController.h"
#include "audio.hpp"
#include "mybus_value_codec.h"

AudioRegistryController::AudioRegistryController(tas5805m& amp, btAudio& bta)
    : amp_(amp), bta_(bta)
{
}

Registery_t* AudioRegistryController::findEntry(uint16_t regAddr)
{
    Registery_t* candidates[] = {
        &reg_module_audio.mode,
        &reg_module_audio.control,
        &reg_module_audio.sleep_timer,
        &reg_module_audio.station,
        &reg_module_audio.title,
        &reg_module_audio.artist,
        &reg_module_audio.volume,
        &reg_module_audio.bass,
        &reg_module_audio.treble,
        &reg_module_audio.eq
    };

    for (auto* entry : candidates) {
        if (entry->address == regAddr) {
            return entry;
        }
    }
    return nullptr;
}

bool AudioRegistryController::read(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = entry->isString;

    if (entry->isString) {
        outValue.stringValue = *static_cast<String*>(entry->ref);
        outValue.byteLen = 0;
        return true;
    }

    switch (entry->datatype) {
        case reg_datatype_uint8:
            outValue.bytes[0] = *static_cast<uint8_t*>(entry->ref);
            outValue.byteLen = sizeof(uint8_t);
            break;
        case reg_datatype_uint16: {
            uint16_t v = *static_cast<uint16_t*>(entry->ref);
            memcpy(outValue.bytes, &v, sizeof(v));
            outValue.byteLen = sizeof(v);
            break;
        }
        default:
            return false;
    }

    return true;
}

bool AudioRegistryController::write(uint16_t regAddr, const String& regVal)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    if (!entry->writable) {
        Serial.printf("[AUDIO] ❌ 0x%04X read-only\n", regAddr);
        return false;
    }

    if (entry->isString) {
        *static_cast<String*>(entry->ref) = regVal;
    } else {
        uint8_t buf[8];
        size_t len = 0;

        if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                                   buf, sizeof(buf), len)) {
            Serial.printf("[AUDIO] ❌ Parse failed: '%s'\n", regVal.c_str());
            return false;
        }

        if (len != entry->size) {
            Serial.printf("[AUDIO] ❌ Size mismatch at 0x%04X\n", regAddr);
            return false;
        }

        memcpy(entry->ref, buf, len);
    }

    if (regAddr == REG_ADD_AUDIO_VOLUME) {
        uint8_t vol = audio_object.volume;
        if (vol > 124) vol = 124;
        esp_err_t ret = tas5805m_set_volume_pct(vol);
        Serial.printf("[AUDIO] Volume -> %u%% (%s)\n", vol, ret == ESP_OK ? "OK" : "FAIL");

    } else if (regAddr == REG_ADD_AUDIO_CONTROL) {
        switch (audio_object.control) {
            case 1:
                bta_.reconnect();
                Serial.println("[AUDIO] ▶️ Play / Reconnect");
                break;
            case 0:
            case 2:
                Serial.printf("[AUDIO] ⏸️ Command %u\n", audio_object.control);
                break;
            default:
                Serial.printf("[AUDIO] ⚠️ Unknown control: %u\n", audio_object.control);
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
        Serial.printf("[AUDIO] Sleep timer -> %u min (not wired)\n", audio_object.sleep_timer);
    }

    return true;
}