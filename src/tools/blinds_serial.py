#!/usr/bin/env python3
"""Send a blind command to the ESP32 over USB serial.

Uses the same 1-byte protocol as the original Arduino Nano firmware:
    byte = (command << 6) | blind_id     command: 0 open, 1 stop, 2 close
    0xFF  enables debug output on the device

Examples:
    python3 blinds_serial.py /dev/ttyUSB0 1 open
    python3 blinds_serial.py COM5 all close
    python3 blinds_serial.py /dev/ttyUSB0 0x13 stop --debug

Requires pyserial (pip install pyserial).
"""
import argparse
import time

import serial

COMMANDS = {"open": 0, "up": 0, "stop": 1, "close": 2, "down": 2}
BLIND_IDS = {"1": 0x11, "2": 0x12, "3": 0x13, "4": 0x14}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. /dev/ttyUSB0 or COM5")
    ap.add_argument("blind", help="1-4, all, or a raw ID such as 0x11")
    ap.add_argument("command", choices=sorted(COMMANDS))
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--debug", action="store_true", help="enable and print device debug output")
    args = ap.parse_args()

    if args.blind == "all":
        ids = list(BLIND_IDS.values())
    elif args.blind in BLIND_IDS:
        ids = [BLIND_IDS[args.blind]]
    else:
        ids = [int(args.blind, 0)]
    if any(not 0 <= i <= 0x3F for i in ids):
        ap.error("blind ID must be between 0x00 and 0x3F")

    # Opening the port resets most ESP32 boards; give it time to boot.
    with serial.Serial(args.port, args.baud, timeout=0.5) as port:
        time.sleep(2.0)
        port.reset_input_buffer()
        if args.debug:
            port.write(b"\xff")
        for blind_id in ids:
            port.write(bytes([(COMMANDS[args.command] << 6) | blind_id]))
            time.sleep(0.35)  # 5 frames x ~46 ms on air
        if args.debug:
            time.sleep(0.3)
            print(port.read(4096).decode(errors="replace"), end="")


if __name__ == "__main__":
    main()
