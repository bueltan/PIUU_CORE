#include "config/settings.h"
#include "config/app_config.h"
#include "Preferences.h"
#include <cassert>
#include <cstdio>
FakeNvs fakeNvs;
int main() {
    fakeNvs.strings={{"wifi_ssid","legacy"},{"wifi_pass","secret"},{"server_ip","10.0.0.20"},{"server_0","10.0.0.20"},{"server_1","10.0.0.20"},{"server_2","10.0.0.21"}};
    fakeNvs.integers={{"server_count",3},{"wifi_count",12}};
    for(int i=0;i<12;++i) {
        fakeNvs.strings["ssid_"+std::to_string(i)]=String("net-")+std::to_string(i);
        fakeNvs.strings["pass_"+std::to_string(i)]=String("pass-")+std::to_string(i);
    }
    assert(settingsLoad());assert(fakeNvs.opens==0);
    assert(settings().wifi.ssid=="legacy" && settings().wifi.password=="secret" && settings().server=="10.0.0.20");
    assert(settings().serverCount==3); // duplicates removed, compiled default retained
    assert(settings().volumePercent==VolumeConfig::DefaultPercent);
    assert(settingsSetVolume(0));assert(settingsLoad() && settings().volumePercent==0);
    assert(settingsSetVolume(100));assert(settingsLoad() && settings().volumePercent==100);
    assert(!settingsSetVolume(101) && settings().volumePercent==100);
    fakeNvs.failWrites=true;
    assert(!settingsSetVolume(35) && settings().volumePercent==100 && fakeNvs.opens==0);
    fakeNvs.failWrites=false;
    assert(settingsSetVolume(35));
    fakeNvs.integers["volume_pct"]=-1;
    assert(settingsLoad() && settings().volumePercent==VolumeConfig::DefaultPercent);
    fakeNvs.integers["volume_pct"]=101;
    assert(settingsLoad() && settings().volumePercent==VolumeConfig::DefaultPercent);
    assert(settingsSetVolume(35));
    String password;
    assert(settingsWifiPassword("net-11",password) && password=="pass-11");
    WifiCredentials draft=settings().wifi;draft.password="edited";
    assert(settings().wifi.password=="secret");
    assert(settingsRememberWifi({"open-network",""}));
    assert(settingsWifiPassword("open-network",password) && password.empty());
    assert(fakeNvs.integers["wifi_count"]==13);
    assert(settingsRememberWifi({"net-11","replacement"}));
    assert(fakeNvs.integers["wifi_count"]==13);
    assert(settingsLoad() && settings().wifi.ssid=="net-11");
    assert(settingsWifiPassword("net-11",password) && password=="replacement");
    assert(settingsAddServer("10.0.0.22"));assert(settings().server=="10.0.0.20");
    assert(settingsSelectServer("10.0.0.22"));
    assert(settingsReplaceServer("10.0.0.22","10.0.0.23"));assert(settings().server=="10.0.0.23");
    assert(!settingsReplaceServer("10.0.0.23","10.0.0.20"));
    assert(settingsRemoveServer("10.0.0.23"));assert(settings().server=="10.0.0.20");
    fakeNvs.failWrites=true;
    assert(!settingsSelectServer("failed"));assert(settings().server=="10.0.0.20");
    assert(!settingsRememberWifi({"failed",""}));assert(settings().wifi.ssid=="net-11");
    assert(!settingsAddServer("failed"));assert(fakeNvs.opens==0);
    fakeNvs.failWrites=false;
    while(settings().serverCount<MAX_SERVER_ENTRIES) assert(settingsAddServer(String("extra-")+std::to_string(settings().serverCount)));
    assert(!settingsAddServer("overflow"));
    assert(settingsLoad() && settings().serverCount==MAX_SERVER_ENTRIES);
    while(settings().serverCount>1) assert(settingsRemoveServer(settings().servers[settings().serverCount-1]));
    assert(settingsRemoveServer(settings().servers[0]));assert(settings().serverCount==1 && settings().server==SERVER_IP);
    assert(settingsForgetWifiCredential("net-5"));assert(fakeNvs.integers["wifi_count"]==12);
    assert(!settingsWifiPassword("net-5",password));
    assert(settingsWifiPassword("net-11",password) && password=="replacement");
    assert(!settingsForgetWifiCredential("missing"));
    assert(settingsForgetWifiCredential("net-11"));assert(settings().wifi.ssid=="net-11");
    fakeNvs.failWrites=true;assert(!settingsClearWifi());assert(settings().wifi.ssid=="net-11");
    fakeNvs.failWrites=false;
    const String savedServer=settings().server;
    assert(settingsClearWifi());assert(settings().wifi.ssid.empty() && settings().wifi.password.empty());
    assert(fakeNvs.integers["wifi_count"]==0 && settings().server==savedServer);
    assert(settingsLoad() && settings().wifi.ssid.empty() && settings().server==savedServer);
    assert(settings().volumePercent==35); // WiFi/server edits preserve the independent volume key.
    fakeNvs.available=false;
    assert(!settingsLoad());assert(settings().server==SERVER_IP && settings().wifi.ssid.empty());
    assert(fakeNvs.opens==0);
    puts("PASS: legacy NVS keys, 12 saved WiFi networks, empty passwords, drafts, server edit/delete/full, persistence failures");
}
