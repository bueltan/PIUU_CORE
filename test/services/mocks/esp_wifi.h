#pragma once
#include "esp_netif.h"
struct wifi_ap_record_t { int rssi=-60; };
inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*) {
    return WiFi.pendingConnect || WiFi.status()==WL_CONNECTED?ESP_OK:ESP_ERR_INVALID_STATE;
}
