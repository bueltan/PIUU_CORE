#pragma once
#include "freertos/FreeRTOS.h"
#include <cstring>
struct StaticQueue_t { unsigned count=0,head=0,capacity=0;size_t size=0;uint8_t* bytes=nullptr; };
using QueueHandle_t=StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(unsigned capacity,size_t size,uint8_t* bytes,StaticQueue_t* q) {
    q->capacity=capacity;q->size=size;q->bytes=bytes;return q;
}
inline int xQueueSend(QueueHandle_t q,const void* p,TickType_t) {
    if(q->count==q->capacity)return pdFALSE;
    memcpy(q->bytes+((q->head+q->count)%q->capacity)*q->size,p,q->size);++q->count;return pdTRUE;
}
extern void (*beforeReceive)();
inline int xQueueReceive(QueueHandle_t q,void* p,TickType_t) {
    if(beforeReceive) { auto fn=beforeReceive;beforeReceive=nullptr;fn(); }
    if(!q->count)return pdFALSE;
    memcpy(p,q->bytes+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;--q->count;return pdTRUE;
}
