#include "RgbRegistryController.h"

#include <Preferences.h>

#include "rgb.hpp"
#include "Logging.h"

static const char* TAG = "RGB";

extern bool     g_rgbControlActive;
extern uint32_t g_lastRgbWriteMs;

static const char* NVS_NAMESPACE = "rgb-config";
static const char* KEY_MODE      = "mode";
static const char* KEY_HUE       = "hue";
static const char* KEY_SAT       = "sat";
static const char* KEY_LIGHT     = "light";

static constexpr uint32_t AUDIO_DECAY_INTERVAL_MS = 50;
static constexpr int32_t  AUDIO_DECAY_AMOUNT      = 5;
static constexpr uint32_t AUDIO_REFRESH_MS        = 30;

RgbRegistryController::RgbRegistryController(
    CRGB* leds,
    size_t numLeds)
    : leds_(leds),
      numLeds_(numLeds)
{
}

void RgbRegistryController::loadFromNvs()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        ECOSMART_LOGI(TAG, "NVS: no stored RGB state, using defaults");
        return;
    }

    rgb_object.mode       = prefs.getUChar(KEY_MODE, 0);
    rgb_object.hue        = prefs.getUShort(KEY_HUE, 0);
    rgb_object.saturation = prefs.getUChar(KEY_SAT, 255);
    rgb_object.lightness  = prefs.getUChar(KEY_LIGHT, 100);
    prefs.end();

    ECOSMART_LOGI(TAG, "NVS: loaded mode=%u hue=%u sat=%u light=%u",
                  rgb_object.mode,
                  rgb_object.hue,
                  rgb_object.saturation,
                  rgb_object.lightness);

    applyToHardware();
}

void RgbRegistryController::saveToNvs()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        ECOSMART_LOGW(TAG, "NVS: open for write failed");
        return;
    }

    prefs.putUChar(KEY_MODE, rgb_object.mode);
    prefs.putUShort(KEY_HUE, rgb_object.hue);
    prefs.putUChar(KEY_SAT, rgb_object.saturation);
    prefs.putUChar(KEY_LIGHT, rgb_object.lightness);
    prefs.end();
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
    g_rgbControlActive = true;
    g_lastRgbWriteMs   = millis();

    saveToNvs();

    (void)regAddr;
    applyToHardware();
}

void RgbRegistryController::update()
{
    const uint32_t now = millis();

    switch (static_cast<RgbMode>(rgb_object.mode)) {
        case RgbMode::OFF:
            break;

        case RgbMode::SOLID:
            break;

        case RgbMode::AUDIO:
            applyAudio(now);
            break;

        default:
            break;
    }
}

void RgbRegistryController::feedAudio(
    const int16_t* samples,
    size_t sampleCount)
{
    if (samples == nullptr || sampleCount == 0) {
        return;
    }

    int32_t peak = 0;
    for (size_t i = 0; i < sampleCount; i++) {
        const int32_t v = abs(samples[i]);
        if (v > peak) peak = v;
    }

    if (peak > dynamicMax_) {
        dynamicMax_ = peak;
    }

    const uint32_t now = millis();
    if (now - lastDecayMs_ >= AUDIO_DECAY_INTERVAL_MS) {
        lastDecayMs_ = now;
        dynamicMax_ = max<int32_t>(peak, dynamicMax_ - AUDIO_DECAY_AMOUNT);
    }

    uint8_t level = 0;
    if (dynamicMax_ > 0) {
        level = static_cast<uint8_t>(
            map(peak, 0, dynamicMax_, 0, static_cast<int32_t>(numLeds_)));
        level = min<uint8_t>(level, static_cast<uint8_t>(numLeds_));
    }

    audioLevel_ = level;
}

void RgbRegistryController::applyToHardware()
{
    switch (static_cast<RgbMode>(rgb_object.mode)) {
        case RgbMode::OFF:
            ECOSMART_LOGI(TAG, "mode=OFF");
            fill_solid(leds_, numLeds_, CRGB::Black);
            FastLED.show();
            return;

        case RgbMode::SOLID:
            applySolid();
            return;

        case RgbMode::AUDIO:
            applyAudio(millis());
            return;

        default:
            fill_solid(leds_, numLeds_, CRGB::Black);
            FastLED.show();
            return;
    }
}

void RgbRegistryController::applySolid()
{
    uint8_t hue8 = static_cast<uint8_t>(
        map(rgb_object.hue % 360, 0, 359, 0, 255));

    uint16_t lightIn = min<uint16_t>(rgb_object.lightness, 100);
    uint8_t lightness255 = static_cast<uint8_t>(
        map(lightIn, 0, 100, 0, 255));

    CHSV color(hue8, rgb_object.saturation, lightness255);

    ECOSMART_LOGI(TAG, "mode=SOLID hue=%u(%u) sat=%u val=%u->%u",
                  rgb_object.hue,
                  hue8,
                  rgb_object.saturation,
                  rgb_object.lightness,
                  lightness255);

    fill_solid(leds_, numLeds_, color);
    FastLED.show();
}

void RgbRegistryController::applyAudio(uint32_t now)
{
    if (now - lastAudioMs_ < AUDIO_REFRESH_MS) {
        return;
    }
    lastAudioMs_ = now;

    uint8_t baseHue = static_cast<uint8_t>(
        map(rgb_object.hue % 360, 0, 359, 0, 255));

    uint8_t sat = rgb_object.saturation;

    uint16_t lightIn = min<uint16_t>(rgb_object.lightness, 100);
    uint8_t maxBrightness = static_cast<uint8_t>(
        map(lightIn, 0, 100, 0, 255));

    for (size_t i = 0; i < numLeds_; i++) {
        if (i < audioLevel_) {
            uint8_t hue = static_cast<uint8_t>(baseHue + i * 8);
            leds_[i] = CHSV(hue, sat, maxBrightness);
        } else {
            leds_[i] = CRGB::Black;
        }
    }
    FastLED.show();
}