import importlib.util
import sys
from pathlib import Path
module,source,target=map(Path,sys.argv[1:4])
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
if len(sys.argv)>4:
    import subprocess
    hello=wire.decode(subprocess.check_output([sys.argv[4],'--hello']))
    assert hello.kind==wire.Kind.HELLO and hello.flags&wire.CAP_DEVICE_MAC
    assert hello.body==bytes.fromhex('02123456789a')
    assert hello.identity.server==hello.identity.turn==hello.identity.response==0
    assert wire.encode(hello)==subprocess.check_output([sys.argv[4],'--hello'])
    print('PASS: real CORE HELLO MAC payload and Python capability/endian contract')

    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        notice_path=Path(directory)/'notice.bin'
        notice_path.write_bytes(wire.encode(wire.Packet(wire.Kind.NOTICE,0,wire.Identity(700,2,0,0),123)))
        start=wire.decode(subprocess.check_output([sys.argv[4],'--notice',str(notice_path)]))
        assert start.kind==wire.Kind.START and start.identity==wire.Identity(700,2,1,0)
        assert int.from_bytes(start.body,'big')==123
        notice_path.write_bytes(wire.encode(wire.Packet(wire.Kind.NOTICE,0,wire.Identity(700,2,1,0),124)))
        reply=wire.decode(subprocess.check_output([sys.argv[4],'--standby-notice',str(notice_path)]))
        assert reply.kind==wire.Kind.NOTICE_ACK and reply.flags==1 and not reply.body
        assert reply.identity==wire.Identity(700,2,1,0) and reply.sequence==124
    print('PASS: Python NOTICE -> real CORE idle wake -> correlated START roundtrip')
    print('PASS: Python turn NOTICE -> real CORE standby admission -> NOTICE_ACK without new START')
