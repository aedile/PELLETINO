#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Start the board and copy what it says over serial to stdout, until it says "bench done"
or the time runs out.

usage: capture.py <port> [seconds]      (needs pyserial; esptool brings it)

On the ESP32-C6's own USB serial, opening the port resets the chip: the Mac moves the
DTR and RTS lines as it opens it, and those two lines are the chip's reset and its boot
strap. Whether it then runs the program or waits in download mode depends on how the lines
happened to settle, so this treats opening the port as the reset it is: it opens it, and if
the chip came up in download mode ("waiting for download") it closes it and tries again.
Once the program is running the port is never opened again, because that would reset it.

Exit status: 0 "bench done" seen; 1 time ran out; 3 the program could not be started.
"""
import re, sys, time
import serial

TRIES = 8

def start(port):
    """open the port until the chip runs the program; returns the open port and what it said"""
    for attempt in range(TRIES):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.2
        s.dtr = False
        s.rts = False
        try:
            s.open()
        except (serial.SerialException, OSError):
            time.sleep(1)
            continue
        said, t = b'', time.time()
        while time.time() - t < 3:
            try:
                said += s.read(4096)
            except (serial.SerialException, OSError):
                break
            if b'waiting for download' in said or b'Calling app_main' in said:
                break
        if b'Calling app_main' in said or (said and b'waiting for download' not in said and b'boot:' in said):
            return s, said
        s.close()
        time.sleep(0.5)
    return None, b''

def main():
    port = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) > 2 else 90
    s, buf = start(port)
    if s is None:
        print(f'capture: the board did not start the program in {TRIES} tries', flush=True)
        sys.exit(3)
    end = time.time() + limit
    while True:
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode('utf-8', 'replace').rstrip()
            print(text, flush=True)
            if text.startswith('bench done'):
                return
        if time.time() > end:
            sys.exit(1)
        try:
            buf += s.read(4096)
        except (serial.SerialException, OSError):
            print('capture: the port went away', flush=True)
            sys.exit(1)

if __name__ == '__main__':
    main()
