#include "config/settings.h"
#include "config/app_config.h"
#include "storage/storage_prefs.h"

namespace { DeviceSettings active; }
const DeviceSettings &settings() { return active; }
bool settingsLoad() { return storagePrefsLoad(active); }
bool settingsSetVolume(unsigned percent) {
    if (percent > 100) return false;
    if (percent == active.volumePercent) return true;
    if (!storagePrefsSaveVolume(percent)) return false;
    active.volumePercent = uint8_t(percent);
    return true;
}
bool settingsRememberWifi(const WifiCredentials &credentials) {
    if (!storagePrefsSaveWifiCredential(credentials.ssid, credentials.password)) return false;
    DeviceSettings next = active;
    next.wifi = credentials;
    if (!storagePrefsSave(next)) return false;
    active = next;
    return true;
}
bool settingsWifiPassword(const String &ssid, String &password) {
    return storagePrefsLoadWifiPasswordForSsid(ssid, password);
}
bool settingsSelectServer(const String &address) {
    if (!address.length() || address.length() > 64) return false;
    DeviceSettings next = active;
    next.server = address;
    if (!storagePrefsSave(next)) return false;
    active = next;
    return true;
}
bool settingsAddServer(const String &address) {
    if (!address.length() || address.length() > 64) return false;
    for (int i = 0; i < active.serverCount; ++i) if (active.servers[i] == address) return true;
    if (active.serverCount == MAX_SERVER_ENTRIES) return false;
    DeviceSettings next = active;
    next.servers[next.serverCount++] = address;
    if (!storagePrefsSaveServers(next)) return false;
    active = next;
    return true;
}
bool settingsRemoveServer(const String &address) {
    DeviceSettings next = active;
    int found = -1;
    for (int i = 0; i < next.serverCount; ++i) if (next.servers[i] == address) found = i;
    if (found < 0) return false;
    for (int i = found; i + 1 < next.serverCount; ++i) next.servers[i] = next.servers[i + 1];
    next.servers[--next.serverCount] = "";
    if (!next.serverCount) { next.serverCount = 1; next.servers[0] = SERVER_IP; }
    if (next.server == address) next.server = next.servers[0];
    if (!storagePrefsSaveServers(next) || !storagePrefsSave(next)) return false;
    active = next;
    return true;
}

bool settingsReplaceServer(const String &oldAddress, const String &address) {
    if (!address.length() || address.length()>64) return false;
    DeviceSettings next=active;
    int found=-1;
    for (int i=0;i<next.serverCount;++i) {
        if (next.servers[i]==oldAddress) found=i;
        else if (next.servers[i]==address) return false;
    }
    if (found<0) return false;
    next.servers[found]=address;
    if (next.server==oldAddress) next.server=address;
    if (!storagePrefsSaveServers(next) || !storagePrefsSave(next)) return false;
    active=next;
    return true;
}

bool settingsForgetWifiCredential(const String &ssid) {
    return storagePrefsRemoveWifiCredential(ssid);
}
bool settingsClearWifi() {
    if (!storagePrefsClearWifi()) return false;
    active.wifi = WifiCredentials{};
    return true;
}
