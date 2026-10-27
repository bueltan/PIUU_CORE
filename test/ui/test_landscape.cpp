#include <cassert>
#include <cstdio>
#include "hardware/display_geometry.h"
#include "hardware/display_hal.h"
#include "config/settings.h"
#include "ui/ui_model.h"
#include "ui/screens/ui_start.h"
#include "ui/screens/ui_setup.h"
#include "ui/screens/ui_wifi.h"
#include "ui/screens/ui_server.h"
#include "ui/screens/ui_wifi_password.h"
#include "ui/screens/ui_server_edit.h"
#include "ui/widgets/ui_keyboard.h"
static TFT_eSPI tft;
static DeviceSettings config;
TFT_eSPI &display() {return tft;}
void screenLock() {}
void screenUnlock() {}
const DeviceSettings &settings() {return config;}
bool appServicesWifiReady() {return true;}
bool appServicesServerReady() {return true;}
int main() {
    // Rotation 3: portrait top-left -> landscape top-right, bottom-right -> bottom-left.
    uint16_t x,y;
    DisplayGeometry::fromPortrait(0,0,x,y);assert(x==319 && y==0);
    DisplayGeometry::fromPortrait(239,319,x,y);assert(x==0 && y==239);
    for(unsigned py=0;py<320;++py) for(unsigned px=0;px<240;++px) {
        DisplayGeometry::fromPortrait(px,py,x,y);
        assert(x<320 && y<240 && unsigned(319-x)==py && y==px);
    }
    config.wifi.ssid="a-long-network-name";
    uiStartInit();uiStartDrawBase();
    for(int s=UI_READY;s<=UI_ERROR;++s)uiStartApplyState(static_cast<UiState>(s));
    assert(uiStartHandleTouch(230,32,TouchPhase::Press)==UI_ACTION_NONE);
    assert(uiStartHandleTouch(230,32,TouchPhase::Cancel)==UI_ACTION_NONE);
    assert(uiStartHandleTouch(230,32,TouchPhase::Release)==UI_ACTION_EYES);
    assert(uiStartHandleTouch(292,32,TouchPhase::Release)==UI_ACTION_SETUP);
    assert(uiStartHandleTouch(160,194,TouchPhase::Release)==UI_ACTION_MIC);
    uiSetupDrawBase();
    assert(uiSetupHandleTouch(235,155,TouchPhase::Release)==UI_SETUP_ACTION_BACK);
    uiModel().wifi.count=7;
    config.serverCount=7;
    for(int i=0;i<7;++i) {std::strcpy(uiModel().wifi.entries[i].ssid,"a-network-name");config.servers[i]="192.168.100.123";}
    uiWifiDrawBase();uiServerDrawBase();
    int index=-1;
    assert(uiWifiHandleTouch(20,181,index,TouchPhase::Release)==UI_WIFI_ACTION_SELECT_NETWORK && index==6);
    assert(uiServerHandleTouch(20,181,index,TouchPhase::Release)==UI_SERVER_ACTION_SELECT && index==6);
    uiModel().wifiDraft.ssid=String(32,'s');
    uiModel().wifiDraft.password=String(64,'p');
    uiWifiPasswordInit();uiWifiPasswordDrawBase();
    uiModel().wifiConnecting=true;uiWifiPasswordDrawBase();
    uiModel().serverDraft=String(64,'1');
    uiServerEditInit();uiServerEditDrawBase();
    String input;
    UiKeyboardState state{};state.text=&input;state.maxLength=64;
    uiKeyboardInit(&state);uiKeyboardDraw();
    assert(uiKeyboardHandleTouch(20,90,TouchPhase::Press)==UI_KEYBOARD_ACTION_NONE);
    assert(input.empty());
    uiKeyboardHandleTouch(20,90,TouchPhase::Release);assert(input=="1");
    uiKeyboardHandleTouch(169,216,TouchPhase::Release);assert(input.empty());
    uiKeyboardHandleTouch(224,216,TouchPhase::Release);assert(state.symbolMode);
    uiKeyboardDraw();
    assert(uiKeyboardHandleTouch(284,216,TouchPhase::Release)==UI_KEYBOARD_ACTION_OK);
    puts("Landscape UI bounds, touch mapping and controls passed");
}
