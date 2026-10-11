"""Exercise the actual pyserial reader against a virtual Linux serial port."""
import importlib.util
import io
import os
from pathlib import Path
import pty
import tempfile
import threading
import time
import unittest
import serial

SOURCE = Path(__file__).resolve().parents[2] / "tools" / "capture_serial.py"
spec = importlib.util.spec_from_file_location("capture_serial", SOURCE)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


def wait_for(predicate):
    deadline = time.monotonic() + 3
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError("Timeout esperando captura")
        time.sleep(0.01)


class CaptureTest(unittest.TestCase):
    def test_partial_utf8_and_bounded_lines(self):
        output = io.StringIO()
        log = capture.LineLog(output)
        encoded = "conexión\r\n".encode()
        log.feed(encoded[:7])
        self.assertEqual(output.getvalue(), "")
        log.feed(encoded[7:])
        self.assertIn("conexión\n", output.getvalue())
        log.feed(b"x" * 8192)
        self.assertEqual(len(log.pending), 0)
        log.feed(b"ultimo")
        log.finish()
        self.assertIn("ultimo\n", output.getvalue())

    def test_absent_connect_disconnect_reconnect_and_stop(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            port, output = root / "console", root / "core.log"
            stop = threading.Event()
            messages = []
            worker = threading.Thread(target=capture.capture_port,
                args=("core", str(port), output, stop, messages.append, serial, 0.02))
            descriptors = []
            worker.start()
            try:
                wait_for(lambda: any("no disponible" in message for message in messages))
                for sequence in range(2):
                    master, slave = pty.openpty()
                    descriptors.extend([master, slave])
                    if port.is_symlink():
                        port.unlink()
                    port.symlink_to(os.ttyname(slave))
                    wait_for(lambda: sum("conectado a" in m for m in messages) == sequence + 1)
                    os.write(master, f"BOOT-{sequence}\n".encode())
                    wait_for(lambda: f"BOOT-{sequence}" in output.read_text())
                    if sequence == 0:
                        os.write(master, b"tail-without-newline")
                        time.sleep(0.05)
                        port.unlink()
                        os.close(master)
                        descriptors.remove(master)
                        wait_for(lambda: "tail-without-newline" in output.read_text())
            finally:
                stop.set()
                worker.join(timeout=2)
                for descriptor in descriptors:
                    os.close(descriptor)
            self.assertFalse(worker.is_alive())
            self.assertIn("[CAPTURE] stopped", output.read_text())


if __name__ == "__main__":
    unittest.main()
