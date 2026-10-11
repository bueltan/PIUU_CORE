#include "network/network_udp.h"
#include "WiFiUdp.h"
#include "diagnostics/config.h"
#include <cassert>
#include <cstdio>
Logger Serial;
UdpMock udpMock;
uint32_t millis() { return 1000; }
static uint8_t bytes[4]={};
static void busySend() { assert(!networkUdpSendRaw("host",9999,bytes,4)); }
int main() {
    assert(!networkUdpSendRaw("host",9999,bytes,4));
    assert(networkUdpInit(9999));
    assert(networkUdpSendRaw("host",9999,bytes,4));
    udpMock.begun=0;
    assert(!networkUdpSendRaw("host",9999,bytes,4));
    assert(udpMock.writes==1 && udpMock.ends==1); // No stale/old destination send.
    udpMock.begun=1;udpMock.written=2;
    assert(!networkUdpSendRaw("host",9999,bytes,4));
    udpMock.written=4;udpMock.ended=0;
    assert(!networkUdpSendRaw("host",9999,bytes,4));
    udpMock.onParse=busySend;assert(networkUdpParsePacket()==4);
    udpMock.readSize=0;assert(networkUdpRead(bytes,4)==0);
    udpMock.readSize=4;assert(networkUdpRead(bytes,4)==4);
    networkUdpLogDiagnostics();
#if PIUU_NETWORK_DIAGNOSTICS
    for(const char *field: {"tx_ok=1 ","tx_uninit=1 ","tx_busy=1 ","tx_begin_fail=1 ",
                           "tx_short=1 ","tx_end_fail=1 ","rx_empty=1 ","rx_packets=1 ","rx_bytes=4"})
        assert(Serial.output.find(field)!=std::string::npos);
#else
    assert(Serial.output.empty());
#endif
    puts("PASS: local UDP failure classification, short writes, busy socket and RX counters");
}
