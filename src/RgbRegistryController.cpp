#include "RgbRegistryController.h"

#include "rgb.hpp"

RgbRegistryController::RgbRegistryController(
    CRGB* leds,
    size_t numLeds)
    : leds_(leds),
      numLeds_(numLeds)
{
}

std::vector<Registery_t*> RgbRegistryController::getCandidates()
{
    return {
        &reg_module_rgb.mode,
        &reg_module_rgb.hue,
        &reg_module_rgb.saturation,
        &reg_module_rgb.lightness,
    };
}

void RgbRegistryController::onWrite(uint16_t regAddr)
{
    // Any RGB register write triggers a hardware refresh.
    (void)regAddr;
    applyToHardware();
}

void RgbRegistryController::applyToHardware()
{
    // WARNING: leds_ / numLeds_ are also written by the audio
    // visualizer in AppController. There is a known race condition
    // between this function and the BLE audio callback. A mutex
    // fix is planned.

    if (rgb_object.mode == 0) {
        Serial.println("[RGB] mode=OFF");
        fill_solid(leds_, numLeds_, CRGB::Black);
        FastLED.show();
        return;
    }

    // TODO: Hue mapping (0-359 -> 0-255) has not been confirmed
    // with the backend yet.
    uint8_t hue8 = static_cast<uint8_t>(
        map(rgb_object.hue % 360, 0, 359, 0, 255));

    CHSV color(hue8, rgb_object.saturation, rgb_object.lightness);

    Serial.printf("[RGB] mode=%u hue=%u(%u) sat=%u val=%u\n",
                  rgb_object.mode,
                  rgb_object.hue,
                  hue8,
                  rgb_object.saturation,
                  rgb_object.lightness);

    fill_solid(leds_, numLeds_, color);
    FastLED.show();
}