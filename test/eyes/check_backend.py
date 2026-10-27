"""Actual backend plan delivery -> native CORE admission/ACK -> backend confirmation."""
import pathlib
import subprocess
import sys
sys.path.insert(0, sys.argv[1] + '/src')
from bridge.expressions.delivery import EyePlanDelivery
from bridge.expressions.plan import Plan, Step
from bridge.transport.protocols.conversation import Identity, encode, decode
sent = []
delivery = EyePlanDelivery(sent.append)
delivery.start(Identity(41, 2, 1, 1), Plan((Step('happy', 'soft_emphasis', 2400), Step('curious', 'question', 1800))), 0)
request, reply = pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4])
request.write_bytes(encode(sent[0]))
subprocess.run([sys.argv[2], str(request), str(reply)], check=True)
assert delivery.acknowledge(decode(reply.read_bytes()))
assert delivery.pending is None and delivery.stats['acked'] == 1
print('PASS: real backend binary eye plan -> CORE scheduler admission -> CORE ACK -> backend delivery')
