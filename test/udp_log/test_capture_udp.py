import importlib.util
import io
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('capture_udp', ROOT / 'tools/capture_udp.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def packet(seq, payload=b'hello\n', boot=1, dropped=0):
    return struct.pack('!4sIIIIHH', b'PLG1', boot, seq, dropped, 123, len(payload), 0) + payload


class Tests(unittest.TestCase):
    def test_validation(self):
        for p in (b'', packet(1)[:-1], packet(1) + b'x', packet(1, b''), packet(1, b'x'*193), b'BAD!' + packet(1)[4:]):
            with self.assertRaises(ValueError):
                m.decode(p)

    def test_loss_reorder_restart_and_fragmented_utf8(self):
        out = io.StringIO()
        c = m.Capture(out)
        c.accept(packet(0xFFFFFFFF, b'caf\xc3'))
        c.accept(packet(0, b'\xa9\n'))
        self.assertFalse(c.accept(packet(0, b'duplicate\n')))
        self.assertFalse(c.accept(packet(0xFFFFFFFF, b'old\n')))
        c.accept(packet(2, b'partial', dropped=9))
        c.accept(packet(1, b'new boot\n', boot=2))
        self.assertFalse(c.accept(packet(3, boot=1)))
        text = out.getvalue()
        for expected in ('café', 'missing_datagrams=1', 'core_dropped_bytes_total=9', 'partial [partial]', 'new boot'):
            self.assertIn(expected, text)
        self.assertNotIn('duplicate', text)

    def test_cpp_wire_compatibility(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'wire.cpp'
            exe = Path(directory) / 'wire'
            source.write_text('''#include "diagnostics/log_wire.h"
#include <cassert>
#include <cstdio>
int main(){unsigned char b[216];const unsigned char p[]={65,10};
assert(!LogWire::encode(b,1,p,2,1,2,3,4));
assert(!LogWire::encode(b,sizeof(b),p,193,1,2,3,4));
auto n=LogWire::encode(b,sizeof(b),p,2,1,2,3,4);fwrite(b,1,n,stdout);}
''')
            subprocess.run(['g++', '-std=c++17', '-I'+str(ROOT/'src'), str(source), '-o', str(exe)], check=True)
            self.assertEqual(m.decode(subprocess.check_output([str(exe)])), (1, 2, 3, 4, b'A\n'))

    def test_real_udp_subscription_and_capture(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            server.settimeout(5)
            output = Path(directory) / 'core.log'
            proc = subprocess.Popen([sys.executable, str(ROOT/'tools/capture_udp.py'), '--host', '127.0.0.1', '--port', str(server.getsockname()[1]), '--duration', '1', '--output', str(output)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                data, peer = server.recvfrom(128)
                self.assertEqual(data, m.SUBSCRIBE)
                server.sendto(packet(1, b'[CORE] UDP test\n'), peer)
                stdout, stderr = proc.communicate(timeout=5)
                self.assertEqual(proc.returncode, 0, stderr)
                self.assertIn('[CORE] UDP test', output.read_text())
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()


if __name__ == '__main__':
    unittest.main()
