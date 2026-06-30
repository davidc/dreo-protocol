#!/usr/bin/env python3

import serial
from time import sleep

# To instruct the wifi module to enter pairing mode

ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0)
# ser.open()

sleep(1)

buf = bytearray([0x55, 0xAA, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00])

buf.append(sum(buf) & 0xFF)

ser.write(buf)


ser.close()