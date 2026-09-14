#ifndef AUDIO_REGISTRY_CONTROLLER_H
#define AUDIO_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include "ecosmart_registeries.h"
#include "RawRegisterValue.h"

class AudioRegistryController {
public:
    AudioRegistryController(tas5805m& amp, btAudio& bta);

    Registery_t* findEntry(uint16_t regAddr);
    bool read(uint16_t regAddr, RawRegisterValue& outValue);
    bool write(uint16_t regAddr, const String& regVal);

private:
    tas5805m& amp_;
    btAudio& bta_;
};

#endif