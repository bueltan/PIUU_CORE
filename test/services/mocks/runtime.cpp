#include <algorithm>
#include "freertos/queue.h"
#include "freertos/task.h"
#include <deque>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <atomic>
struct HostQueue { size_t capacity,itemSize; std::deque<std::vector<unsigned char>> items; std::mutex mutex; std::condition_variable cv; HostQueue(size_t c,size_t s):capacity(c),itemSize(s){} };
static std::vector<HostQueue*> queues;
static std::vector<std::thread> workers;
static std::atomic_bool stopping{false};
struct StopWorker {};
QueueHandle_t xQueueCreate(size_t count,size_t itemSize) { auto q=new HostQueue(count,itemSize); queues.push_back(q); return q; }
int xQueueSend(QueueHandle_t q,const void *data,TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if(wait) q->cv.wait(lock,[q]{return stopping || q->items.size()<q->capacity;});
    if(stopping) throw StopWorker{};
    if(q->items.size()==q->capacity) return pdFALSE;
    const auto p=static_cast<const unsigned char*>(data); q->items.emplace_back(p,p+q->itemSize); q->cv.notify_all();return pdTRUE;
}
int xQueueReceive(QueueHandle_t q,void *data,TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if(wait) q->cv.wait(lock,[q]{return stopping || !q->items.empty();});
    if(stopping) throw StopWorker{};
    if(q->items.empty()) return pdFALSE;
    memcpy(data,q->items.front().data(),q->itemSize); q->items.pop_front();q->cv.notify_all();return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { queues.erase(std::remove(queues.begin(),queues.end(),q),queues.end());delete q; }
int xTaskCreatePinnedToCore(TaskFunction_t fn,const char*,uint32_t,void *arg,int,TaskHandle_t *handle,int) {
    workers.emplace_back([fn,arg]{try{fn(arg);}catch(StopWorker&) {}});
    if(handle) *handle=reinterpret_cast<void*>(workers.size());
    return pdPASS;
}
void hostStopWorkers() { stopping=true;for(auto q:queues) { std::lock_guard<std::mutex> lock(q->mutex);q->cv.notify_all(); }for(auto &w:workers) w.join();for(auto q:queues) delete q;queues.clear(); }

void hostCheckStop() { if(stopping) throw StopWorker{}; }
int xQueuePeek(QueueHandle_t q,void *data,TickType_t) {
    std::lock_guard<std::mutex> lock(q->mutex);
    if(q->items.empty()) return pdFALSE;
    memcpy(data,q->items.front().data(),q->itemSize);return pdTRUE;
}
