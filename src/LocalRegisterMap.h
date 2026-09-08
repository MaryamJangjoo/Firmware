#ifndef LOCAL_REGISTER_MAP_H
#define LOCAL_REGISTER_MAP_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <string.h>

#include "mybus_frame.h"
#include "mybus_value_codec.h"

// ============================================================
// LocalRegisterMap — Process Image Binding
//
// هر ورودی مستقیماً به آدرس یک متغیر عضو ماژول اشاره می‌کند.
// خواندن/نوشتن یعنی memcpy از/به همان حافظه، نه یک کپی جدا.
//
// ⚠️ فقط برای رجیسترهای Local/Hardware. رجیسترهای mYBUS/ریموت
// اینجا bind نمی‌شوند - همیشه باید request/response شبکه‌ای بروند.
//
// Thread-safety: نوشتن از context ASync WebSocket می‌آید، خواندن/
// اعمال هاردور از loop() اصلی. رجیسترهای عددی با spinlock محافظت
// می‌شوند؛ رجیسترهای String با آن محافظت نمی‌شوند چون String::operator=
// می‌تواند heap alloc کند و نگه‌داشتن spinlock در طول آن خطرناک است.
// ============================================================

using LocalRegisterHook = void (*)(void* ctx, void* ptr);

struct LocalRegisterBinding {
    uint16_t addr;
    MyBusDataType type;

    void*  ptr;
    size_t size;          
    bool writable;
    bool mirrorToCloud;
    bool isString;          
    void* ctx;              
    LocalRegisterHook onWrite; 
    LocalRegisterHook onRead;  
};

class LocalRegisterMap {
public:
    bool bind(const LocalRegisterBinding& binding);

    bool isLocal(uint16_t addr) const;
    bool shouldMirrorToCloud(uint16_t addr) const;

    bool readValueToJson(uint16_t addr, JsonDocument& doc);
    bool writeValueFromString(uint16_t addr, const String& regVal);

private:
    LocalRegisterBinding* find(uint16_t addr);

    std::vector<LocalRegisterBinding> bindings_;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    static constexpr size_t MAX_FIXED_SIZE = 8;
};

#endif