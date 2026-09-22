#ifndef HVAC_REGISTRY_CONTROLLER_H
#define HVAC_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include <DHT.h>

#include "RegistryControllerBase.h"

class HvacRegistryController : public RegistryControllerBase {
public:
    HvacRegistryController();

    void begin(uint8_t dhtPin = 14);

    void poll();

    float getTemperature() const { return hvac_object.temperature; }
    float getHumidity()    const { return hvac_object.humidity; }
    bool  isValid()        const { return valid_; }

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

private:
    DHT* dht_ = nullptr;

    uint32_t lastReadMs_ = 0;
    static constexpr uint32_t READ_INTERVAL_MS = 2000;

    bool valid_ = false;
};

#endif