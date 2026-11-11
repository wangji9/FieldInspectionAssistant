"""ListenGo manual wake-up, using the supplied Yahboom serial_reader.py protocol.

The UAC stream remains 16 kHz mono; framed six-channel UART PCM is not a WAV.
Only the known control-port alias is used. Never changes firmware/wake words.
"""
import json
from pathlib import Path
import struct
import time


def packet(kind, payload, message_id=0):
    head = struct.pack('<BBBHH', 0xA5, 0x01, kind, len(payload), message_id)
    data = head + payload
    return data + bytes([(-sum(data)) & 255])


def frames(buffer):
    """Extract checksum-checked frames; preserve fragmented UART input."""
    result = []
    while len(buffer) >= 8:
        start = buffer.find(b'\xa5\x01')
        if start < 0:
            return result, buffer[-1:]
        buffer = buffer[start:]
        if len(buffer) < 8:
            break
        size, message_id = struct.unpack_from('<HH', buffer, 3)
        if size > 1024:
            buffer = buffer[1:]
            continue
        if len(buffer) < size+8:
            break
        frame = buffer[:size+8]
        if sum(frame) & 255:
            buffer = buffer[1:]
            continue
        result.append((frame[2], message_id, frame[7:-1]))
        buffer = buffer[size+8:]
    return result, buffer


def wake_microphone():
    port = Path('/dev/lg_speech_serial')
    if not port.exists():
        return '控制串口未连接，使用 UAC 输入'
    try:
        import serial
        with serial.Serial(str(port), 115200, timeout=0.08,
                           write_timeout=0.3, exclusive=True) as control:
            payload = json.dumps({'type': 'manual_wakeup',
                                  'content': {'beam': 0}}).encode('utf-8')
            command = packet(0x05, payload, 1)
            data = b''
            deadline = time.monotonic()+0.4
            first = True
            while time.monotonic() < deadline:
                data += control.read(2048)
                messages, data = frames(data)
                handshake = False
                for kind, msgid, body in messages:
                    if kind == 1:
                        control.write(packet(0xFF, b'\xa5\0\0\0', msgid))
                        handshake = True
                # A handshake can arrive after the first command. Retry only
                # after acknowledging it, never persistently wake in background.
                if first or handshake:
                    control.write(command)
                    control.flush()
                    first = False
        return '已发送手动唤醒，使用六麦 UAC 输入'
    except Exception as exc:
        # UAC also works on firmwares that do not require a wake command.
        return '控制口暂不可用，继续使用 UAC 输入：' + str(exc)
