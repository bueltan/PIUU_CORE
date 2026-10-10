#pragma once
#include <Arduino.h>
#include "config/settings.h"
#include "network/wifi_types.h"
enum AppScreen { SCREEN_START, SCREEN_SETUP, SCREEN_WIFI, SCREEN_WIFI_PASSWORD, SCREEN_SERVER, SCREEN_SERVER_EDIT };
struct UiModel {
    AppScreen screen = SCREEN_START;
    uint32_t screenRevision = 0;
    uint32_t screenEnteredMs = 0;
    WifiScanResults wifi;
    bool wifiScanning = false;
    bool wifiConnecting = false;
    int selectedWifi = -1;
    int selectedServer = -1;
    WifiCredentials wifiDraft;
    String serverDraft;
    String settingsNotice;
    String errorLine1;
    String errorLine2;
};
// Mutable only from the setup/loop task; never read by workers.
UiModel &uiModel();
