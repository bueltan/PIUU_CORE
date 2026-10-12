"""Check the wire contract without starting the server or loading its dependencies.

Usage: python3 check_python_bridge.py SERVER_MAIN PCM_PACKET END_PACKET OPUS_OUTPUT
Only the inspected BasePacketFormat class is loaded from the supplied server.
"""
import ast
from pathlib import Path
import struct
import sys

server, pcm_file, end_file, opus_output = map(Path, sys.argv[1:])
tree = ast.parse(server.read_text())
packet_class = next((node for node in tree.body
                    if isinstance(node, ast.ClassDef) and node.name == "BasePacketFormat"), None)
if packet_class is None:
    # The modular backend re-exports the contract from bridge.transport.protocols.audio.
    # Inspect that class without importing the server or loading its dependencies.
    protocol_file = server.parent / "bridge" / "transport" / "protocols" / "audio.py"
    if not protocol_file.exists():
        protocol_file = server.parent / "bridge" / "protocol.py"
    tree = ast.parse(protocol_file.read_text())
    packet_class = next(node for node in tree.body
                        if isinstance(node, ast.ClassDef) and node.name == "BasePacketFormat")
namespace = {"struct": struct}
exec(compile(ast.Module(body=[packet_class], type_ignores=[]), str(server), "exec"), namespace)
packet_format = namespace["BasePacketFormat"](b"PCM!", 1, 1)
for path, end in [(pcm_file, False), (end_file, True)]:
    packet = packet_format.unpack_packet(path.read_bytes())
    packet_format.validate_packet(packet)
    assert packet_format.is_end_packet(packet) == end
    assert packet["seq"] == 0x12345678 and packet["pts_samples"] == 0x87654321
    if not end:
        assert packet["frame_samples"] == 320
        assert packet["payload"][:4] == struct.pack("<hh", 123, -456)
opus_format = namespace["BasePacketFormat"](b"OPUS", 1, 1)
opus_output.write_bytes(opus_format.pack_audio_packet(9, 2880, 320, bytes([11, 22, 33])))
print("PASS: Python bridge accepts CORE PCM/END; Opus fixture generated")
