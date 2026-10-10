#include "network/wifi_service.h"
#include "freertos/task.h"
#include "WiFi.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
FakeWifi WiFi;
Logger Serial;
static std::atomic<uint32_t> now{1000};
uint32_t millis() { return now.load(); }
void vTaskDelay(TickType_t ticks) { now+=ticks;std::this_thread::yield(); }
static WifiResult result() {
    WifiResult out{};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!wifiServiceTakeResult(out)) { assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield(); }
    return out;
}
int main() {
    assert(!wifiServiceRequestScan());
    assert(wifiServiceStart());assert(wifiServiceStart());
    WifiCredentials draft{"first","original"};
    { std::lock_guard<std::mutex> lock(WiFi.mutex);WiFi.holdConnect=true; }
    const auto start=std::chrono::steady_clock::now();
    assert(wifiServiceRequestConnect(draft,true));draft.password="changed after enqueue";
    assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
    {
        std::unique_lock<std::mutex> lock(WiFi.mutex);
        assert(WiFi.cv.wait_for(lock,std::chrono::seconds(3),[]{return !WiFi.connections.empty();}));
        assert(WiFi.connections[0].second=="original");
    }
    assert(wifiServiceBusy() && !wifiServiceRequestScan() && !wifiServiceRequestConnect({"second",""}));
    WifiResult out{};assert(!wifiServiceTakeResult(out)); // loop can continue while worker is blocked
    { std::lock_guard<std::mutex> lock(WiFi.mutex);WiFi.holdConnect=false;WiFi.cv.notify_all(); }
    out=result();assert(out.operation==WifiOperation::InitialConnect && out.success && out.connected && !wifiServiceBusy());
    assert(std::string(out.password)=="original" && !WiFi.sleep);
    assert(wifiServiceRequestScan());out=result();
    assert(out.operation==WifiOperation::Scan && out.success && out.connected && out.scan.count==10);
    assert(std::string(out.scan.entries[9].ssid)=="scan-9" && out.scan.entries[9].rssi==-49);
    assert(WiFi.scans==1 && WiFi.deleted==1 && WiFi.connections.size()==2 && WiFi.connections[1].second=="original");
    WiFi.succeed=false;
    assert(wifiServiceRequestConnect({"bad","wrong"}));out=result();assert(!out.success && !out.connected && !wifiServiceBusy());
    assert(!wifiServiceRequestConnect({"", ""}));
    assert(!wifiServiceRequestConnect({String(33,'s'),""}));
    assert(!wifiServiceRequestConnect({"valid",String(65,'p')}));
    WiFi.succeed=true;
    assert(wifiServiceRequestConnect({"open",""}));out=result();assert(out.success && std::string(out.password).empty());
    WiFi.scanCount=-2;
    assert(wifiServiceRequestScan());out=result();assert(!out.success && out.connected && out.scan.count==0);
    hostStopWorkers();
    puts("PASS: nonblocking WiFi jobs, serialization, copied credentials, bounded scans, restoration and failure recovery");
}
