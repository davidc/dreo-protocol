#!/usr/bin/env python3

from datetime import datetime
from pprint import pprint

from dataclasses import dataclass
import struct
from typing import Optional

import serial


HEADER = struct.Struct(">2sBBBBH")
#                 2s 55AA
#                    B  version
#                     B sequence
#                      B command
#                       B reserved1
#                        H len

HEADER_MAGIC = b"\x55\xaa"


@dataclass
class Header:
    magic: bytes
    version: int
    sequence: int
    command: int
    r1: int
    length: int


class IncompletePacketException(Exception):
    pass


def parse_header(packet: bytes) -> Header:
    if len(packet) < HEADER.size:
        raise IncompletePacketException()
        # raise ValueError("Packet too short")

    fields = HEADER.unpack_from(packet)
    hdr = Header(*fields)

    if hdr.magic != HEADER_MAGIC:
        raise ValueError(f"Bad magic {hdr.magic.hex()}")

    return hdr


DP_HEADER = struct.Struct(">BBBH")


@dataclass
class Datapoint:
    dpid: int
    r1: int  # this seems to be set to 1 if any dp has just been changed by the app
    dp_type: int
    length: int
    value: bool | int | bytes


def parse_dp(data: bytes, offset=0):
    fields = DP_HEADER.unpack_from(data, offset)
    offset += DP_HEADER.size

    dp = Datapoint(*fields, value=data[offset : offset + fields[3]])
    offset += dp.length

    if dp.dp_type == 0x01:  # boolean
        dp.value = bool(dp.value[0])
    elif dp.dp_type == 0x02:  # int
        if dp.length == 1:
            dp.value = struct.unpack(">b", dp.value)[0]
        else:
            dp.value = struct.unpack(">i", dp.value)[0]
    elif dp.dp_type == 0x04:  # enum
        dp.value = struct.unpack(">B", dp.value)[0]

    return dp, offset


def dpid_name(dpid: int) -> str:
    if dpid == 1:
        return "power"
    elif dpid == 2:
        return "display always on"
    elif dpid == 3:
        return "mode"  # 1 = normal, 2 = natural, 3 = sleep, 4 = auto
    elif dpid == 4:
        return "fan speed"  # 1-9
    elif dpid == 5:
        return "beeper"
    elif dpid == 6:
        return "auto-off timer (m)"
    elif dpid == 8:
        return "oscillation"
    elif dpid == 11:
        return "temperature (F)"
    else:
        return "unknown"


def dpid_type_name(dp_type: int) -> str:
    if dp_type == 0x01:
        return "boolean"
    elif dp_type == 0x02:
        return "int32"
    elif dp_type == 0x04:
        return "enum"
    else:
        return f"unknown (0x{dp_type:02x})"


def cmd_heartbeat(body: bytes):
    if len(body) == 0:
        print("Heartbeat request")
    else:
        print(f"Heartbeat response, status={hex(body[0])}")


def cmd_mcu_info(body: bytes):
    if len(body) == 0:
        print("Request MCU info")
    else:
        print("Request MCU info response")
        print(f"    MCU info: {body.decode(errors='ignore')}")


def cmd_report_status(body: bytes):
    if len(body) == 0:
        print("Status report request/ack")
    else:
        print("Status report response")
        offset = 0
        while offset < len(body):
            dp, offset = parse_dp(body, offset)
            print(f"    dpId {dp.dpid} ({dpid_name(dp.dpid)}): {dpid_type_name(dp.dp_type)} = {dp.value}")


def cmd_dp_changed(body: bytes):
    # a DP has just been changed on the panel; the data seems to not make much sense, but we've just received a status
    # report anyway so we can ignore this
    print(f"DP changed {body.hex()}")


def cmd_change_dp(body: bytes):
    # 040102000106 set dp 4 to value 6
    # first byte is the dp id

    # 040102000107 set dp 4 value to 7
    # 080101000100 set dp 8 value to 0
    # 080101000101 set dp 8 value to 1
    # 030102000104 set dp 3 value to 4
    # 0701020004000001e0   set dp 7 value to 480
    # ^^ DP
    #   ^^ always 1?
    #     ^^ DP type??? 1 for bool, 2 for int32 and enum
    #       ^^ always 00
    #         ^^ length of data
    #           ^^ new value

    if len(body) == 0:
        print("Change DP response")
    else:
        print("Change DPs:")
        offset = 0
        while offset < len(body):
            dp, offset = parse_dp(body, offset)
            print(f"    dpId {dp.dpid} ({dpid_name(dp.dpid)}): {dpid_type_name(dp.dp_type)} = {dp.value}")


def decode_packet(label: str, packet: bytes):
    hdr = parse_header(packet)

    body = packet[HEADER.size : HEADER.size + hdr.length]

    if len(body) < hdr.length:
        raise IncompletePacketException()

    print("Packet: " + packet[0 : HEADER.size + hdr.length + 1].hex())

    time = datetime.now().strftime("%H:%M:%S.%f")
    print(f"[{time}] [{label}] [seq={hdr.sequence:02x}] ", end="")

    if hdr.version != 0:
        print(f"WARNING: Unknown version {hdr.version:02x}")

    if hdr.r1 != 0:
        print(f"WARNING: r1 is {hdr.r1:02x}")

    print(f"header size {HEADER.size}")
    packet_checksum = packet[HEADER.size + hdr.length]
    calculated_checksum = sum(packet[0 : HEADER.size + hdr.length]) & 0xFF

    if packet_checksum != calculated_checksum:
        raise ValueError(f"Checksum mismatch: packet 0x{packet_checksum:02x} != calculated 0x{calculated_checksum:02x}")

    if hdr.command == 0x00:  # Heartbeat
        cmd_heartbeat(body)
    elif hdr.command == 0x01:  # Get MCU info
        cmd_mcu_info(body)
    elif hdr.command == 0x03:  # Report module status
        print(f"Report module status {body.hex()}")
    elif hdr.command == 0x04:  # Reset module
        print("Reset module")
    elif hdr.command == 0x06:  # Change dp
        cmd_change_dp(body)
    elif hdr.command == 0x07:  # Report dp status
        cmd_report_status(body)
    elif hdr.command == 0x08:  # Query status
        print("Request immediate status report")
    elif hdr.command == 0x0E:  # dp changed (not tuya standard)
        cmd_dp_changed(body)
    else:
        print(f"body length {len(body)}")
        pprint(body.hex())
        print(f"Unknown command 0x{hdr.command:02x}")

    return packet[HEADER.size + hdr.length + 1 :]


def decode_datastream(label: str, data: bytes):
    try:
        while len(data) >= 2 and data[0:2] != HEADER_MAGIC:
            print(f"[{label}] skip byte 0x{data[0]:02x} waiting for header magic")
            data = data[1:]

        if len(data) < HEADER.size:
            return data

        while len(data) > 0:
            data = decode_packet(label, data)
    except IncompletePacketException:
        return data
    return bytes()


# decode_packet(
#     bytes.fromhex(
#         "55AA00740700004B0100010001010200010001000300040001020400040001050500010001010600020004000000000700020004000000C60800010001000900040001000B00020004000000520C000100010055"
#     )
# )
# decode_packet(
#     bytes.fromhex(
#         "55AA009E0700004B01000100010102000100010003000400010204000400010505000100010106000200040000000007000200040000009C0800010001000900040001000B00020004000000520C000100010055"
#     )
# )

# decode_packet(bytes.fromhex("55AA0005000000010106"))

# decode_packet(bytes.fromhex("55AA00F700000000F6"))


# decode_packet(
#     bytes.fromhex(
#         "55AA00BE0700004B0100010001010200010001000300040001020400040001060500010001010600020004000000000700020004000000800800010001000900040001000B00020004000000520C00010001005A55AA00BF0E000003010105D6"
#     )
# )
# decode_datastream(
#     bytes.fromhex(
#         "55AA00CC0700004B01000100010102000100010003000400010204000400010205000100010106000200040000000007000200040000007F0800010001000900040001000B00020004000000520C00010001006355AA00CD0E000003010104E3"
#     )
# )

class SerialPort:
    device: str
    label: str
    serial: Optional[serial.Serial]
    buffer: Optional[bytes]

    def __init__(self, device: str, label: str):
        self.device = device
        self.label = label


ports = []

ports.append(SerialPort("/dev/ttyUSB0", "Wifi-->MCU"))
ports.append(SerialPort("/dev/ttyUSB1", "Wifi<--MCU"))

for port in ports:
    port.serial = serial.Serial(port.device, 115200, timeout=0)
    # port.serial.open()
    port.buffer = bytes()

while True:
    for port in ports:
        read = port.serial.read(port.serial.in_waiting or 1)
        if read:
            port.buffer += read
            if len(port.buffer) > HEADER.size:
                port.buffer = decode_datastream(port.label, port.buffer)
