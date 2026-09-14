#include "RgbRegistryController.h"
#include "rgb.hpp"
#include "mybus_value_codec.h"

RgbRegistryController::RgbRegistryController(CRGB* leds, size_t numLeds)
    : leds_(leds), numLeds_(numLeds)
{
}

Registery_t* RgbRegistryController::findEntry(uint16_t regAddr)
{
    Registery_t* candidates[] = {
        &reg_module_rgb.mode,
        &reg_module_rgb.hue,
        &reg_module_rgb.saturation,
        &reg_module_rgb.lightness
    };

    for (auto* entry : candidates) {
        if (entry->address == regAddr) {
            return entry;
        }
    }
    return nullptr;
}

bool RgbRegistryController::read(uint16_t regAddr, RawRegisterValue& outValue)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    outValue.datatype = entry->datatype;
    outValue.isString = false;

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

bool RgbRegistryController::write(uint16_t regAddr, const String& regVal)
{
    Registery_t* entry = findEntry(regAddr);
    if (entry == nullptr || entry->ref == nullptr) {
        return false;
    }

    if (!entry->writable) {
        Serial.printf("[RGB] ❌ 0x%04X read-only\n", regAddr);
        return false;
    }

    uint8_t buf[8];
    size_t len = 0;

    if (!encodeRegValueString(regVal, static_cast<MyBusDataType>(entry->datatype),
                               buf, sizeof(buf), len)) {
        Serial.printf("[RGB] ❌ Parse failed: '%s'\n", regVal.c_str());
        return false;
    }

    if (len != entry->size) {
        Serial.printf("[RGB] ❌ Size mismatch at 0x%04X\n", regAddr);
        return false;
    }

    memcpy(entry->ref, buf, len);

    applyToHardware();

    return true;
}

void RgbRegistryController::applyToHardware()
{
    // ⚠️ TODO (باگ باز): leds_/numLeds_ توسط ویژوالایزر صوتی هم
    // نوشته می‌شوند (AppController::visualizeAudio). تداخل هنوز رفع
    // نشده - نگاه کن به یادداشت‌های قبلی.

    if (rgb_object.mode == 0) {
        Serial.println("[RGB] mode=OFF");
        fill_solid(leds_, numLeds_, CRGB::Black);
        FastLED.show();
        return;
    }

    // ⚠️ TODO (باگ باز): مپینگ Hue 0-359 -> 0-255 هنوز با بک‌اند
    // تایید نشده.
    uint8_t hue8 = static_cast<uint8_t>(map(rgb_object.hue % 360, 0, 359, 0, 255));

    CHSV color(hue8, rgb_object.saturation, rgb_object.lightness);

    Serial.printf("[RGB] mode=%u hue=%u(%u) sat=%u val=%u\n",
                  rgb_object.mode, rgb_object.hue, hue8,
                  rgb_object.saturation, rgb_object.lightness);

    fill_solid(leds_, numLeds_, color);
    FastLED.show();
}