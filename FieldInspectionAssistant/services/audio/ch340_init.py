#!/usr/bin/env python3
"""Initialize the six-mic CH340 at 115200 when the factory ch341.ko is absent.

Uses the built-in generic USB serial transport and CH341 vendor requests from
Linux v6.1 drivers/usb/serial/ch341.c. This is a fixed-115200 workaround, not a
replacement for the vendor's matching ch341.ko. It does not change firmware.
"""
import ctypes
import errno
import os
from pathlib import Path
import re
import subprocess
import sys
import time


class Control(ctypes.Structure):
    _fields_ = [('request_type', ctypes.c_uint8), ('request', ctypes.c_uint8),
                ('value', ctypes.c_uint16), ('index', ctypes.c_uint16),
                ('length', ctypes.c_uint16), ('timeout', ctypes.c_uint32),
                ('data', ctypes.c_void_p)]


def main():
    name = sys.argv[1]
    if not re.fullmatch(r'\d+-\d+(?:\.\d+)*', name):
        raise ValueError('Expected USB device name, e.g. 8-1.3')
    device = Path('/sys/bus/usb/devices') / name
    if ((device / 'idVendor').read_text().strip(),
            (device / 'idProduct').read_text().strip()) != ('1a86', '7523'):
        raise ValueError('This is not the expected CH340 device')
    subprocess.run(['modprobe', 'ch341'], capture_output=True)
    driver = device.parent / (name + ':1.0') / 'driver'
    if driver.exists() and driver.resolve().name == 'ch341':
        print('Native ch341 driver active; fallback unnecessary')
        return
    bus = int((device / 'busnum').read_text())
    address = int((device / 'devnum').read_text())
    lib = ctypes.CDLL(None, use_errno=True)
    fd = os.open(f'/dev/bus/usb/{bus:03d}/{address:03d}', os.O_RDWR)

    def transfer(kind, request, value=0, index=0, length=0):
        data = ctypes.create_string_buffer(max(1, length))
        arg = Control(kind, request, value, index, length, 1000,
                      ctypes.addressof(data))
        request_id = 0xC0005500 | (ctypes.sizeof(Control) << 16)
        result = lib.ioctl(fd, request_id, ctypes.byref(arg))
        if result < 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error))
        return data.raw[:result]

    try:
        version = transfer(0xC0, 0x5F, length=2)
        if not version or version[0] < 0x30:
            raise RuntimeError('Unsupported CH340 revision for this workaround')
        transfer(0x40, 0xA1)
        # 48 MHz / (8 * 52) = 115384.6 baud, nearest supported 115200.
        transfer(0x40, 0x9A, 0x1312, 0xCC83)
        transfer(0x40, 0x9A, 0x2518, 0xC3)  # RX/TX enabled, 8N1
        transfer(0x40, 0xA4, 0xFF9F)  # DTR/RTS asserted (active low)
    finally:
        os.close(fd)
    if not driver.exists():
        try:
            Path('/sys/bus/usb-serial/drivers/generic/new_id').write_text('1a86 7523')
        except OSError as error:
            if error.errno != errno.EEXIST:
                raise
    for _ in range(30):
        if list((device.parent / (name + ':1.0')).glob('ttyUSB*')):
            print(f'{name}: fixed 115200 8N1 generic transport ready')
            return
        time.sleep(0.1)
    raise RuntimeError('USB serial transport did not create ttyUSB')


if __name__ == '__main__':
    main()
