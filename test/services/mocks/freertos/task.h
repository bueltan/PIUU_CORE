#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
using TaskFunction_t = void(*)(void*);
int xTaskCreatePinnedToCore(TaskFunction_t,const char*,uint32_t,void*,int,TaskHandle_t*,int);
void vTaskDelay(TickType_t);
void hostStopWorkers();

void hostCheckStop();
