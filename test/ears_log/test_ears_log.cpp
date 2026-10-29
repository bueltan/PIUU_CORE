#include "hardware/ears_log.h"
#include "diagnostics/core_log_wire.h"
#include "diagnostics/wifi_history.h"
#include <cassert>
#include <fstream>
#include <thread>
#include <cstring>
#include <cstdio>
Esp ESP;
static uint32_t now=0;
uint32_t millis() { return now; }
namespace WifiDiagnostics {
static bool pending=false;
bool next(uint64_t&,char *line,size_t,uint32_t &ms) {
 if(!pending)return false;
 pending=false;strcpy(line,"[WIFI_HISTORY] event=disconnected reason=204\n");ms=7;return true;
}
}
int main(int argc,char **argv) {
 assert(argc==2);earsLogBegin();HardwareSerial serial;
 uint8_t text[192];memset(text,'A',sizeof(text));
 // Saturation retains recent chunks and counts lost bytes without allocating.
 for(unsigned i=0;i<10;++i)assert(earsAppLog().write(text,sizeof(text))==sizeof(text));
 now=40;serial.space=226;earsLogPoll(serial);assert(serial.bytes.empty()); // reserve command space
 serial.space=256;earsLogPoll(serial);assert(serial.bytes.size()>8);
 const size_t first=serial.bytes.size();earsLogPoll(serial);assert(serial.bytes.size()==first); // rate cap
 WifiDiagnostics::pending=true;now+=40;earsLogPoll(serial); // WiFi history independent of ordinary overflow
 now+=40;earsLogPoll(serial);
 std::thread a([&]{for(int i=0;i<1000;++i)earsAppLog().write(text,sizeof(text));});
 std::thread b([&]{for(int i=0;i<1000;++i)earsAppLog().write(text,sizeof(text));});a.join();b.join();
 for(unsigned i=0;i<8;++i) { now+=40;earsLogPoll(serial); }
 // A huge producer call is bounded to 1024 bytes, remaining bytes count as lost.
 std::vector<uint8_t> huge(9000,'Z');earsAppLog().write(huge.data(),huge.size());
 for(unsigned i=0;i<5;++i) { now+=40;earsLogPoll(serial); }
 unsigned count=0;uint32_t lost=0;
 for(size_t offset=0;offset<serial.bytes.size();) {
  const auto *p=serial.bytes.data()+offset;assert(!memcmp(p,"CMD!",4) && p[4]==1 && p[5]==0x10);
  const size_t n=p[6]+256*p[7];assert(offset+n+8<=serial.bytes.size() && CoreLogWire::valid(p+8,n));
  assert(CoreLogWire::get32(p+8)==0x1234abcd && CoreLogWire::get32(p+12)==++count);
  assert(p[24]==12);const auto dropped=CoreLogWire::get32(p+20);assert(dropped>=lost);lost=dropped;
  offset+=n+8;
 }
 assert(count>=8 && lost>390000);
 std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<const char*>(serial.bytes.data()),serial.bytes.size());
 puts("PASS: bounded CORE log queue, concurrent producers, WiFi history, rate/capacity limits and framed metadata");
}
