#include "hardware/app_log.h"
#include "ui/ui_controller.h"
#include "ui/touch_gesture.h"
#include "ui/eyes_hold.h"
#include "ui/ui_model.h"
#include "config/settings.h"
#include "app/app_services.h"
#include "app/conversation.h"
#include "ui/ui_status.h"
#include "network/server_link.h"
#include "network/wifi_service.h"
#include "input/touch_input.h"
#include "ui/app_navigation.h"
#include "ui/screens/ui_start.h"
#include "ui/screens/ui_setup.h"
#include "ui/screens/ui_volume.h"
#include "audio/volume_control.h"
#include "ui/screens/ui_wifi.h"
#include "ui/screens/ui_wifi_password.h"
#include "ui/screens/ui_server.h"
#include "ui/screens/ui_server_edit.h"

namespace {
String editingServer;
uint32_t connectRevision = 0;
void saveError() {
    uiStatusSetError("Unable to save", "Check value / storage");
    uiModel().settingsNotice="Unable to save";
    if (uiModel().screen==SCREEN_SERVER_EDIT) uiServerEditDrawBase();
    else if (uiModel().screen==SCREEN_SERVER) uiServerDrawBase();
}
void pollWifi() {
    WifiResult result{};
    if (!wifiServiceTakeResult(result)) return;
    if (result.operation == WifiOperation::Scan) {
        uiModel().wifiScanning = false;
        uiModel().wifi = result.scan;
        uiModel().selectedWifi = -1;
        if (uiModel().screen == SCREEN_WIFI) showWifiScreen();
        if (!result.success) uiStatusSetError("Scan failed", "Try again");
    } else {
        uiModel().wifiConnecting = false;
        if (result.operation == WifiOperation::Connect) {
            if (result.success) {
                if (!settingsRememberWifi({String(result.ssid), String(result.password)})) saveError();
            } else uiStatusSetError("WiFi failed", "Connection not established");
            if (uiModel().screen == SCREEN_WIFI_PASSWORD && uiModel().screenRevision == connectRevision)
                showStartScreen();
        }
    }
    appServicesRefreshConnectivity();
    uiStatusInvalidate();
}
void dispatch(uint16_t x, uint16_t y, TouchPhase phase) {
    int index = -1;
    switch (uiModel().screen) {
    case SCREEN_EYES: break;
    case SCREEN_START:
        switch (uiStartHandleTouch(x,y,phase)) {
        case UI_ACTION_MIC: uiStatusHandleConversationToggle(); break;
        case UI_ACTION_SETUP: showSetupScreen(); break;
        case UI_ACTION_EYES: showEyesScreen(); break;
        default: break;
        } break;
    case SCREEN_SETUP:
        switch (uiSetupHandleTouch(x,y,phase)) {
        case UI_SETUP_ACTION_VOLUME: showVolumeScreen(); break;
        case UI_SETUP_ACTION_WIFI: showWifiScreen(); break;
        case UI_SETUP_ACTION_SERVER:
            uiModel().selectedServer = -1;
            for (int i=0;i<settings().serverCount;++i)
                if (settings().servers[i] == settings().server) uiModel().selectedServer=i;
            showServerScreen(); break;
        case UI_SETUP_ACTION_BACK: showStartScreen(); break;
        case UI_SETUP_ACTION_TOUCH: appLog().println("[UI] Touch calibration not implemented"); break;
        default: break;
        } break;
    case SCREEN_VOLUME: {
        const UiVolumeAction action=uiVolumeHandleTouch(x,y,phase);
        unsigned value=uiModel().volumePercent;
        if (action==UI_VOLUME_BACK) {
            if (settingsSetVolume(value)) showSetupScreen();
            else {
                uiModel().settingsNotice="Save failed";
                uiVolumeDrawBase();
                appLog().println("[VOLUME] NVS save failed; live volume remains temporary");
            }
            break;
        }
        if (action==UI_VOLUME_DOWN) value=value>VolumeConfig::Step ? value-VolumeConfig::Step : 0;
        else if (action==UI_VOLUME_UP) value=value+VolumeConfig::Step>100 ? 100 : value+VolumeConfig::Step;
        else if (action==UI_VOLUME_MUTE) {
            if (value) { uiModel().volumeBeforeMute=value; value=0; }
            else value=uiModel().volumeBeforeMute;
        } else break;
        if (value) uiModel().volumeBeforeMute=value;
        uiModel().volumePercent=value;
        audioVolumeSetPercent(value);
        uiModel().settingsNotice="";
        appLog().printf("[VOLUME] percent=%u gain_q16=%ld\n",value,(long)audioVolumeGainQ16());
        uiVolumeDrawBase();
        break;
    }
    case SCREEN_WIFI:
        switch (uiWifiHandleTouch(x,y,index,phase)) {
        case UI_WIFI_ACTION_BACK: showSetupScreen(); break;
        case UI_WIFI_ACTION_SCAN:
            if (wifiServiceRequestScan()) {
                uiModel().wifiScanning=true;
                uiModel().wifi.count=0;
                uiModel().selectedWifi=-1;
                showWifiScreen();
            } else uiStatusSetError("WiFi busy", "Wait for current job");
            break;
        case UI_WIFI_ACTION_SELECT_NETWORK:
            if (index >= 0 && index < uiModel().wifi.count) {
                uiModel().wifiDraft.ssid=uiModel().wifi.entries[index].ssid;
                uiModel().wifiDraft.password="";
                settingsWifiPassword(uiModel().wifiDraft.ssid,uiModel().wifiDraft.password);
                showWifiPasswordScreen();
            } break;
        default: break;
        } break;
    case SCREEN_WIFI_PASSWORD:
        switch (uiWifiPasswordHandleTouch(x,y,phase)) {
        case UI_WIFI_PASSWORD_ACTION_BACK: showWifiScreen(); break;
        case UI_WIFI_PASSWORD_ACTION_OK:
            if (wifiServiceRequestConnect(uiModel().wifiDraft)) {
                conversationAbort();
                uiModel().wifiConnecting=true;
                connectRevision=uiModel().screenRevision;
                uiWifiPasswordDrawBase();
            } else uiStatusSetError("WiFi busy/invalid", "Check SSID and wait");
            break;
        default: break;
        } break;
    case SCREEN_SERVER:
        switch (uiServerHandleTouch(x,y,index,phase)) {
        case UI_SERVER_ACTION_BACK: showSetupScreen(); break;
        case UI_SERVER_ACTION_SELECT: break; // Selection belongs to this controller task.
        case UI_SERVER_ACTION_ADD:
            editingServer=""; uiModel().serverDraft=""; showServerEditScreen(); break;
        case UI_SERVER_ACTION_EDIT:
            if (uiModel().selectedServer >= 0 && uiModel().selectedServer < settings().serverCount) {
                editingServer=settings().servers[uiModel().selectedServer];
                uiModel().serverDraft=editingServer; showServerEditScreen();
            } break;
        case UI_SERVER_ACTION_DELETE:
            if (uiModel().selectedServer >= 0 && uiModel().selectedServer < settings().serverCount) {
                const String address=settings().servers[uiModel().selectedServer];
                const bool active=address==settings().server;
                if (!settingsRemoveServer(address)) { saveError(); break; }
                if (active) { conversationAbort(); serverLinkResetHeartbeat(); }
                uiModel().selectedServer=-1; showServerScreen();
            } break;
        case UI_SERVER_ACTION_OK:
            if (uiModel().selectedServer >= 0 && uiModel().selectedServer < settings().serverCount) {
                if (!settingsSelectServer(settings().servers[uiModel().selectedServer])) { saveError(); break; }
                conversationAbort(); serverLinkResetHeartbeat();
            }
            showStartScreen(); break;
        default: break;
        } break;
    case SCREEN_SERVER_EDIT:
        switch (uiServerEditHandleTouch(x,y,phase)) {
        case UI_SERVER_EDIT_ACTION_BACK: showServerScreen(); break;
        case UI_SERVER_EDIT_ACTION_OK: {
            const bool active=editingServer.length() && editingServer==settings().server;
            const bool ok=editingServer.length() ? settingsReplaceServer(editingServer,uiModel().serverDraft) : settingsAddServer(uiModel().serverDraft);
            if (!ok) { saveError(); break; }
            if (active) { conversationAbort(); serverLinkResetHeartbeat(); }
            showServerScreen(); break;
        }
        default: break;
        } break;
    }
}
}
void uiControllerInit() {
    uiStartInit(); uiSetupInit(); uiWifiInit(); uiWifiPasswordInit(); uiServerInit(); uiServerEditInit();
    showEyesScreen();
}
void uiControllerPoll() {
    pollWifi();
    static TouchGesture gesture;
    static EyesHold hold;
    TouchEvent event{};
    if(uiModel().screen==SCREEN_EYES) {
        // Drain releases first so a short touch cannot become a delayed hold.
        for(unsigned i=0;i<16 && takeTouchEvent(event);++i)hold.edge(event,uiModel().screenEnteredMs);
        if(hold.poll(millis())) { gesture=TouchGesture{};showStartScreen(); }
        return;
    }
    UiTouch touch{};
    if (takeTouchEvent(event) && gesture.consume(event,uiModel().screenRevision,uiModel().screenEnteredMs,touch))
        dispatch(touch.x,touch.y,touch.phase);
}
