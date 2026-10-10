#include "storage/storage_prefs.h"
#include "config/app_config.h"
#include <Preferences.h>

namespace {
// Used only by the controller task. Handles are closed on every path.
Preferences prefs;
String serverKey(int i) { return "server_" + String(i); }
String ssidKey(int i) { return "ssid_" + String(i); }
String passKey(int i) { return "pass_" + String(i); }
bool put(const char *key, const String &value) {
    const size_t written = prefs.putString(key, value);
    if (value.length()) return written == value.length();
    // Empty strings return zero on success too. Verify the stored value explicitly.
    return prefs.isKey(key) && prefs.getString(key, "<missing>").length() == 0;
}
}

bool storagePrefsLoad(DeviceSettings &out) {
    out = DeviceSettings{};
    out.server = SERVER_IP;
    out.servers[0] = SERVER_IP;
    out.serverCount = 1;
    if (!prefs.begin("handwalle", true)) return false;
    out.wifi.ssid = prefs.getString("wifi_ssid", "");
    out.wifi.password = prefs.getString("wifi_pass", "");
    out.server = prefs.getString("server_ip", SERVER_IP);
    if (!out.server.length()) out.server = SERVER_IP;
    out.serverCount = 0;
    const int count = constrain(prefs.getInt("server_count", 0), 0, MAX_SERVER_ENTRIES);
    for (int i = 0; i < count; ++i) {
        String address = prefs.getString(serverKey(i).c_str(), "");
        if (!address.length()) continue;
        bool duplicate = false;
        for (int j = 0; j < out.serverCount; ++j) duplicate |= out.servers[j] == address;
        if (!duplicate) out.servers[out.serverCount++] = address;
    }
    bool hasDefault = false;
    for (int i = 0; i < out.serverCount; ++i) hasDefault |= out.servers[i] == SERVER_IP;
    if (!hasDefault && out.serverCount < MAX_SERVER_ENTRIES) out.servers[out.serverCount++] = SERVER_IP;
    prefs.end();
    return true;
}

bool storagePrefsSave(const DeviceSettings &data) {
    if (!prefs.begin("handwalle", false)) return false;
    bool ok = put("wifi_ssid", data.wifi.ssid);
    ok = put("wifi_pass", data.wifi.password) && ok;
    ok = put("server_ip", data.server) && ok;
    prefs.end();
    return ok;
}
bool storagePrefsSaveServers(const DeviceSettings &data) {
    if (data.serverCount < 1 || data.serverCount > MAX_SERVER_ENTRIES || !prefs.begin("handwalle", false)) return false;
    bool ok = true;
    for (int i = 0; i < MAX_SERVER_ENTRIES; ++i) {
        const String key = serverKey(i);
        if (i < data.serverCount) ok = put(key.c_str(), data.servers[i]) && ok;
        else if (prefs.isKey(key.c_str())) ok = prefs.remove(key.c_str()) && ok;
    }
    ok = prefs.putInt("server_count", data.serverCount) == sizeof(int32_t) && ok;
    prefs.end();
    return ok;
}
bool storagePrefsSaveWifiCredential(const String &ssid, const String &password) {
    if (!ssid.length() || ssid.length() > 32 || password.length() > 64 || !prefs.begin("handwalle", false)) return false;
    // Preserve legacy saved networks, including databases larger than eight entries.
    int count = prefs.getInt("wifi_count", 0);
    if (count < 0) count = 0;
    int index = count;
    for (int i = 0; i < count; ++i) {
        if (prefs.getString(ssidKey(i).c_str(), "") == ssid) { index = i; break; }
    }
    bool ok = put(ssidKey(index).c_str(), ssid);
    ok = put(passKey(index).c_str(), password) && ok;
    if (index == count && ok) ok = prefs.putInt("wifi_count", count + 1) == sizeof(int32_t);
    prefs.end();
    return ok;
}
bool storagePrefsLoadWifiPasswordForSsid(const String &ssid, String &password) {
    password = "";
    if (!prefs.begin("handwalle", true)) return false;
    const int count = prefs.getInt("wifi_count", 0);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        if (prefs.getString(ssidKey(i).c_str(), "") != ssid) continue;
        password = prefs.getString(passKey(i).c_str(), "");
        found = true;
        break;
    }
    prefs.end();
    return found;
}

bool storagePrefsRemoveWifiCredential(const String &ssid) {
    if (!ssid.length() || !prefs.begin("handwalle", false)) return false;
    const int count = prefs.getInt("wifi_count", 0);
    int found = -1;
    for (int i=0;i<count;++i) {
        if (prefs.getString(ssidKey(i).c_str(), "") == ssid) { found=i; break; }
    }
    if (found<0) { prefs.end(); return false; }
    bool ok=true;
    for (int i=found;i+1<count;++i) {
        const String name=prefs.getString(ssidKey(i+1).c_str(), "");
        const String password=prefs.getString(passKey(i+1).c_str(), "");
        ok=put(ssidKey(i).c_str(),name) && ok;
        ok=put(passKey(i).c_str(),password) && ok;
    }
    const String lastSsid=ssidKey(count-1),lastPass=passKey(count-1);
    if (prefs.isKey(lastSsid.c_str())) ok=prefs.remove(lastSsid.c_str()) && ok;
    if (prefs.isKey(lastPass.c_str())) ok=prefs.remove(lastPass.c_str()) && ok;
    if (ok) ok=prefs.putInt("wifi_count",count-1)==sizeof(int32_t);
    prefs.end();
    return ok;
}
bool storagePrefsClearWifi() {
    if (!prefs.begin("handwalle",false)) return false;
    const int count=prefs.getInt("wifi_count",0);
    bool ok=true;
    for (int i=0;i<count;++i) {
        const String name=ssidKey(i),password=passKey(i);
        if (prefs.isKey(name.c_str())) ok=prefs.remove(name.c_str()) && ok;
        if (prefs.isKey(password.c_str())) ok=prefs.remove(password.c_str()) && ok;
    }
    ok=put("wifi_ssid","") && ok;
    ok=put("wifi_pass","") && ok;
    if (ok) ok=prefs.putInt("wifi_count",0)==sizeof(int32_t);
    prefs.end();
    return ok;
}
