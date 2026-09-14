#ifndef RGB_REGISTRY_CONTROLLER_H
#define RGB_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>
#include "ecosmart_registeries.h"
#include "RawRegisterValue.h"

class RgbRegistryController {
public:
    RgbRegistryController(CRGB* leds, size_t numLeds);

    Registery_t* findEntry(uint16_t regAddr);
    bool read(uint16_t regAddr, RawRegisterValue& outValue);
    bool write(uint16_t regAddr, const String& regVal);

    void applyToHardware();

private:
    CRGB* leds_;
    size_t numLeds_;
};

#endif