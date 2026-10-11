#include "hardware/app_log.h"
#include "app/app_services.h"
#include "network/wifi_service.h"
#include "network/server_link.h"
#include "audio/audio_uplink.h"
#include "audio/audio_downlink.h"
#include "app/conversation.h"

static bool started = false;
static bool wifiReady = false;
static bool serverReady = false;
static bool wasConnected = false;

void appServicesInit() {
    wasConnected = false;
    wifiReady = false;
    serverReady = false;
    serverLinkResetHeartbeat();
}
bool appServicesReady() { return started; }

bool appServicesStartIfNeeded() {
    if (started) return true;
    if (!wifiReady) return false;
    audioDownlinkSetEventSink(conversationPostPlaybackEvent);
    if (!serverLinkInit() || !audioDownlinkInit() || !audioDownlinkStartTasks()) {
        appLog().println("[SERVICES] Startup failed; will retry on connectivity refresh");
        return false;
    }
    audioUplinkSetSender(serverLinkSend);
    started = true;
    serverLinkResetHeartbeat();
    appLog().println("[SERVICES] UDP/audio ready");
    return true;
}

void appServicesRefreshConnectivity() {
    const bool connected = wifiServiceConnected() && !wifiServiceBusy();
    wifiReady = connected;
    if (connected != wasConnected) {
        appLog().printf("[WIFI] Connection %s\n", connected ? "detected" : "lost");
        serverLinkResetHeartbeat();
        serverReady = false;
    }
    wasConnected = connected;
    if (!connected) serverReady = false;
    else appServicesStartIfNeeded();
}

void appServicesPoll() {
    const bool connected = wifiServiceConnected() && !wifiServiceBusy();
    if (connected != wasConnected) appServicesRefreshConnectivity();
    wifiReady = connected;
    const bool online = wifiReady && started;
    serverReady = serverLinkPollHeartbeat(online);
}

bool appServicesWifiReady() { return wifiReady; }
bool appServicesServerReady() { return serverReady; }
bool appServicesServerReachable() { return wifiReady && started && serverLinkReachable(); }
