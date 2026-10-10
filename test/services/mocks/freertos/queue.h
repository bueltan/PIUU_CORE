#pragma once
#include "FreeRTOS.h"
struct HostQueue;
using QueueHandle_t = HostQueue*;
QueueHandle_t xQueueCreate(size_t count,size_t itemSize);
int xQueueSend(QueueHandle_t,const void*,TickType_t);
int xQueueReceive(QueueHandle_t,void*,TickType_t);
void vQueueDelete(QueueHandle_t);

int xQueuePeek(QueueHandle_t,void*,TickType_t);
