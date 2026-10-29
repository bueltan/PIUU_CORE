#pragma once
#include <Arduino.h>
#include "config/volume_config.h"

constexpr int MAX_SERVER_ENTRIES = 8;
struct WifiCredentials { String ssid; String password; };
struct DeviceSettings {
    uint8_t volumePercent = VolumeConfig::DefaultPercent;
    WifiCredentials wifi;
    String server;
    int serverCount = 0;
    String servers[MAX_SERVER_ENTRIES];
};
// setup()/loop() own settings. Workers receive bounded value copies, never references.
const DeviceSettings &settings();
bool settingsLoad();
bool settingsSetVolume(unsigned percent);
bool settingsRememberWifi(const WifiCredentials &credentials);
bool settingsWifiPassword(const String &ssid, String &password);
bool settingsSelectServer(const String &address);
bool settingsAddServer(const String &address);
bool settingsRemoveServer(const String &address);

bool settingsReplaceServer(const String &oldAddress, const String &address);
// Forgetting one saved SSID does not disconnect or change the active credentials.
bool settingsForgetWifiCredential(const String &ssid);
// Clears active/saved credentials; lifecycle decides whether to disconnect hardware.
bool settingsClearWifi();
