#!/usr/bin/env python3
"""Build EARS EVT! or PIU3 EYES packets; print hex by default, send UDP explicitly."""
import argparse
import socket
import struct


def payload(command):
    data = command.encode("ascii")
    if not 1 <= len(data) <= 31 or any(c < 32 or c > 126 for c in data):
        raise ValueError("command must contain 1..31 printable ASCII bytes")
    return data


def uart_packet(command):
    data = payload(command)
    return struct.pack("<4sBBH", b"EVT!", 1, 5, len(data)) + data


def udp_packet(command, client, server, turn, response, sequence):
    data = payload(command)
    return struct.pack("!4sBBHQQIIIH", b"PIU3", 3, 25, 0,
                       client, server, turn, response, sequence, len(data)) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transport", choices=("uart", "udp"))
    parser.add_argument("command", help='one quoted command, e.g. "emotion happy 3000"')
    for name in ("client", "server", "turn", "response", "sequence"):
        parser.add_argument("--" + name, type=lambda s: int(s, 0))
    parser.add_argument("--host", help="send UDP to this CORE IP; omit to print packet hex only")
    parser.add_argument("--port", type=int, default=12000)
    args = parser.parse_args()
    try:
        if args.transport == "uart":
            if args.host:
                parser.error("--host is only valid for UDP")
            packet = uart_packet(args.command)
        else:
            fields = [getattr(args, n) for n in ("client", "server", "turn", "response", "sequence")]
            if any(v is None for v in fields):
                parser.error("UDP requires all five current session/sequence fields")
            packet = udp_packet(args.command, *fields)
        if args.host:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(packet, (args.host, args.port))
            print("Sent (best effort; no acknowledgement)")
        else:
            print(packet.hex(" "))
    except (ValueError, UnicodeError, struct.error, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
