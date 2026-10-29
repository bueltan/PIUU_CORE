#include "network/wifi_service.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>
#include <type_traits>
#include "hardware/app_log.h"
#include "diagnostics/wifi_history.h"
#if PIUU_WIFI_DIAGNOSTICS
#include <esp_wifi.h>
#include <esp_netif.h>
#endif
#if PIUU_EARS_LOG
#include <esp_heap_caps.h>
#endif

namespace {
struct WifiRequest { WifiOperation operation; char ssid[33]; char password[65]; };
static_assert(std::is_trivially_copyable<WifiRequest>::value, "Queue copies must own their bytes");
static_assert(std::is_trivially_copyable<WifiResult>::value, "No Strings/pointers across tasks");
QueueHandle_t requests = nullptr, results = nullptr;
TaskHandle_t worker = nullptr;
constexpr uint32_t ConnectTimeoutMs=60000; // Diagnostic trial: one uninterrupted attempt.
bool busy = false; // Only controller reads/writes this; worker communicates via queues.

// The worker owns explicit connect/scan jobs. Arduino may otherwise start a
// reconnect from its event task while we are cancelling or scanning.
class ReconnectPause {
public:
    ReconnectPause(): previous(WiFi.getAutoReconnect()) { WiFi.setAutoReconnect(false); }
    ~ReconnectPause() { WiFi.setAutoReconnect(previous); }
private:
    bool previous;
};
bool stopAttempt() {
    if (!WiFi.disconnect(false, false, 1000)) {
        WifiDiagnostics::record(WifiDiagnostics::Event::SetupFailed, 3);
        return false;
    }
    // Let the intentional disconnect event finish before starting another job.
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

#if PIUU_WIFI_DIAGNOSTICS
void logConnectWait(uint32_t start,bool final) {
    wifi_ap_record_t ap{};
    const bool associated=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
    esp_netif_dhcp_status_t dhcp=ESP_NETIF_DHCP_INIT;
    esp_netif_ip_info_t ip{};
    esp_netif_t *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const esp_err_t dhcpError=netif?esp_netif_dhcpc_get_status(netif,&dhcp):ESP_ERR_INVALID_STATE;
    const esp_err_t ipError=netif?esp_netif_get_ip_info(netif,&ip):ESP_ERR_INVALID_STATE;
    const char *state=dhcpError!=ESP_OK?"unknown":
        dhcp==ESP_NETIF_DHCP_STARTED?"started":dhcp==ESP_NETIF_DHCP_STOPPED?"stopped":"init";
    const uint32_t address=ipError==ESP_OK?ip.ip.addr:0;
    const uint32_t now=millis();
    // Worker-side observation only: never restart DHCP/radio or log credentials.
    appLog().printf("[WIFI_WAIT t=%lu] final=%d elapsed_ms=%lu status=%d assoc=%d rssi=%d dhcp=%s dhcp_err=%ld ip_err=%ld ip=%u.%u.%u.%u\n",
        (unsigned long)now,final,(unsigned long)uint32_t(now-start),int(WiFi.status()),
        associated,associated?int(ap.rssi):0,state,(long)dhcpError,(long)ipError,
        unsigned(address&255),unsigned((address>>8)&255),unsigned((address>>16)&255),unsigned(address>>24));
}
#endif

bool connect(const char *ssid, const char *password) {
    if (!ssid[0]) return false;
#if PIUU_NETWORK_DIAGNOSTICS
    const uint32_t connectStarted=millis();
    appLog().printf("[WIFI_CONNECT t=%lu] phase=begin\n",(unsigned long)connectStarted);
#endif
    WifiDiagnostics::record(WifiDiagnostics::Event::Attempt, WiFi.status());
    if (!WiFi.mode(WIFI_STA)) {
        WifiDiagnostics::record(WifiDiagnostics::Event::SetupFailed, 1);
        return false;
    }
    // Disable power saving before association/DHCP, including the first attempt.
    if (!WiFi.setSleep(false))
        WifiDiagnostics::record(WifiDiagnostics::Event::SetupFailed, 2);
    if (!stopAttempt()) return false;
    const auto beginStatus = WiFi.begin(ssid, password);
    WifiDiagnostics::record(WifiDiagnostics::Event::BeginStatus, beginStatus);
    const uint32_t start = millis();
#if PIUU_WIFI_DIAGNOSTICS
    uint32_t lastWait=start;
#endif
    while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - start) < ConnectTimeoutMs) {
#if PIUU_WIFI_DIAGNOSTICS
        const uint32_t now=millis();
        if(uint32_t(now-lastWait)>=5000) { logConnectWait(start,false);lastWait=now; }
#endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
#if PIUU_WIFI_DIAGNOSTICS
    logConnectWait(start,true);
#endif
#if PIUU_NETWORK_DIAGNOSTICS
    appLog().printf("[WIFI_CONNECT t=%lu] phase=end connected=%d status=%d elapsed_ms=%lu\n",
        (unsigned long)millis(),WiFi.status()==WL_CONNECTED,int(WiFi.status()),
        (unsigned long)uint32_t(millis()-connectStarted));
#endif
    const bool connected = WiFi.status() == WL_CONNECTED;
    WifiDiagnostics::record(connected ? WifiDiagnostics::Event::Ready : WifiDiagnostics::Event::Timeout,
                            WiFi.status(), uint32_t(millis() - start));
    // A finished UI job must not leave its connection attempt running behind
    // the next scan or explicit connect. Auto-reconnect resumes after the job.
    if (!connected) stopAttempt();
    return connected;
}
WifiResult runJob(const WifiRequest &request) {
    ReconnectPause pause;
    WifiResult result{};
    result.operation = request.operation;
    if (request.operation == WifiOperation::Scan) {
        WifiDiagnostics::record(WifiDiagnostics::Event::ScanStart, WiFi.status());
        // Preserve an associated link; stop any offline background attempt
        // so Arduino's automatic reconnect cannot compete with the scan.
        const bool modeReady = WiFi.mode(WIFI_STA);
        if (!modeReady) WifiDiagnostics::record(WifiDiagnostics::Event::SetupFailed, 1);
        const bool prepared = modeReady && (WiFi.status() == WL_CONNECTED || stopAttempt());
        const int count = prepared ? WiFi.scanNetworks(false, true) : -2;
        WifiDiagnostics::record(WifiDiagnostics::Event::ScanEnd, count);
        result.success = count >= 0;
        result.scan.count = count > MAX_WIFI_NETWORKS ? MAX_WIFI_NETWORKS : (count > 0 ? count : 0);
        for (int i = 0; i < result.scan.count; ++i) {
            const String name = WiFi.SSID(i);
            name.toCharArray(result.scan.entries[i].ssid, sizeof(result.scan.entries[i].ssid));
            result.scan.entries[i].rssi = WiFi.RSSI(i);
        }
        WiFi.scanDelete();
    } else {
        result.success = connect(request.ssid, request.password);
        memcpy(result.ssid, request.ssid, sizeof(result.ssid));
        memcpy(result.password, request.password, sizeof(result.password));
    }
    result.connected = WiFi.status() == WL_CONNECTED;
    return result;
}
void run(void *) {
    for (;;) {
        WifiRequest request{};
        if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
#if PIUU_EARS_LOG
        appLog().printf("[WIFI_JOB t=%lu] phase=start operation=%u status=%d internal=%u largest=%u stack=%u\n",
            (unsigned long)millis(),unsigned(request.operation),int(WiFi.status()),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(uxTaskGetStackHighWaterMark(nullptr)));
#endif
        const WifiResult result = runJob(request); // Reconnect policy restored before publication.
#if PIUU_EARS_LOG
        appLog().printf("[WIFI_JOB t=%lu] phase=end operation=%u success=%d status=%d internal=%u largest=%u stack=%u\n",
            (unsigned long)millis(),unsigned(request.operation),result.success,int(WiFi.status()),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(uxTaskGetStackHighWaterMark(nullptr)));
#endif
        xQueueSend(results, &result, portMAX_DELAY);
    }
}
}

bool wifiServiceStart() {
    if (worker) return true;
    WifiDiagnostics::begin();
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
