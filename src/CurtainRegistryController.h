#ifndef CURTAIN_REGISTRY_CONTROLLER_H
#define CURTAIN_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include "ecosmart_registeries.h"
#include "RawRegisterValue.h"
#include "OutputsRegistryController.h"

// ⚠️ جایگزین setCurtainOn()/setCurtainOff() قدیمی که کل شیفت‌رجیستر
// ۱۶‌بیتی را overwrite می‌کردند. حالا پرده یک بیت داخل outputs_object[]
// است و از طریق OutputsRegistryController::applyToHardware() اعمال
// می‌شود، نه با shiftOut مستقیم.
class CurtainRegistryController {
public:
    // ⚠️⚠️⚠️ TODO حیاتی: outputIndex باید از schematic تایید شود قبل
    // از اتصال به موتور واقعی پرده.
    CurtainRegistryController(OutputsRegistryController& outputs, size_t outputIndex);

    Registery_t* findEntry(uint16_t regAddr);
    bool read(uint16_t regAddr, RawRegisterValue& outValue);
    bool write(uint16_t regAddr, const String& regVal);

private:
    OutputsRegistryController& outputs_;
    size_t outputIndex_;
};

#endif