#!/usr/bin/env python3
"""Capture PIUU CORE LAN diagnostics. Standard library only; no serial/reset access."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import socket
import struct
import time

SUBSCRIBE = b"PIUU_LOG_SUBSCRIBE_V1"
HEADER = struct.Struct("!4sIIIIHH")


def decode(data):
    if len(data) < HEADER.size:
        raise ValueError("short header")
    magic, boot, seq, dropped, ms, length, reserved = HEADER.unpack_from(data)
    if magic != b"PLG1" or reserved or not 1 <= length <= 192 or len(data) != HEADER.size + length:
        raise ValueError("invalid packet")
    return boot, seq, dropped, ms, data[HEADER.size:]


class Capture:
    def __init__(self, output):
        self.output = output
        self.boot = self.seq = self.dropped = None
        self.previous_boots = []
        self.pending = bytearray()

    def line(self, text):
        stamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        self.output.write(f"[{stamp}] {text}\n")
        self.output.flush()

    def finish(self):
        if self.pending:
            self.line(self.pending.decode("utf-8", errors="replace").rstrip("\r") + " [partial]")
            self.pending.clear()

    def accept(self, packet):
        boot, seq, dropped, ms, payload = decode(packet)
        if boot != self.boot:
            if boot in self.previous_boots:
                return False
            self.finish()
            if self.boot is not None:
                self.previous_boots = (self.previous_boots + [self.boot])[-8:]
            self.boot, self.seq, self.dropped = boot, None, None
            self.line(f"[CAPTURE] boot={boot:08x} core_ms={ms} sequence={seq}")
        if self.seq is not None:
            distance = (seq - self.seq) & 0xFFFFFFFF
            if distance == 0 or distance >= 0x80000000:
                return False  # Duplicate or reordered datagram.
            if distance > 1:
                self.finish()
                self.line(f"[CAPTURE] missing_datagrams={distance - 1} core_ms={ms}")
        if dropped != self.dropped:
            if dropped or self.dropped is not None:
                self.finish()
                self.line(f"[CAPTURE] core_dropped_bytes_total={dropped}")
        self.seq, self.dropped = seq, dropped
        self.pending.extend(payload)
        while b"\n" in self.pending:
            line, _, rest = self.pending.partition(b"\n")
            self.pending = bytearray(rest)
            self.line(line.decode("utf-8", errors="replace").rstrip("\r"))
        if len(self.pending) >= 8192:
            self.finish()
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="CORE WiFi IPv4 address")
    parser.add_argument("--port", type=int, default=12001)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--duration", type=float, default=0, help="seconds; 0 captures until Ctrl+C")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.duration < 0:
        parser.error("invalid port or duration")
    host = socket.gethostbyname(args.host)
    destination = args.output or Path(__file__).resolve().parents[1] / "logs" / "integrated" / (
        "core-udp-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".log")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("a", encoding="utf-8", buffering=1) as output, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("0.0.0.0", 0))
        udp.settimeout(0.25)
        capture = Capture(output)
        start = time.monotonic()
        renew = start
        last_received = start
        warned = False
        print(f"Capturing {host}:{args.port} -> {destination}", flush=True)
        try:
            while not args.duration or time.monotonic() - start < args.duration:
                now = time.monotonic()
                if now >= renew:
                    udp.sendto(SUBSCRIBE, (host, args.port))
                    renew = now + 3
                try:
                    data, source = udp.recvfrom(2048)
                    if source != (host, args.port):
                        continue
                    try:
                        accepted = capture.accept(data)
                    except ValueError:
                        continue
                    if accepted:
                        last_received = time.monotonic()
                        if warned:
                            print("CORE logs resumed", flush=True)
                        warned = False
                except socket.timeout:
                    pass
                if time.monotonic() - last_received >= 12 and not warned:
                    print("No CORE packets for 12 s: check IP, WiFi, firewall and UDP logging firmware.", flush=True)
                    warned = True
        except KeyboardInterrupt:
            pass
        finally:
            capture.finish()
            print(f"Saved: {destination}", flush=True)


if __name__ == "__main__":
    main()
