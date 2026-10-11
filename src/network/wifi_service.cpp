#include "network/wifi_service.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>
#include <type_traits>
#include "hardware/app_log.h"

namespace {
struct WifiRequest { WifiOperation operation; char ssid[33]; char password[65]; };
static_assert(std::is_trivially_copyable<WifiRequest>::value, "Queue copies must own their bytes");
static_assert(std::is_trivially_copyable<WifiResult>::value, "No Strings/pointers across tasks");
QueueHandle_t requests = nullptr, results = nullptr;
TaskHandle_t worker = nullptr;
bool busy = false; // Only controller reads/writes this; worker communicates via queues.

bool connect(const char *ssid, const char *password) {
    if (!ssid[0]) return false;
#if PIUU_NETWORK_DIAGNOSTICS
    const uint32_t connectStarted=millis();
    appLog().printf("[WIFI_CONNECT t=%lu] phase=begin\n",(unsigned long)connectStarted);
#endif
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(ssid, password);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - start) < 15000)
        vTaskDelay(pdMS_TO_TICKS(50));
#if PIUU_NETWORK_DIAGNOSTICS
    appLog().printf("[WIFI_CONNECT t=%lu] phase=end connected=%d status=%d elapsed_ms=%lu\n",
        (unsigned long)millis(),WiFi.status()==WL_CONNECTED,int(WiFi.status()),
        (unsigned long)uint32_t(millis()-connectStarted));
#endif
    if (WiFi.status() != WL_CONNECTED) return false;
    WiFi.setSleep(false);
    return true;
}
void run(void *) {
    WifiRequest remembered{};
    for (;;) {
        WifiRequest request{};
        if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
        WifiResult result{};
        result.operation = request.operation;
        if (request.operation == WifiOperation::Scan) {
            const bool restore = WiFi.status() == WL_CONNECTED && remembered.ssid[0];
            WiFi.mode(WIFI_STA);
            WiFi.disconnect(false, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            const int count = WiFi.scanNetworks(false, true);
            result.success = count >= 0;
            result.scan.count = count > MAX_WIFI_NETWORKS ? MAX_WIFI_NETWORKS : (count > 0 ? count : 0);
            for (int i = 0; i < result.scan.count; ++i) {
                const String name = WiFi.SSID(i);
                name.toCharArray(result.scan.entries[i].ssid, sizeof(result.scan.entries[i].ssid));
                result.scan.entries[i].rssi = WiFi.RSSI(i);
            }
            WiFi.scanDelete();
            if (restore) connect(remembered.ssid, remembered.password);
        } else {
            result.success = connect(request.ssid, request.password);
            memcpy(result.ssid, request.ssid, sizeof(result.ssid));
            memcpy(result.password, request.password, sizeof(result.password));
            if (result.success) remembered = request;
        }
        result.connected = WiFi.status() == WL_CONNECTED;
        xQueueSend(results, &result, portMAX_DELAY);
    }
}
}

bool wifiServiceStart() {
    if (worker) return true;
    requests = xQueueCreate(1, sizeof(WifiRequest));
    results = xQueueCreate(1, sizeof(WifiResult));
    if (requests && results && xTaskCreatePinnedToCore(run, "wifiWorker", 6144, nullptr, 1, &worker, 0) == pdPASS) return true;
    if (requests) vQueueDelete(requests);
    if (results) vQueueDelete(results);
    requests = results = nullptr;
    worker = nullptr;
    return false;
}
bool wifiServiceRequestConnect(const WifiCredentials &credentials, bool initial) {
    if (!requests || busy || credentials.ssid.length() == 0 || credentials.ssid.length() > 32 || credentials.password.length() > 64) return false;
    WifiRequest request{};
    request.operation = initial ? WifiOperation::InitialConnect : WifiOperation::Connect;
    credentials.ssid.toCharArray(request.ssid, sizeof(request.ssid));
    credentials.password.toCharArray(request.password, sizeof(request.password));
    if (xQueueSend(requests, &request, 0) != pdTRUE) return false;
    busy = true;
    return true;
}
bool wifiServiceRequestScan() {
    if (!requests || busy) return false;
    WifiRequest request{};
    request.operation = WifiOperation::Scan;
    if (xQueueSend(requests, &request, 0) != pdTRUE) return false;
    busy = true;
    return true;
}
bool wifiServiceTakeResult(WifiResult &result) {
    if (!results || xQueueReceive(results, &result, 0) != pdTRUE) return false;
    busy = false;
    return true;
}
bool wifiServiceBusy() { return busy; }
bool wifiServiceConnected() { return WiFi.status() == WL_CONNECTED; }
