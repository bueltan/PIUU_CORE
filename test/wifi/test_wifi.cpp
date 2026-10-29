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
        assert(WiFi.connections[0].second=="original" && !WiFi.sleep && !WiFi.autoReconnect);
    }
    assert(wifiServiceBusy() && !wifiServiceRequestScan() && !wifiServiceRequestConnect({"second",""}));
    WifiResult out{};assert(!wifiServiceTakeResult(out)); // loop can continue while worker is blocked
    { std::lock_guard<std::mutex> lock(WiFi.mutex);WiFi.holdConnect=false;WiFi.cv.notify_all(); }
    out=result();assert(out.operation==WifiOperation::InitialConnect && out.success && out.connected && !wifiServiceBusy());
    assert(std::string(out.password)=="original" && !WiFi.sleep);
    assert(WiFi.autoReconnect);
    const int disconnectsBeforeScan=WiFi.disconnects;
    assert(wifiServiceRequestScan());out=result();
    assert(out.operation==WifiOperation::Scan && out.success && out.connected && out.scan.count==10);
    assert(std::string(out.scan.entries[9].ssid)=="scan-9" && out.scan.entries[9].rssi==-49);
    assert(WiFi.scans==1 && WiFi.deleted==1 && WiFi.connections.size()==1 && WiFi.disconnects==disconnectsBeforeScan);
    WiFi.succeed=false;
    assert(wifiServiceRequestConnect({"bad","wrong"}));out=result();assert(!out.success && !out.connected && !wifiServiceBusy());
    assert(!WiFi.pendingConnect && WiFi.autoReconnect); // Timeout cancels the old attempt.
    WiFi.pendingConnect=true; // Arduino started an offline retry before the next UI job.
    assert(wifiServiceRequestScan());out=result();
    assert(out.success && !out.connected && !WiFi.pendingConnect && WiFi.autoReconnect);
    assert(!wifiServiceRequestConnect({"", ""}));
    assert(!wifiServiceRequestConnect({String(33,'s'),""}));
    assert(!wifiServiceRequestConnect({"valid",String(65,'p')}));
    // Late driver recovery after an application timeout must survive a scan.
    WiFi.state=WL_CONNECTED;
    const auto connectionsBeforeScan=WiFi.connections.size();
    WiFi.scanCount=-2;
    assert(wifiServiceRequestScan());out=result();assert(!out.success && out.connected && out.scan.count==0);
    assert(WiFi.connections.size()==connectionsBeforeScan);
    // Failed cancellation cannot be followed by a new connect or scan.
    WiFi.disconnectOk=false;
    assert(wifiServiceRequestConnect({"new","password"}));out=result();
    assert(!out.success && out.connected && WiFi.connections.size()==connectionsBeforeScan && WiFi.autoReconnect);
    WiFi.state=WL_DISCONNECTED;WiFi.pendingConnect=true;
    const auto scansBeforeFailure=WiFi.scans;
    assert(wifiServiceRequestScan());out=result();
    assert(!out.success && WiFi.scans==scansBeforeFailure && WiFi.autoReconnect);
    WiFi.disconnectOk=true;
    // Preserve an explicitly disabled reconnect policy on every exit path.
    WiFi.autoReconnect=false;WiFi.modeOk=false;
    assert(wifiServiceRequestConnect({"new","password"}));out=result();
    assert(!out.success && !WiFi.autoReconnect && WiFi.connections.size()==connectionsBeforeScan);
    WiFi.modeOk=true;WiFi.autoReconnect=true;
    WiFi.succeed=true;
    assert(wifiServiceRequestConnect({"open",""}));out=result();assert(out.success && std::string(out.password).empty());
    assert(WiFi.autoReconnect && WiFi.reconnectConflicts==0);
    hostStopWorkers();
    puts("PASS: nonblocking WiFi jobs, serialization, copied credentials, bounded scans, connected scans without disconnect and failure recovery");
}
