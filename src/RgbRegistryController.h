#ifndef RGB_REGISTRY_CONTROLLER_H
#define RGB_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include <FastLED.h>

#include "RegistryControllerBase.h"

class RgbRegistryController : public RegistryControllerBase {
public:
    RgbRegistryController(CRGB* leds, size_t numLeds);

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    void applyToHardware();

    CRGB* leds_;
    size_t numLeds_;
};

#endif