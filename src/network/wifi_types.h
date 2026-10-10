#pragma once
constexpr int MAX_WIFI_NETWORKS = 10;
struct WifiEntry { char ssid[33] = {}; int rssi = 0; };
struct WifiScanResults { int count = 0; WifiEntry entries[MAX_WIFI_NETWORKS]; };
