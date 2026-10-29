#pragma once
#include "Arduino.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
constexpr int WIFI_STA=1, WL_CONNECTED=3, WL_DISCONNECTED=6;
enum WiFiEvent_t { ARDUINO_EVENT_WIFI_STA_START, ARDUINO_EVENT_WIFI_STA_CONNECTED,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_GOT_IP,
    ARDUINO_EVENT_WIFI_STA_LOST_IP };
struct WiFiEventInfo_t {
    struct { int channel=0, authmode=0; } wifi_sta_connected;
    struct { int reason=0; } wifi_sta_disconnected;
    struct { struct { struct { uint32_t addr=0; } ip; } ip_info; } got_ip;
};
class FakeWifi {
public:
    void (*callback)(WiFiEvent_t, WiFiEventInfo_t)=nullptr;
    void onEvent(void (*cb)(WiFiEvent_t, WiFiEventInfo_t)) { callback=cb; }
    std::atomic<int> state{WL_DISCONNECTED};
    std::mutex mutex;
    std::condition_variable cv;
    bool holdConnect=false, succeed=true;
    int scanCount=15, scans=0, deleted=0;
    bool sleep=true;
    std::atomic<bool> autoReconnect{true};
    bool modeOk=true, disconnectOk=true, pendingConnect=false;
    int reconnectConflicts=0;
    std::vector<std::pair<std::string,std::string>> connections;
    bool mode(int) { return modeOk; }
    bool getAutoReconnect() { return autoReconnect; }
    bool setAutoReconnect(bool value) { autoReconnect=value;return true; }
    int disconnects=0;
    bool disconnect(bool,bool,unsigned long=100) {
        ++disconnects;
        if(autoReconnect) ++reconnectConflicts;
        if(!disconnectOk) return false;
        state=WL_DISCONNECTED;pendingConnect=false;return true;
    }
    int begin(const char *ssid,const char *password) {
        std::unique_lock<std::mutex> lock(mutex);
        if(autoReconnect || pendingConnect) ++reconnectConflicts;
        connections.emplace_back(ssid,password);cv.notify_all();
        cv.wait(lock,[this]{return !holdConnect;});
        state=succeed ? WL_CONNECTED : WL_DISCONNECTED;
        pendingConnect=!succeed;
        return state.load();
    }
    int status() { return state.load(); }
    bool setSleep(bool value) { sleep=value;return true; }
    int scanNetworks(bool,bool) {
        ++scans;
        if(autoReconnect || pendingConnect) { ++reconnectConflicts;return -2; }
        return scanCount;
    }
    String SSID(int i) { return String("scan-")+std::to_string(i); }
    int RSSI(int i) { return -40-i; }
    void scanDelete() { ++deleted; }
};
extern FakeWifi WiFi;
