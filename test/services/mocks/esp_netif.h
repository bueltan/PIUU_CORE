#pragma once
#include "WiFi.h"
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=0x103;
enum esp_netif_dhcp_status_t { ESP_NETIF_DHCP_INIT,ESP_NETIF_DHCP_STARTED,ESP_NETIF_DHCP_STOPPED };
struct esp_netif_t {};
struct esp_netif_ip_info_t { struct { uint32_t addr=0; } ip; };
inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char*) { static esp_netif_t netif;return &netif; }
inline esp_err_t esp_netif_dhcpc_get_status(esp_netif_t*,esp_netif_dhcp_status_t *out) {
    ++WiFi.dhcpReads;*out=ESP_NETIF_DHCP_STARTED;return ESP_OK;
}
inline esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t *out) {
    out->ip.addr=WiFi.status()==WL_CONNECTED?0x5501a8c0u:0;return ESP_OK;
}
