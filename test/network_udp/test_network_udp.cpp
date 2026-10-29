#include "network/network_udp.h"
#include "WiFiUdp.h"
#include "diagnostics/config.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
Logger Serial;
UdpMock udpMock;
uint32_t millis() { return 1000; }
static uint8_t bytes[4]={};
static void busySend() { assert(!networkUdpSendRaw("host",9999,bytes,4)); }
static std::mutex sendMutex;
static std::condition_variable sendCv;
static bool sending=false, releaseSend=false;
static void holdSend() {
    std::unique_lock<std::mutex> lock(sendMutex);
    sending=true;sendCv.notify_all();
    sendCv.wait(lock,[]{return releaseSend;});
}
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
    udpMock.packets.push_back({1,2,3,4});
    udpMock.onParse=busySend;assert(networkUdpParsePacket()==4);udpMock.onParse=nullptr;
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
    // The sender wins the mutex after parsePacket staged a datagram, before
    // read can take the lock. The RX task's next poll must retry that same data.
    udpMock.readSize=-1;udpMock.ended=1;
    udpMock.packets.push_back({9,8,7,6});udpMock.packets.push_back({5,4,3,2});
    assert(networkUdpParsePacket()==4);
    udpMock.onEnd=holdSend;
    std::thread sender([]{assert(networkUdpSendRaw("host",9999,bytes,4));});
    {
        std::unique_lock<std::mutex> lock(sendMutex);
        assert(sendCv.wait_for(lock,std::chrono::seconds(2),[]{return sending;}));
    }
    assert(networkUdpParsePacket()==0); // Parse lock miss leaves the buffer untouched.
    assert(networkUdpRead(bytes,4)==0); // Read lock miss reproduces the original fault.
    { std::lock_guard<std::mutex> lock(sendMutex);releaseSend=true;sendCv.notify_all(); }
    sender.join();udpMock.onEnd=nullptr;
    assert(networkUdpParsePacket()==4);
    assert(networkUdpParsePacket()==4); // Repeated polling doesn't consume the datagram.
    assert(networkUdpRead(bytes,4)==4 && !memcmp(bytes,"\x09\x08\x07\x06",4));
    assert(networkUdpParsePacket()==4);
    assert(networkUdpRead(bytes,4)==4 && !memcmp(bytes,"\x05\x04\x03\x02",4));
    assert(networkUdpParsePacket()==0);

    // A zero-byte read is retryable and cannot wedge future polling either.
    udpMock.packets.push_back({1,2,3,4});assert(networkUdpParsePacket()==4);
    udpMock.readSize=0;assert(networkUdpRead(bytes,4)==0);
    assert(networkUdpParsePacket()==4);
    udpMock.readSize=-1;assert(networkUdpRead(bytes,4)==4);

    // Oversize/partial datagrams are discarded whole; their tail must never be
    // reinterpreted as another application packet or block the following one.
    udpMock.packets.push_back({0,1,2,3,4});udpMock.packets.push_back({8,7,6,5});
    assert(networkUdpParsePacket()==5 && networkUdpRead(bytes,4)==0);
    assert(networkUdpParsePacket()==4);
    udpMock.readSize=2;assert(networkUdpRead(bytes,4)==0);udpMock.readSize=-1;
    assert(networkUdpParsePacket()==0);
    udpMock.packets.push_back({4,3,2,1});assert(networkUdpParsePacket()==4);
    assert(networkUdpRead(bytes,4)==4 && !memcmp(bytes,"\x04\x03\x02\x01",4));
    assert(networkUdpParsePacket()==0);
#if PIUU_NETWORK_DIAGNOSTICS
    networkUdpLogDiagnostics();
    for(const char *field: {"rx_parse_busy=1 ","rx_read_busy=1 ","rx_pending=3 ","rx_oversized=1 ","rx_short=1"})
        assert(Serial.output.find(field)!=std::string::npos);
#else
    assert(Serial.output.empty());
#endif
    networkUdpLogPlaybackFault(7);
#if PIUU_NETWORK_DIAGNOSTICS || PIUU_PLAYBACK_DIAGNOSTICS
    for(const char *field: {"[UDP_FAULT", "fault=7 ", "parse_busy=1 ", "read_busy=1 ", "oversized=1 ", "short=1"})
        assert(Serial.output.find(field)!=std::string::npos);
#else
    assert(Serial.output.empty());
#endif
    puts("PASS: UDP mutex contention recovery, buffered/zero-byte retries, intact datagrams, oversize/short discard and I/O diagnostics");
}
