#pragma once
#include <Arduino.h>

// ⚠️ پرده بر خلاف audio/rgb یک ماژول جدا با شیفت‌رجیستر خودش نیست.
// state آن باید روی یکی از بیت‌های آرایه‌ی outputs_object[] بنشیند
// تا از همون مسیر applyOutputsToHardware() اعمال شود (به‌جای shiftOut
// مستقیم که کل رجیستر را overwrite می‌کرد - نگاه کن به یادداشت‌های
// setCurtainOn/setCurtainOff قدیمی که dead code شدند).
struct CurtainObject
{
    uint8_t  state           = 0; // 0=CLOSE, 1=OPEN
    uint16_t timer_permanent = 0; // فقط متادیتا - مثل output timerها
};

extern CurtainObject curtain_object;