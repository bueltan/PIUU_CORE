"""Actual C++ NACK -> Python resend cache -> C++ validated END envelope."""
import sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from bridge.config import BridgeConfig
from bridge.transport.protocols.conversation import Kind, Packet, decode, encode
from bridge.transport.downlink_recovery import DownlinkRecovery
from bridge.transport.protocols.audio import OpusPacketFormat
request=decode(Path(sys.argv[2]).read_bytes())
assert request.kind==Kind.DOWNLINK_NACK
sent=[];recovery=DownlinkRecovery(BridgeConfig(),lambda *p:sent.append(p))
fmt=OpusPacketFormat(BridgeConfig())
for seq in range(9): recovery.remember(request.identity,seq,fmt.pack_audio_packet(seq,seq*320,320,b'abc'),0)
assert recovery.request(request,.3)
recovery.poll(.32)
assert [p[2] for p in sent]==[7,8]
for p in sent: assert p[3]==fmt.pack_audio_packet(p[2],p[2]*320,320,b'abc')
recovery.finish(request.identity,9,fmt.pack_end_packet(9,2880),.34)
kind,token,sequence,body=sent[-1]
assert kind==Kind.AUDIO_END
Path(sys.argv[3]).write_bytes(encode(Packet(kind,0,token,sequence,body)))
assert recovery.acknowledge(Packet(Kind.AUDIO_END_ACK,0,token,sequence))
print('PASS: actual CORE downlink NACK -> backend bounded resend -> authoritative END/ACK wire')
