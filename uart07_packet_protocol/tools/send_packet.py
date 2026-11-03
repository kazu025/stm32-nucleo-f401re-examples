#!/usr/bin/env python3
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    sys.exit(1)


PACKET_STX = 0x02


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF

    for value in data:
        crc ^= value << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


def make_packet(sequence: int, packet_type: int, payload: bytes) -> bytes:
    if len(payload) > 48:
        raise ValueError("payload must be 48 bytes or less")

    body = bytes([
        PACKET_STX,
        len(payload),
        sequence & 0xFF,
        packet_type & 0xFF,
    ]) + payload

    crc = crc16_ccitt(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send one uart07_packet_protocol test packet."
    )
    parser.add_argument(
        "-d",
        "--device",
        default="/dev/ttyACM0",
        help="serial device, default: /dev/ttyACM0",
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="baudrate, default: 115200",
    )
    parser.add_argument(
        "-s",
        "--sequence",
        type=lambda value: int(value, 0),
        default=1,
        help="sequence number, default: 1",
    )
    parser.add_argument(
        "-t",
        "--type",
        type=lambda value: int(value, 0),
        default=1,
        help="packet type, default: 1",
    )
    parser.add_argument("payload", nargs="?", default="hello")
    args = parser.parse_args()

    payload = args.payload.encode("utf-8")
    packet = make_packet(args.sequence, args.type, payload)

    with serial.Serial(args.device, args.baudrate, timeout=1.0) as port:
        port.reset_input_buffer()
        time.sleep(0.5)
        port.write(packet)
        port.flush()
        time.sleep(0.1)
        response = port.read(len(payload))

    print(f"tx packet: {packet.hex(' ')}")
    print(f"rx payload: {response!r}")

    return 0 if response == payload else 1


if __name__ == "__main__":
    raise SystemExit(main())
