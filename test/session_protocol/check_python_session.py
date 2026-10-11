import importlib.util
import sys
from pathlib import Path
module,source,target=map(Path,sys.argv[1:])
spec=importlib.util.spec_from_file_location('session_wire',module)
wire=importlib.util.module_from_spec(spec);sys.modules[spec.name]=wire;spec.loader.exec_module(wire)
packet=wire.decode(source.read_bytes())
assert packet.identity==wire.Identity(0x0102030405060708,0x1112131415161718,0x21222324,0x31323334)
assert packet.kind==wire.Kind.PCM and packet.sequence==0x41424344 and packet.body==bytes([1,2,3])
assert wire.encode(packet)==source.read_bytes()
for data in (source.read_bytes()[:37],source.read_bytes()+b'X',b'PCM!'+source.read_bytes()[4:]):
    try: wire.decode(data)
    except ValueError: pass
    else: raise AssertionError('Malformed envelope accepted')
target.write_bytes(wire.encode(wire.Packet(wire.Kind.NACK,0,wire.Identity(1,2,9,0),7,b'\0\0\0\5\0\3')))
print('PASS: C++/Python v3 identities, endian layout, NACK and malformed envelopes')
