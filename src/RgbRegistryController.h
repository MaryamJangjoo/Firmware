#ifndef RGB_REGISTRY_CONTROLLER_H
#define RGB_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include <FastLED.h>

#include "RegistryControllerBase.h"

class RgbRegistryController : public RegistryControllerBase {
public:
    RgbRegistryController(CRGB* leds, size_t numLeds);

    // Load persisted RGB state from NVS.
    void loadFromNvs();

    // Called periodically from the main loop. Handles
    // mode-specific behaviour such as AUDIO visualization.
    void update();

    // Called by AppController when new audio samples arrive.
    // The samples are signed 16-bit mono PCM.
    void feedAudio(const int16_t* samples, size_t sampleCount);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    void applyToHardware();
    void applySolid();
    void applyAudio(uint32_t now);

    void saveToNvs();

    CRGB*  leds_;
    size_t numLeds_;

    // ---- Audio visualizer state ----
    int32_t  dynamicMax_   = 0;
    uint32_t lastDecayMs_  = 0;
    uint32_t lastAudioMs_  = 0;
    uint8_t  audioLevel_   = 0;
};

#endif