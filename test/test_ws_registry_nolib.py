# ============================================================
# تست واقعی رجیستری‌ها روی دستگاه ESP32 از طریق WebSocket
#
# بدون هیچ وابستگی خارجی - فقط کتابخانه‌ی استاندارد پایتون
# (نیازی به pip install ندارد)
#
# اجرا:
#   python test_ws_registry.py                (از IP پیش‌فرض استفاده می‌کند)
#   python test_ws_registry.py 192.168.88.84  (IP را صریح می‌دهد)
# ============================================================

import sys
import json
import time
import socket
import base64
import hashlib
import os
import struct

DEFAULT_IP = "192.168.88.84"
WS_PATH = "/ws"
WS_PORT = 80

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class SimpleWebSocket:
    """کلاینت WebSocket حداقلی مطابق RFC6455، فقط برای فریم‌های متنی."""

    def __init__(self, host, port=80, path="/ws", timeout=5):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._buffer = b""
        self._handshake(host, path)

    def _handshake(self, host, path):
        key = base64.b64encode(os.urandom(16)).decode()

        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n"
            f"\r\n"
        )
        self.sock.sendall(request.encode())

        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("اتصال قبل از تکمیل handshake بسته شد")
            response += chunk

        header_part, _, rest = response.partition(b"\r\n\r\n")
        self._buffer = rest  # هر بایت اضافی بعد از هدر HTTP، اولین فریم است

        status_line = header_part.split(b"\r\n", 1)[0].decode(errors="replace")
        if " 101 " not in status_line:
            raise ConnectionError(f"Handshake رد شد: {status_line}")

        # اعتبارسنجی Sec-WebSocket-Accept (اختیاری ولی مطمئن‌تر)
        expected = base64.b64encode(
            hashlib.sha1((key + GUID).encode()).digest()
        ).decode()

        accept_header = None
        for line in header_part.split(b"\r\n")[1:]:
            if line.lower().startswith(b"sec-websocket-accept:"):
                accept_header = line.split(b":", 1)[1].strip().decode()
                break

        if accept_header != expected:
            print("⚠️ هشدار: Sec-WebSocket-Accept مطابقت ندارد (اما ادامه می‌دهیم)")

    def send_text(self, text: str):
        payload = text.encode("utf-8")
        length = len(payload)

        # FIN=1, opcode=0x1 (text)
        header = bytearray([0x81])

        mask_bit = 0x80  # کلاینت -> سرور همیشه باید masked باشد
        if length <= 125:
            header.append(mask_bit | length)
        elif length <= 0xFFFF:
            header.append(mask_bit | 126)
            header += struct.pack(">H", length)
        else:
            header.append(mask_bit | 127)
            header += struct.pack(">Q", length)

        mask_key = os.urandom(4)
        masked = bytearray(payload)
        for i in range(length):
            masked[i] ^= mask_key[i % 4]

        self.sock.sendall(bytes(header) + mask_key + bytes(masked))

    def _fill_buffer(self, timeout):
        self.sock.settimeout(timeout)
        try:
            chunk = self.sock.recv(4096)
            if chunk:
                self._buffer += chunk
                return True
        except socket.timeout:
            pass
        except OSError:
            pass
        return False

    def recv_all(self, wait_seconds=1.5):
        """تا wait_seconds صبر می‌کند و همه‌ی پیام‌های متنی دریافتی را برمی‌گرداند."""
        end_time = time.time() + wait_seconds
        messages = []

        while time.time() < end_time:
            remaining = max(0.05, end_time - time.time())
            got_data = self._fill_buffer(remaining)

            while True:
                msg, consumed = self._try_parse_frame()
                if consumed == 0:
                    break
                self._buffer = self._buffer[consumed:]
                if msg is not None:
                    messages.append(msg)

            if not got_data and not self._buffer:
                # چیزی نیامد و بافر هم خالی است؛ کمی صبر کن و دوباره تلاش کن
                time.sleep(0.05)

        return messages

    def _try_parse_frame(self):
        """یک فریم از self._buffer پارس می‌کند. برمی‌گرداند: (متن یا None, تعداد بایت مصرف‌شده)."""
        buf = self._buffer
        if len(buf) < 2:
            return None, 0

        b0, b1 = buf[0], buf[1]
        opcode = b0 & 0x0F
        masked = (b1 & 0x80) != 0
        length = b1 & 0x7F

        offset = 2

        if length == 126:
            if len(buf) < offset + 2:
                return None, 0
            length = struct.unpack(">H", buf[offset:offset + 2])[0]
            offset += 2
        elif length == 127:
            if len(buf) < offset + 8:
                return None, 0
            length = struct.unpack(">Q", buf[offset:offset + 8])[0]
            offset += 8

        mask_key = b""
        if masked:
            if len(buf) < offset + 4:
                return None, 0
            mask_key = buf[offset:offset + 4]
            offset += 4

        if len(buf) < offset + length:
            return None, 0

        payload = bytearray(buf[offset:offset + length])
        if masked:
            for i in range(length):
                payload[i] ^= mask_key[i % 4]

        total_consumed = offset + length

        if opcode == 0x1:  # TEXT
            return payload.decode("utf-8", errors="replace"), total_consumed
        elif opcode == 0x8:  # CLOSE
            return "[CLOSE فریم دریافت شد]", total_consumed
        elif opcode in (0x9, 0xA):  # PING/PONG
            return None, total_consumed
        else:
            return None, total_consumed

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def send_and_wait(ws, payload, wait_seconds=1.5, label=""):
    text = json.dumps(payload)
    print(f"\n--> ارسال [{label}]: {text}")
    ws.send_text(text)

    messages = ws.recv_all(wait_seconds)
    if not messages:
        print("<-- (چیزی دریافت نشد در بازه‌ی زمانی تعیین‌شده)")
    for msg in messages:
        print(f"<-- دریافت: {msg}")


def main():
    device_ip = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_IP

    print(f"در حال اتصال به ws://{device_ip}{WS_PATH} ...")
    ws = SimpleWebSocket(device_ip, WS_PORT, WS_PATH, timeout=5)
    print("✅ متصل شد (handshake موفق).")

    # پیام‌های اولیه‌ی خوش‌آمدگویی که دستگاه خودش هنگام اتصال می‌فرستد
    initial_msgs = ws.recv_all(wait_seconds=2)
    for msg in initial_msgs:
        print(f"<-- (اولیه) {msg}")

    # ========================================================
    # تست 1: Audio Volume (لوکال، R/W) - regAddr = 0x8103
    # ========================================================
    print("\n" + "=" * 60)
    print("تست 1: Audio Volume (لوکال، خواندنی/نوشتنی)")
    print("=" * 60)

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x8103},
                  label="GET Volume (قبل)")

    send_and_wait(ws, {"action": "WRITE_REGISTRY", "RegAdd": 0x8103, "RegVal": "55"},
                  label="SET Volume=55")

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x8103},
                  label="GET Volume (بعد - باید 55 باشد)")

    # ========================================================
    # تست 2: Digital Output State #1 (لوکال، R/W) - regAddr = 0x8000
    # ========================================================
    print("\n" + "=" * 60)
    print("تست 2: Digital Output #1 (لوکال، خواندنی/نوشتنی)")
    print("=" * 60)

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x8000},
                  label="GET Output#1 (قبل)")

    send_and_wait(ws, {"action": "WRITE_REGISTRY", "RegAdd": 0x8000, "RegVal": "true"},
                  label="SET Output#1=true")

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x8000},
                  label="GET Output#1 (بعد - باید true باشد)")

    # ========================================================
    # تست 3: Audio Title (لوکال، فقط خواندنی، رشته) - regAddr = 0x0800
    # ========================================================
    print("\n" + "=" * 60)
    print("تست 3: Audio Title (لوکال، فقط خواندنی، رشته)")
    print("=" * 60)

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x0800},
                  label="GET Title")

    # ========================================================
    # تست 4: Digital Input #1 (لوکال، فقط خواندنی) - regAddr = 0x0000
    # ========================================================
    print("\n" + "=" * 60)
    print("تست 4: Digital Input #1 (لوکال، فقط خواندنی)")
    print("=" * 60)

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x0000},
                  label="GET Input#1")

    # ========================================================
    # تست 5: HVAC Temperature (غیرلوکال - باید به mYBUS فوروارد شود)
    # ممکن است transport_error/backend_error برگردد اگر سنسور واقعی
    # روی باس نباشد؛ مهم این است که دستگاه کرش نکند.
    # ========================================================
    print("\n" + "=" * 60)
    print("تست 5: HVAC Temperature (غیرلوکال -> فوروارد به mYBUS)")
    print("=" * 60)

    send_and_wait(ws, {"action": "GET_REGISTRY", "RegAdd": 0x0705, "DeviceId": 1},
                  wait_seconds=3, label="GET HVAC Temp (remote)")

    ws.close()
    print("\n✅ تست کامل شد. اتصال بسته شد.")


if __name__ == "__main__":
    main()