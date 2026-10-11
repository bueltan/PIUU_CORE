#!/usr/bin/env python3
"""Wait for the three USB consoles and capture them without resetting firmware."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import threading


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class LineLog:
    def __init__(self, output):
        self.output = output
        self.pending = bytearray()

    def write_line(self, data):
        text = data.decode("utf-8", errors="replace").rstrip("\r")
        self.output.write(f"[{timestamp()}] {text}\n")
        self.output.flush()

    def feed(self, data):
        self.pending.extend(data)
        while b"\n" in self.pending:
            line, _, rest = self.pending.partition(b"\n")
            self.pending = bytearray(rest)
            self.write_line(line)
        # Bound memory even if a faulty device never sends a newline.
        if len(self.pending) >= 8192:
            self.finish()

    def finish(self):
        if self.pending:
            self.write_line(self.pending)
            self.pending.clear()


def capture_port(name, port, destination, stop, report, serial_module, retry=0.1):
    """Independent worker per console; a missing board never blocks the others."""
    with destination.open("a", encoding="utf-8", buffering=1) as output:
        log = LineLog(output)
        previous_error = None
        report(f"{name}: esperando {port}")
        while not stop.is_set():
            connection = None
            try:
                # Set modem lines before opening: never issue an intentional reset
                # or a bootloader sequence. Some USB drivers can still pulse lines.
                connection = serial_module.Serial(
                    port=None, baudrate=115200, timeout=0.25, exclusive=True
                )
                connection.dtr = False
                connection.rts = False
                connection.port = port
                connection.open()
                previous_error = None
                report(f"{name}: conectado a {port}")
                log.write_line(b"[CAPTURE] connected")
                while not stop.is_set():
                    data = connection.read(min(connection.in_waiting or 1, 4096))
                    if data:
                        log.feed(data)
            except (serial_module.SerialException, OSError) as error:
                log.finish()
                message = str(error)
                if message != previous_error:
                    report(f"{name}: puerto no disponible: {message}; reintentando")
                    log.write_line(b"[CAPTURE] port unavailable; waiting for reconnect")
                    previous_error = message
            finally:
                log.finish()
                if connection is not None:
                    connection.close()
            stop.wait(retry)
        log.write_line(b"[CAPTURE] stopped")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", default="/dev/esp32-core")
    parser.add_argument("--ears", default="/dev/esp32-ears")
    parser.add_argument("--keyword", default="/dev/esp32-keyword")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "logs" / "integrated")
    args = parser.parse_args()
    ports = [(name, getattr(args, name)) for name in ("core", "keyword", "ears")]
    resolved = [str(Path(port).resolve()) for _, port in ports]
    if len(set(resolved)) != len(resolved):
        parser.error("Cada placa necesita un puerto diferente")
    try:
        import serial
    except ImportError:
        parser.error("Falta pyserial: usa /home/denis/.platformio/penv/bin/python")
    session = args.output / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    session.mkdir(parents=True, exist_ok=False)
    stop = threading.Event()
    report_lock = threading.Lock()
    workers = []
    failures = []
    with (session / "capture.log").open("a", encoding="utf-8", buffering=1) as events:
        def report(message):
            with report_lock:
                line = f"[{timestamp()}] {message}"
                print(line, flush=True)
                events.write(line + "\n")

        def run_worker(name, port):
            try:
                capture_port(name, port, session / f"{name}.log", stop, report, serial)
            except Exception as error:
                failures.append(name)
                report(f"{name}: fallo de captura: {error}")
                stop.set()

        report(f"Guardando en {session}; Ctrl+C para terminar")
        for name, port in ports:
            worker = threading.Thread(
                target=run_worker,
                args=(name, port),
                name=f"capture-{name}",
            )
            worker.start()
            workers.append(worker)
        try:
            while any(worker.is_alive() for worker in workers):
                stop.wait(0.25)
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            for worker in workers:
                worker.join()
        report("Captura terminada")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
