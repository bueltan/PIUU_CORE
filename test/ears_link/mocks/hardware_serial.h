#pragma once
#include <mutex>
#include <vector>
#include <deque>
class HardwareSerial {
    mutable std::mutex mutex;
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
    bool started=false;
public:
    uint32_t baud=0;int rxPin=-1,txPin=-1;size_t rxBuffer=0,txBuffer=0;
    size_t setRxBufferSize(size_t n) { rxBuffer=n;return n; }
    size_t setTxBufferSize(size_t n) { txBuffer=n;return n; }
    void begin(uint32_t b,int,int r,int t) { baud=b;rxPin=r;txPin=t;started=true; }
    void end() { started=false; }
    explicit operator bool() const { return started; }
    int availableForWrite() { return 128; }
    int available() { std::lock_guard<std::mutex> lock(mutex);return rx.size(); }
    int read() { std::lock_guard<std::mutex> lock(mutex);if(rx.empty()) return -1;int b=rx.front();rx.pop_front();return b; }
    size_t write(const uint8_t *p,size_t n) { std::lock_guard<std::mutex> lock(mutex);tx.insert(tx.end(),p,p+n);return n; }
    void inject(const std::vector<uint8_t> &p) { std::lock_guard<std::mutex> lock(mutex);rx.insert(rx.end(),p.begin(),p.end()); }
    std::vector<uint8_t> output() const { std::lock_guard<std::mutex> lock(mutex);return tx; }
};
constexpr int SERIAL_8N1=0;
