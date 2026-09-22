#include "HvacRegistryController.h"

#include <math.h>

#include "ecosmart_registeries.h"
#include "hvac.hpp"
#include "Logging.h"

static const char* TAG = "HVAC";

HvacRegistryController::HvacRegistryController()
{
}

void HvacRegistryController::begin(uint8_t dhtPin)
{
    dht_ = new DHT(dhtPin, DHT11);
    dht_->begin();
    
    delay(1000);

    ECOSMART_LOGI(TAG, "DHT11 initialized on GPIO %u", dhtPin);
}

void HvacRegistryController::poll()
{
    if (dht_ == nullptr) {
        return;
    }

    const uint32_t now = millis();
    if (now - lastReadMs_ < READ_INTERVAL_MS) {
        return;
    }
    lastReadMs_ = now;

    float t = dht_->readTemperature();
    float h = dht_->readHumidity();

    if (isnan(t) || isnan(h)) {
        if (valid_) {
            ECOSMART_LOGW(TAG, "DHT11 read failed, keeping last values");
        } else {
            ECOSMART_LOGW(TAG, "DHT11 read failed (no valid value yet)");
        }
        return;
    }

    hvac_object.temperature = t;
    hvac_object.humidity    = h;
    valid_ = true;

   //ECOSMART_LOGI(TAG, "Temp=%.1f C  Humidity=%.1f %%", t, h);
}

std::vector<Registery_t*> HvacRegistryController::getCandidates()
{
    return {
        &reg_module_hvac.temperature,
        &reg_module_hvac.humidity,
        &reg_module_hvac.set_point,
        &reg_module_hvac.remap_output,
    };
}

void HvacRegistryController::onWrite(uint16_t regAddr)
{
    if (regAddr == REG_ADD_HVAC_SET_POINT) {
        ECOSMART_LOGI(TAG, "Set Point -> %u C", hvac_object.set_point);
    } else if (regAddr == REG_ADD_HVAC_REMAP_OUTPUT_REGISTRY) {
        ECOSMART_LOGI(TAG, "Remap Output -> 0x%04X", hvac_object.remap_output);
    }
}