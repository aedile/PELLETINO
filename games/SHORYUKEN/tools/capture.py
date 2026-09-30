#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Reset the board and copy what it says over serial to stdout, until it says
"bench done" or the time runs out.

usage: capture.py <port> [seconds]      (needs pyserial; esptool brings it)

The ESP32-C6's USB serial resets the chip into the application when RTS is pulsed with DTR
low, which is what esptool's hard reset does.
"""
import sys, time
import serial

def main():
    port = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 90
    s = serial.Serial(port, 115200, timeout=0.2)
    s.dtr = False
    s.rts = True
    time.sleep(0.2)
    s.rts = False
    end = time.time() + limit
    buf = b''
    while time.time() < end:
        try:
            chunk = s.read(4096)
        except serial.SerialException:
            time.sleep(0.3)             # the port drops for a moment while the chip resets
            try:
                s.close(); s = serial.Serial(port, 115200, timeout=0.2)
            except serial.SerialException:
                pass
            continue
        if not chunk:
            continue
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode('utf-8', 'replace').rstrip()
            print(text, flush=True)
            if text.startswith('bench done'):
                return

if __name__ == '__main__':
    main()
