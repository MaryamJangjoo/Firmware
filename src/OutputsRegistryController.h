#ifndef OUTPUTS_REGISTRY_CONTROLLER_H
#define OUTPUTS_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include "ecosmart_registeries.h"
#include "RawRegisterValue.h"

// ============================================================
// OutputsRegistryController
//
// مالک منطق خواندن/نوشتن رجیسترهای Digital Input و Digital Output
// (state + timer_permanent + timer_sleep) و تنها نقطه‌ای که مجاز
// است روی شیفت‌رجیستر 74HC595 بنویسد (applyToHardware). هر ماژول
// دیگری که می‌خواهد یک بیت از outputs_object[] را تغییر دهد (مثلاً
// پرده) باید از طریق این کلاس عمل کند تا از overwrite ناخواسته‌ی کل
// رجیستر جلوگیری شود.
// ============================================================

class OutputsRegistryController {
public:
    OutputsRegistryController(int pinLatch, int pinClock, int pinData);

    void begin();

    Registery_t* findOutputEntry(uint16_t regAddr);
    Registery_t* findInputEntry(uint16_t regAddr);

    bool readLocal(uint16_t regAddr, RawRegisterValue& outValue);
    bool writeOutput(uint16_t regAddr, const String& regVal);

    String getValue(uint16_t regAddr);

    void applyToHardware();

private:
    int pinLatch_;
    int pinClock_;
    int pinData_;
};

#endif