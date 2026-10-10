#include "hardware/display_hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static TFT_eSPI tft;
static TaskHandle_t owner = nullptr;
void initDisplayHardware() {
    owner = xTaskGetCurrentTaskHandle();
    tft.init(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);
}
void screenLock() { configASSERT(owner && owner == xTaskGetCurrentTaskHandle()); }
void screenUnlock() { screenLock(); }
TFT_eSPI& display() { screenLock(); return tft; }
