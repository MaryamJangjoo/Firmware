#ifndef AUDIO_REGISTRY_CONTROLLER_H
#define AUDIO_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include <tas5805m.hpp>
#include <btAudio.h>

#include "RegistryControllerBase.h"

class AudioRegistryController : public RegistryControllerBase {
public:
    AudioRegistryController(tas5805m& amp, btAudio& bta);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    void enableEqIfNeeded();

    tas5805m& amp_;
    btAudio& bta_;
    bool eqEnabled_ = false;
};

#endif