#include "input/touch_input.h"
#include <Arduino.h>
#include "hardware/touch_hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
static QueueHandle_t events = nullptr;
static void taskTouch(void*) {
    bool down = false;
    uint16_t lastX=0, lastY=0;
    for (;;) {
        uint16_t x=0,y=0;
        bool touching = getTouchPoint(x,y);
        if (touching) { lastX=x; lastY=y; }
        if (touching != down) {
            TouchEvent event{touching ? TouchEdge::Down : TouchEdge::Up, lastX,lastY,millis()};
            // Preserve both edges; only this input worker may wait on a full queue.
            xQueueSend(events, &event, portMAX_DELAY);
            down = touching;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
bool startTouchTask() {
    if (events) return true;
    events = xQueueCreate(16, sizeof(TouchEvent));
    if (!events) return false;
    if (xTaskCreatePinnedToCore(taskTouch,"touch",3072,nullptr,1,nullptr,1) != pdPASS) {
        vQueueDelete(events); events=nullptr; return false;
    }
    return true;
}
bool takeTouchEvent(TouchEvent &event) { return events && xQueueReceive(events,&event,0)==pdTRUE; }
