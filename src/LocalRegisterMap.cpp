#include "LocalRegisterMap.h"

#include "Logging.h"

static const char* TAG = "REGMAP";

bool LocalRegisterMap::bind(const LocalRegisterBinding& binding)
{
    if (find(binding.addr) != nullptr) {
        ECOSMART_LOGW(TAG, "0x%04X already bound - ignoring duplicate", binding.addr);
        return false;
    }

    if (binding.ptr == nullptr) {
        ECOSMART_LOGE(TAG, "null ptr for 0x%04X", binding.addr);
        return false;
    }

    if (!binding.isString) {
        if (binding.size == 0 || binding.size > MAX_FIXED_SIZE) {
            ECOSMART_LOGE(TAG, "invalid size (%u) for 0x%04X",
                          static_cast<unsigned>(binding.size), binding.addr);
            return false;
        }
    }

    bindings_.push_back(binding);

    ECOSMART_LOGI(TAG, "Bound 0x%04X (%s, %s)",
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
        ECOSMART_LOGW(TAG, "0x%04X is read-only", addr);
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
        ECOSMART_LOGE(TAG, "Encode failed for 0x%04X: %s", addr, regVal.c_str());
        return false;
    }

    if (len != b->size) {
        ECOSMART_LOGE(TAG, "Size mismatch 0x%04X: got %u expected %u",
                      addr, static_cast<unsigned>(len), static_cast<unsigned>(b->size));
        return false;
    }

    portENTER_CRITICAL(&mux_);
    memcpy(b->ptr, buf, len);
    portEXIT_CRITICAL(&mux_);

    if (b->onWrite != nullptr) b->onWrite(b->ctx, b->ptr);

    return true;
}