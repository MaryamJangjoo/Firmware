#include "LocalRegisterMap.h"

bool LocalRegisterMap::bind(const LocalRegisterBinding& binding)
{
    if (find(binding.addr) != nullptr) {
        Serial.printf("[REGMAP] ❌ 0x%04X already bound - ignoring duplicate\n", binding.addr);
        return false;
    }

    if (binding.ptr == nullptr) {
        Serial.printf("[REGMAP] ❌ null ptr for 0x%04X\n", binding.addr);
        return false;
    }

    if (!binding.isString) {
        if (binding.size == 0 || binding.size > MAX_FIXED_SIZE) {
            Serial.printf("[REGMAP] ❌ invalid size (%u) for 0x%04X\n",
                          static_cast<unsigned>(binding.size), binding.addr);
            return false;
        }
    }

    bindings_.push_back(binding);

    Serial.printf("[REGMAP] ✅ Bound 0x%04X (%s, %s)\n",
                  binding.addr,
                  binding.isString ? "string" : "fixed",
                  binding.writable ? "R/W" : "R");

    return true;
}

LocalRegisterBinding* LocalRegisterMap::find(uint16_t addr)
{
    for (auto& b : bindings_) {
        if (b.addr == addr) return &b;
    }
    return nullptr;
}

bool LocalRegisterMap::isLocal(uint16_t addr) const
{
    return const_cast<LocalRegisterMap*>(this)->find(addr) != nullptr;
}

bool LocalRegisterMap::shouldMirrorToCloud(uint16_t addr) const
{
    auto* b = const_cast<LocalRegisterMap*>(this)->find(addr);
    return b != nullptr && b->mirrorToCloud;
}

bool LocalRegisterMap::readValueToJson(uint16_t addr, JsonDocument& doc)
{
    auto* b = find(addr);
    if (b == nullptr) return false;

    if (b->onRead != nullptr) {
        b->onRead(b->ctx, b->ptr);
    }

    if (b->isString) {
        doc["value"] = *static_cast<String*>(b->ptr);
        return true;
    }

    uint8_t buf[MAX_FIXED_SIZE];

    portENTER_CRITICAL(&mux_);
    memcpy(buf, b->ptr, b->size);
    portEXIT_CRITICAL(&mux_);

    decodeRegValueToJson(doc, buf, b->size, b->type);
    return true;
}

bool LocalRegisterMap::writeValueFromString(uint16_t addr, const String& regVal)
{
    auto* b = find(addr);
    if (b == nullptr) return false;

    if (!b->writable) {
        Serial.printf("[REGMAP] ❌ 0x%04X is read-only\n", addr);
        return false;
    }

    if (b->isString) {
        *static_cast<String*>(b->ptr) = regVal;
        if (b->onWrite != nullptr) b->onWrite(b->ctx, b->ptr);
        return true;
    }

    uint8_t buf[MAX_FIXED_SIZE];
    size_t len = sizeof(buf);

    if (!encodeRegValueString(regVal, b->type, buf, sizeof(buf), len)) {
        Serial.printf("[REGMAP] ❌ Encode failed for 0x%04X: %s\n", addr, regVal.c_str());
        return false;
    }

    if (len != b->size) {
        Serial.printf("[REGMAP] ❌ Size mismatch 0x%04X: got %u expected %u\n",
                      addr, static_cast<unsigned>(len), static_cast<unsigned>(b->size));
        return false;
    }

    portENTER_CRITICAL(&mux_);
    memcpy(b->ptr, buf, len);
    portEXIT_CRITICAL(&mux_);

    // ⚠️ onWrite عمداً بیرون از critical section: ممکن است I2C
    // (tas5805m) بزند، و نگه‌داشتن spinlock در طول یک تراکنش I2C
    // می‌تواند core دیگر را برای مدت نامعلوم بلاک کند.
    if (b->onWrite != nullptr) b->onWrite(b->ctx, b->ptr);

    return true;
}