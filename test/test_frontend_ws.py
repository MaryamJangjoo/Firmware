#!/usr/bin/env python3
"""
Test script for the ESP32 plaintext frontend WebSocket channel.

Workflow:
  1. POST /auth/login to get a 4-byte token (hex string).
  2. Open WebSocket to /ws/frontend.
  3. Run 5 tests:
     - Test 1: WRITE 0x8000 = 1  (turn on LED 1)         - success
     - Test 2: READ  0x8103      (audio volume)           - success
     - Test 3: WRITE 0x8000 = 0  (turn off LED 1)         - success
     - Test 4: WRITE 0x9999 = 1  (unknown register)       - error NOT_FOUND
     - Test 5: WRITE 0x8000 = 1  (invalid token)          - error BAD_REQUEST

Requirements:
    pip install websockets requests

Usage:
    python test_frontend_ws.py
"""

import asyncio
import json
import struct
import sys

import requests
import websockets


# -----------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------

DEVICE_IP = "192.168.88.94"
HTTP_BASE = f"http://{DEVICE_IP}"
WS_URL    = f"ws://{DEVICE_IP}/ws/frontend"

USERNAME  = "tes29t_operator"
PASSWORD  = "SecurePassword@20266"

# mYBUS protocol constants
PROTOCOL_VERSION = 0x02
HEADER_SIZE      = 16
CRC_SIZE         = 4

CMD_READ_REGISTRY  = 0x00
CMD_WRITE_REGISTRY = 0x01

FLAG_SCU_BIT = 5
FLAG_RSP_BIT = 0
FLAG_SF_BIT  = 2

INTERFACE_WIFI = 0x01
ZONE           = 0x01
DEVICE_ID      = 0x01

# Error reason codes
REASON_TRANSPORT_ERROR = 0x01
REASON_BACKEND_ERROR   = 0x02
REASON_NOT_FOUND       = 0x03
REASON_BAD_REQUEST     = 0x04
REASON_UNKNOWN         = 0x05


# -----------------------------------------------------------------
# CRC32 (IEEE 802.3, same as ESP32 side)
# -----------------------------------------------------------------

def crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc & 1) and (0xEDB88320 ^ (crc >> 1)) or (crc >> 1)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


# -----------------------------------------------------------------
# Frame builder
# -----------------------------------------------------------------

def build_frame(command: int,
                reg_addr: int,
                value_bytes: bytes,
                token_bytes: bytes,
                request_number: int = 0) -> bytes:
    """
    Build a plaintext mYBUS v2 frame with a 4-byte token at
    the start of the payload.

    Payload layout:
        [Token0][Token1][Token2][Token3][RegLow][RegHigh][Value...]
    """
    assert len(token_bytes) == 4, "Token must be exactly 4 bytes"

    payload = bytearray()
    payload.extend(token_bytes)
    payload.append(reg_addr & 0xFF)
    payload.append((reg_addr >> 8) & 0xFF)
    payload.extend(value_bytes)

    payload_len = len(payload)
    total_len   = HEADER_SIZE + payload_len + CRC_SIZE

    frame = bytearray(total_len)

    # Header
    frame[0]  = PROTOCOL_VERSION
    frame[1]  = total_len & 0xFF
    frame[2]  = (total_len >> 8) & 0xFF
    frame[3]  = 0x00                       # sequence
    frame[4]  = INTERFACE_WIFI             # interface
    frame[5]  = ZONE                       # zone
    frame[6]  = DEVICE_ID                  # device id
    frame[7]  = 0x00                       # reserved
    frame[8]  = request_number & 0xFF      # request number low
    frame[9]  = (request_number >> 8) & 0xFF
    frame[10] = 0x00                       # qos
    frame[11] = 0x00                       # options
    frame[12] = (1 << FLAG_SCU_BIT)        # flags
    frame[13] = 0x00                       # security = plaintext
    frame[14] = 0x00                       # compression
    frame[15] = command                    # command

    # Payload
    frame[HEADER_SIZE:HEADER_SIZE + payload_len] = payload

    # CRC32 over header + payload
    crc = crc32(bytes(frame[:HEADER_SIZE + payload_len]))
    frame[HEADER_SIZE + payload_len + 0] = crc & 0xFF
    frame[HEADER_SIZE + payload_len + 1] = (crc >> 8) & 0xFF
    frame[HEADER_SIZE + payload_len + 2] = (crc >> 16) & 0xFF
    frame[HEADER_SIZE + payload_len + 3] = (crc >> 24) & 0xFF

    return bytes(frame)


# -----------------------------------------------------------------
# Frame parser (for responses from ESP32)
# -----------------------------------------------------------------

def parse_frame(data: bytes) -> dict:
    if len(data) < HEADER_SIZE + CRC_SIZE:
        raise ValueError(f"Frame too short: {len(data)} bytes")

    hdr = {
        "protocolVersion": data[0],
        "length":          data[1] | (data[2] << 8),
        "sequence":        data[3],
        "interfaceId":     data[4],
        "zone":            data[5],
        "deviceId":        data[6],
        "reserved":        data[7],
        "requestNumber":   data[8] | (data[9] << 8),
        "qos":             data[10],
        "options":         data[11],
        "flags":           data[12],
        "security":        data[13],
        "compression":     data[14],
        "command":         data[15],
    }

    payload_len = hdr["length"] - HEADER_SIZE - CRC_SIZE
    payload = data[HEADER_SIZE:HEADER_SIZE + payload_len]

    return {
        "header":  hdr,
        "payload": bytes(payload),
    }


# -----------------------------------------------------------------
# Hex dump helper
# -----------------------------------------------------------------

def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


# -----------------------------------------------------------------
# Response helpers
# -----------------------------------------------------------------

def is_error_response(parsed: dict) -> bool:
    return (parsed["header"]["flags"] & (1 << FLAG_SF_BIT)) != 0


def parse_error_payload(parsed: dict) -> dict:
    payload = parsed["payload"]
    if len(payload) < 4:
        return {}
    return {
        "originalCommand": payload[0],
        "reason":          payload[1],
        "regAddr":         payload[2] | (payload[3] << 8),
    }


def reason_to_string(reason: int) -> str:
    return {
        REASON_TRANSPORT_ERROR: "TRANSPORT_ERROR",
        REASON_BACKEND_ERROR:   "BACKEND_ERROR",
        REASON_NOT_FOUND:       "NOT_FOUND",
        REASON_BAD_REQUEST:     "BAD_REQUEST",
        REASON_UNKNOWN:         "UNKNOWN",
    }.get(reason, f"UNKNOWN(0x{reason:02X})")


def print_response(parsed: dict):
    hdr = parsed["header"]
    print(f"[TEST]   cmd=0x{hdr['command']:02X} "
          f"flags=0x{hdr['flags']:02X} "
          f"reqNum={hdr['requestNumber']} "
          f"payload={hexdump(parsed['payload'])}")

    if is_error_response(parsed):
        err = parse_error_payload(parsed)
        print(f"[TEST]   ⚠️ ERROR: "
              f"originalCmd=0x{err.get('originalCommand', 0):02X}, "
              f"reason=0x{err.get('reason', 0):02X} "
              f"({reason_to_string(err.get('reason', 0))}), "
              f"reg=0x{err.get('regAddr', 0):04X}")


# -----------------------------------------------------------------
# Step 1: Login
# -----------------------------------------------------------------

def login_and_get_token() -> bytes:
    url = f"{HTTP_BASE}/auth/login"
    body = {
        "username": USERNAME,
        "password": PASSWORD,
    }

    print(f"[TEST] POST {url}")
    resp = requests.post(url, json=body, timeout=5)
    print(f"[TEST] HTTP {resp.status_code}")

    if resp.status_code != 200:
        print(f"[TEST] Login failed: {resp.text}")
        sys.exit(1)

    data = resp.json()
    print(f"[TEST] Response: {data}")

    if not data.get("ok"):
        print("[TEST] Login rejected by device")
        sys.exit(1)

    token_hex = data["token"]
    token_bytes = bytes.fromhex(token_hex)

    if len(token_bytes) != 4:
        print(f"[TEST] Unexpected token length: {len(token_bytes)}")
        sys.exit(1)

    print(f"[TEST] Got token: {token_hex} ({hexdump(token_bytes)})")
    return token_bytes


# -----------------------------------------------------------------
# Step 2: WebSocket tests
# -----------------------------------------------------------------

async def run_ws_tests(token: bytes):
    print(f"\n[TEST] Connecting to {WS_URL}")

    async with websockets.connect(WS_URL) as ws:
        print("[TEST] WebSocket connected")

        # ---- Test 1: WRITE 0x8000 = 1 (turn on LED 1) ----
        print("\n[TEST] --- Test 1: WRITE 0x8000 = 1 (success) ---")
        frame = build_frame(
            command=CMD_WRITE_REGISTRY,
            reg_addr=0x8000,
            value_bytes=bytes([0x01]),
            token_bytes=token,
            request_number=1,
        )
        print(f"[TEST] TX ({len(frame)} bytes): {hexdump(frame)}")
        await ws.send(frame)

        response = await asyncio.wait_for(ws.recv(), timeout=5.0)
        print(f"[TEST] RX ({len(response)} bytes): {hexdump(response)}")
        parsed = parse_frame(response)
        print_response(parsed)

        # ---- Test 2: READ 0x8103 (audio volume) ----
        print("\n[TEST] --- Test 2: READ 0x8103 (success) ---")
        frame = build_frame(
            command=CMD_READ_REGISTRY,
            reg_addr=0x8103,
            value_bytes=b"",
            token_bytes=token,
            request_number=2,
        )
        print(f"[TEST] TX ({len(frame)} bytes): {hexdump(frame)}")
        await ws.send(frame)

        response = await asyncio.wait_for(ws.recv(), timeout=5.0)
        print(f"[TEST] RX ({len(response)} bytes): {hexdump(response)}")
        parsed = parse_frame(response)
        print_response(parsed)

        if len(parsed["payload"]) >= 3:
            value = parsed["payload"][2]
            print(f"[TEST]   Volume = {value}%")

        # ---- Test 3: WRITE 0x8000 = 0 (turn off LED 1) ----
        print("\n[TEST] --- Test 3: WRITE 0x8000 = 0 (success) ---")
        frame = build_frame(
            command=CMD_WRITE_REGISTRY,
            reg_addr=0x8000,
            value_bytes=bytes([0x00]),
            token_bytes=token,
            request_number=3,
        )
        print(f"[TEST] TX ({len(frame)} bytes): {hexdump(frame)}")
        await ws.send(frame)

        response = await asyncio.wait_for(ws.recv(), timeout=5.0)
        print(f"[TEST] RX ({len(response)} bytes): {hexdump(response)}")
        parsed = parse_frame(response)
        print_response(parsed)

        # ---- Test 4: WRITE 0x9999 = 1 (unknown register → NOT_FOUND) ----
        print("\n[TEST] --- Test 4: WRITE 0x9999 = 1 (error: NOT_FOUND) ---")
        frame = build_frame(
            command=CMD_WRITE_REGISTRY,
            reg_addr=0x9999,
            value_bytes=bytes([0x01]),
            token_bytes=token,
            request_number=4,
        )
        print(f"[TEST] TX ({len(frame)} bytes): {hexdump(frame)}")
        await ws.send(frame)

        response = await asyncio.wait_for(ws.recv(), timeout=5.0)
        print(f"[TEST] RX ({len(response)} bytes): {hexdump(response)}")
        parsed = parse_frame(response)
        print_response(parsed)

        if is_error_response(parsed):
            err = parse_error_payload(parsed)
            if err.get("reason") == REASON_NOT_FOUND:
                print("[TEST]   ✅ PASS: reason is NOT_FOUND as expected")
            else:
                print("[TEST]   ❌ FAIL: reason is not NOT_FOUND")
        else:
            print("[TEST]   ❌ FAIL: expected error response")

        # ---- Test 5: WRITE with invalid token → BAD_REQUEST ----
        print("\n[TEST] --- Test 5: WRITE 0x8000 = 1 (error: BAD_REQUEST) ---")
        bad_token = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        frame = build_frame(
            command=CMD_WRITE_REGISTRY,
            reg_addr=0x8000,
            value_bytes=bytes([0x01]),
            token_bytes=bad_token,
            request_number=5,
        )
        print(f"[TEST] TX ({len(frame)} bytes): {hexdump(frame)}")
        await ws.send(frame)

        response = await asyncio.wait_for(ws.recv(), timeout=5.0)
        print(f"[TEST] RX ({len(response)} bytes): {hexdump(response)}")
        parsed = parse_frame(response)
        print_response(parsed)

        if is_error_response(parsed):
            err = parse_error_payload(parsed)
            if err.get("reason") == REASON_BAD_REQUEST:
                print("[TEST]   ✅ PASS: reason is BAD_REQUEST as expected")
            else:
                print("[TEST]   ❌ FAIL: reason is not BAD_REQUEST")
        else:
            print("[TEST]   ❌ FAIL: expected error response")

    print("\n[TEST] All tests completed.")


# -----------------------------------------------------------------
# Entry point
# -----------------------------------------------------------------

def main():
    try:
        token = login_and_get_token()
        asyncio.run(run_ws_tests(token))
    except KeyboardInterrupt:
        print("\n[TEST] Interrupted by user.")
        sys.exit(1)
    except Exception as exc:
        print(f"\n[TEST] Error: {exc}")
        sys.exit(1)


if __name__ == "__main__":
    main()