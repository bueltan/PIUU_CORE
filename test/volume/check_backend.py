"""Exercise Python command encoding against the real C++ receiver/controller."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from bridge.transport.device_control import volume_body
from bridge.transport.protocols.conversation import Identity, Kind, Packet, encode, decode

for operation, arguments, level, saved in (
        ('get_volume', {}, 20, 20), ('set_volume', {'percent': 60}, 60, 20),
        ('adjust_volume', {'delta': -10}, 10, 20), ('adjust_volume', {'delta': 100}, 100, 20),
        ('set_mute', {'muted': True}, 0, 20),
        ('set_volume', {'percent': 35, 'persist': True}, 35, 35)):
    request = Packet(Kind.VOLUME, 0, Identity(700, 2, 0, 0), 1, volume_body(operation, arguments))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'command.bin'
        path.write_bytes(encode(request))
        reply = decode(subprocess.check_output([sys.argv[2], str(path)]))
    assert reply.kind == Kind.VOLUME_ACK and reply.identity == request.identity and reply.sequence == 1
    assert reply.body == bytes((0, level, level or 20, saved)), reply
print('PASS: Python/C++ volume command, state ACK and persistence interoperability')
