#pragma once
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>
enum hardwareSerial_error_t { UART_NO_ERROR,UART_BREAK_ERROR,UART_BUFFER_FULL_ERROR,
    UART_FIFO_OVF_ERROR,UART_FRAME_ERROR,UART_PARITY_ERROR };
class HardwareSerial {
    mutable std::mutex mutex;
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
    bool started=false;
public:
    void (*errorCallback)(hardwareSerial_error_t)=nullptr;
    void onReceiveError(void (*callback)(hardwareSerial_error_t)) { errorCallback=callback; }
    void emitError(hardwareSerial_error_t error) { if(errorCallback)errorCallback(error); }
    std::atomic<size_t> byteReads{0},blockReads{0},bytesRead{0},maxRequested{0};
    std::atomic<size_t> readLimit{256}; // Exercise short reads from the driver.
    uint32_t baud=0;int rxPin=-1,txPin=-1;size_t rxBuffer=0,txBuffer=0;
    uint8_t fifoThreshold=120;
    size_t setRxBufferSize(size_t n) { rxBuffer=n;return n; }
    size_t setTxBufferSize(size_t n) { txBuffer=n;return n; }
    void begin(uint32_t b,int,int r,int t) { baud=b;rxPin=r;txPin=t;fifoThreshold=120;started=true; }
    bool setRxFIFOFull(uint8_t threshold) { if(!started)return false;fifoThreshold=threshold;return true; }
    void end() { started=false; }
    explicit operator bool() const { return started; }
    int availableForWrite() { return 128; }
    int available() { std::lock_guard<std::mutex> lock(mutex);return rx.size(); }
    int read() { ++byteReads;std::lock_guard<std::mutex> lock(mutex);if(rx.empty()) return -1;int b=rx.front();rx.pop_front();return b; }
    size_t read(uint8_t *buffer,size_t size) {
        ++blockReads;
        std::lock_guard<std::mutex> lock(mutex);
        if (size>maxRequested.load()) maxRequested.store(size);
        size_t count=0;
        while (count<size && count<readLimit.load() && !rx.empty()) {
            buffer[count++]=rx.front();rx.pop_front();
        }
        bytesRead.fetch_add(count);return count;
    }
    size_t write(const uint8_t *p,size_t n) { std::lock_guard<std::mutex> lock(mutex);tx.insert(tx.end(),p,p+n);return n; }
    void inject(const std::vector<uint8_t> &p) { std::lock_guard<std::mutex> lock(mutex);rx.insert(rx.end(),p.begin(),p.end()); }
    std::vector<uint8_t> output() const { std::lock_guard<std::mutex> lock(mutex);return tx; }
};
constexpr int SERIAL_8N1=0;
