#pragma once
#include "config/settings.h"
// Persistence only; explicit data arguments, no UI/runtime globals.
// NVS namespace/keys remain compatible with the legacy firmware.
bool storagePrefsLoad(DeviceSettings &out);
bool storagePrefsSave(const DeviceSettings &data);
bool storagePrefsSaveServers(const DeviceSettings &data);
bool storagePrefsSaveWifiCredential(const String &ssid, const String &password);
bool storagePrefsLoadWifiPasswordForSsid(const String &ssid, String &password);

bool storagePrefsRemoveWifiCredential(const String &ssid);
bool storagePrefsClearWifi();

bool storagePrefsSaveVolume(unsigned percent);
