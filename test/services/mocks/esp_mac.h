#pragma once
#include <stdint.h>
#include <cstring>
constexpr int ESP_OK=0, ESP_MAC_WIFI_STA=0;
inline int esp_read_mac(uint8_t *out,int) {
    const uint8_t mac[6]={0x02,0x12,0x34,0x56,0x78,0x9a};
    memcpy(out,mac,sizeof(mac));return ESP_OK;
}
