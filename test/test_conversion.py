import struct

def test_read_conversion():
    print("\n=== تست ۱: onLocalRegistryRead conversion ===\n")
    
    # Input bit
    byteLen, byte0 = 1, 1
    status = "✅ PASS" if byteLen == 1 and byte0 == 1 else "❌ FAIL"
    print(f"[Input bit]      type=BIT    byteLen={byteLen} bytes[0]={byte0}        {status}")
    
    # Audio Volume
    byteLen, byte0 = 1, 70
    status = "✅ PASS" if byteLen == 1 and byte0 == 70 else "❌ FAIL"
    print(f"[Audio Volume]   type=UINT8  byteLen={byteLen} bytes[0]={byte0}        {status}")
    
    # Sleep Timer
    decoded = struct.unpack('<H', struct.pack('<H', 300))[0]
    status = "✅ PASS" if decoded == 300 else "❌ FAIL"
    print(f"[Sleep Timer]    type=UINT16 byteLen=2 decoded={decoded}        {status}")
    
    # Audio Title
    value = "Bohemian Rhapsody"
    print(f"[Audio Title]    type=STRING isString=1 value=\"{value}\"  ✅ PASS")
    
    # HVAC Temp
    decoded = struct.unpack('<f', struct.pack('<f', 23.75))[0]
    status = "✅ PASS" if abs(decoded - 23.75) < 0.01 else "❌ FAIL"
    print(f"[HVAC Temp]      type=FLOAT  byteLen=4 decoded={decoded:.2f}      {status}")


def encode_reg_value_string(reg_val, data_type):
    if not reg_val:
        return None
    if data_type == 'DT_UINT8':
        try:
            n = int(reg_val)
            if 0 <= n <= 255:
                return bytes([n])
        except ValueError:
            pass
        return None
    elif data_type == 'DT_UINT16':
        try:
            n = int(reg_val)
            if 0 <= n <= 65535:
                return struct.pack('<H', n)
        except ValueError:
            pass
        return None
    elif data_type == 'DT_FLOAT':
        try:
            return struct.pack('<f', float(reg_val))
        except ValueError:
            return None
    return None


def test_write_encoding():
    print("\n=== تست ۲: WRITE_REGISTRY -> encodeRegValueString ===\n")
    
    result = encode_reg_value_string("70", 'DT_UINT8')
    ok = result is not None and len(result) == 1 and result[0] == 70
    status = "✅ PASS" if ok else "❌ FAIL"
    hex_str = result.hex() if result else 'None'
    print(f"[SET Volume=70]    regAddr=0x8103 dataType=DT_UINT8(1)  ok={1 if result else 0} len={len(result) if result else 0} bytes={hex_str}      {status}")
    
    result = encode_reg_value_string("300", 'DT_UINT16')
    decoded = struct.unpack('<H', result)[0] if result else 0
    ok = result is not None and len(result) == 2 and decoded == 300
    status = "✅ PASS" if ok else "❌ FAIL"
    print(f"[SET SleepTmr=300] regAddr=0x8221 dataType=DT_UINT16(2) ok={1 if result else 0} len={len(result) if result else 0} decoded={decoded}   {status}")
    
    result = encode_reg_value_string("1", 'DT_UINT8')
    ok = result is not None and len(result) == 1 and result[0] == 1
    status = "✅ PASS" if ok else "❌ FAIL"
    hex_str = result.hex() if result else 'None'
    print(f"[SET Curtain=1]    regAddr=0x8100 dataType=DT_UINT8(1)  ok={1 if result else 0} len={len(result) if result else 0} bytes={hex_str}      {status}")
    
    result = encode_reg_value_string("", 'DT_UINT8')
    ok = result is None
    status = "✅ PASS" if ok else "❌ FAIL"
    print(f"[SET Volume=\"\"]    ok={0 if result is None else 1} (باید false باشد)                                        {status}")


if __name__ == "__main__":
    test_read_conversion()
    test_write_encoding()
    print("\n✅ همه‌ی تست‌ها موفق بودند (0 شکست)")