#pragma once
#include "config/settings.h"
#include "network/wifi_types.h"

enum class WifiOperation : uint8_t { InitialConnect, Connect, Scan };
struct WifiResult {
    WifiOperation operation;
    bool success;
    bool connected;
    char ssid[33];
    char password[65];
    WifiScanResults scan;
};
// Controller owns submission/busy/result consumption; one worker owns WiFi operations.
bool wifiServiceStart();
bool wifiServiceRequestConnect(const WifiCredentials &credentials, bool initial = false);
bool wifiServiceRequestScan();
bool wifiServiceTakeResult(WifiResult &result);
bool wifiServiceBusy();
bool wifiServiceConnected();
