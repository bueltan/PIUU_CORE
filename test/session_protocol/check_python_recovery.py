"""Exercise real CORE retransmission against the backend ingress worker, offline."""
import asyncio
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,sys.argv[2])
from bridge.config import BridgeConfig
from bridge.ingress import InboundSession
from bridge.protocol import PcmPacketFormat
from bridge.conversation_protocol import Identity,Packet,Kind,encode,decode

async def main():
    config=BridgeConfig(); fmt=PcmPacketFormat(config); output=[]; tasks=[]
    async def audio(p): output.append(p['seq']); return True
    async def end(p): pass
    async def commit(): return True
    async def recover(base,mask):
        await asyncio.sleep(.30)  # simulated request/retransmission round trip
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'nack.bin'
            path.write_bytes(encode(Packet(Kind.NACK,0,Identity(40,2,1,0),base,
                base.to_bytes(4,'big')+mask.to_bytes(2,'big'))))
            packet=decode(subprocess.check_output([sys.argv[1],str(path)]))
            assert packet.kind==Kind.PCM and packet.sequence==0
            assert session.admit(packet.body)
    def nack(base,mask): tasks.append(asyncio.create_task(recover(base,mask)))
    session=InboundSession(fmt,config,audio,end,commit,nack_sink=nack)
    session.start()
    try:
        for seq in range(1,16):
            assert session.admit(fmt.pack_audio_packet(seq,seq*320,320,bytes(640)))
            await asyncio.sleep(.02)
        await asyncio.gather(*tasks)
        for _ in range(100):
            if len(output)==16: break
            await asyncio.sleep(.005)
        assert output==list(range(16)),output
        assert session.stats.nack_frames==session.stats.nack_recovered==1,session.stats
        assert session.stats.missing==session.stats.silence_frames==0,session.stats
        print('PASS: real CORE cache -> backend NACK -> CORE retransmission -> ordered PCM (300 ms RTT)')
    finally:
        session.close()
        try: await session.task
        except asyncio.CancelledError: pass
asyncio.run(main())
