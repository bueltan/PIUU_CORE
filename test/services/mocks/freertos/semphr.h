#pragma once
#include "FreeRTOS.h"
#include <mutex>
using SemaphoreHandle_t=std::timed_mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::timed_mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t s,TickType_t wait) {
    if(wait==portMAX_DELAY) { s->lock();return pdTRUE; }
    return s->try_lock() ? pdTRUE : pdFALSE;
}
inline void xSemaphoreGive(SemaphoreHandle_t s) { s->unlock(); }
inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
