#pragma once
#include "Arduino.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
constexpr int WIFI_STA=1, WL_CONNECTED=3, WL_DISCONNECTED=6;
class FakeWifi {
public:
    std::atomic<int> state{WL_DISCONNECTED};
    std::mutex mutex;
    std::condition_variable cv;
    bool holdConnect=false, succeed=true;
    int scanCount=15, scans=0, deleted=0;
    bool sleep=true;
    std::vector<std::pair<std::string,std::string>> connections;
    void mode(int) {}
    void disconnect(bool,bool) { state=WL_DISCONNECTED; }
    void begin(const char *ssid,const char *password) {
        std::unique_lock<std::mutex> lock(mutex);
        connections.emplace_back(ssid,password);cv.notify_all();
        cv.wait(lock,[this]{return !holdConnect;});
        state=succeed ? WL_CONNECTED : WL_DISCONNECTED;
    }
    int status() { return state.load(); }
    void setSleep(bool value) { sleep=value; }
    int scanNetworks(bool,bool) { ++scans;return scanCount; }
    String SSID(int i) { return String("scan-")+std::to_string(i); }
    int RSSI(int i) { return -40-i; }
    void scanDelete() { ++deleted; }
};
extern FakeWifi WiFi;
