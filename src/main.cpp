#include "hardware/app_log.h"
#include "diagnostics/performance.h"
#include "diagnostics/uart.h"
#include <Arduino.h>
#include "hardware/display_hal.h"
#include "eyes/runtime.h"
#include "hardware/touch_hal.h"
#include "app/app_services.h"
#include "app/conversation.h"
#include "app/conversation_transport.h"
#include "network/server_link.h"
#include "audio/audio_downlink.h"
#include "audio/volume_control.h"
#include "app/ears_session.h"
#include "input/touch_input.h"
#if PIUU_DIAGNOSTICS
#include "input/ears_link.h"
#endif
#include "config/settings.h"
#include "config/ears_config.h"
#include <freertos/task.h>
#include "ui/ui_controller.h"
#include "ui/ui_model.h"
#include "ui/ui_status.h"
#include "ui/app_navigation.h"
#include "network/wifi_service.h"

void setup() {
    appLogBegin();
    delay(800);
    appLog().printf("[BOOT] udp=v3 ears_rx=%d ears_tx=%d baud=%lu diag=%d perf_diag=%d uart_diag=%d net_diag=%d fifo_threshold=%u\n",
        EarsConfig::RX_PIN,EarsConfig::TX_PIN,(unsigned long)EarsConfig::BAUD,PIUU_DIAGNOSTICS,
        PIUU_PERFORMANCE_DIAGNOSTICS,PIUU_UART_DIAGNOSTICS,PIUU_NETWORK_DIAGNOSTICS,
        unsigned(EarsConfig::RX_FIFO_FULL_THRESHOLD));
    initDisplayHardware();
    initTouchHardware();
    if (!settingsLoad()) appLog().println("[CONFIG] NVS unavailable; using defaults");
    audioVolumeSetPercent(settings().volumePercent);
#if PIUU_VOLUME_DIAGNOSTICS
    appLog().printf("[VOLUME] loaded percent=%u gain_q16=%ld\n",
        audioVolumePercent(), (long)audioVolumeGainQ16());
#endif
    conversationSetTransportPort({serverLinkOpenConversation,serverLinkCloseConversation,serverLinkTakeFailure,audioDownlinkPlayedMs,serverLinkInterrupt,serverLinkPlaybackFinished,serverLinkLogPlaybackFault});
    appServicesInit();
    eyesInit();
    uiControllerInit();
    if (!conversationInit() || !earsSessionInit() || !startTouchTask() || !wifiServiceStart()) haltWithUiError();
    wifiServiceRequestConnect(settings().wifi, true);
}
void loop() {
    appServicesPoll();
    uiStatusHandleEarsNotice(earsSessionPoll());
    uiControllerPoll();
    conversationPoll();
    static uint32_t lastConnectivity=0;
    if (uint32_t(millis()-lastConnectivity) >= 1000) {
        lastConnectivity=millis();
        appServicesRefreshConnectivity();
        uiStatusRefreshConnectivity();
    }
#if PIUU_DIAGNOSTICS
    static uint32_t lastDiagnostics=0;
    static uint8_t diagnosticsPhase=0;
    // Stagger groups: each repeats every five seconds, avoiding a UART burst.
    if(uint32_t(millis()-lastDiagnostics)>=1000) {
        lastDiagnostics=millis();
        switch(diagnosticsPhase) {
        case 0: {
        const auto link=earsLinkStats();const auto session=earsSessionStats();
        const auto queues=earsLinkQueueStats();
        appLog().printf("[EARS t=%lu] peer=%d streaming=%d rx=%lu drop=%lu evt_drop=%lu bad=%lu cmd=%lu uplink=%lu gaps=%lu dup=%lu queue=%lu peak=%lu poll_gap_max_ms=%lu\n",
            (unsigned long)millis(),earsLinkPeerOnline(),link.remoteStreaming,
            (unsigned long)link.audioReceived,(unsigned long)link.audioDropped,
            (unsigned long)link.eventDropped,(unsigned long)link.parseRejected,
            (unsigned long)link.commandsSent,(unsigned long)session.forwarded,
            (unsigned long)session.sequenceGaps,(unsigned long)session.duplicates,
            (unsigned long)queues.audio,(unsigned long)queues.audioPeak,(unsigned long)session.pollGapMaxMs);
        break;
        }
        case 1: serverLinkLogDiagnostics();break;
        case 2: audioDownlinkLogDiagnostics();break;
        case 3: eyesLogDiagnostics();break;
        case 4:
        appLog().printf("[RESOURCE t=%lu] heap=%lu min_heap=%lu max_block=%lu loop_hwm_bytes=%lu\n",
            (unsigned long)millis(),(unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getMinFreeHeap(),
            (unsigned long)ESP.getMaxAllocHeap(),
#if PIUU_PERFORMANCE_DIAGNOSTICS
            (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
#else
            0UL); // Stack scans are disabled with performance diagnostics.
#endif
        break;
        }
        diagnosticsPhase=(diagnosticsPhase+1)%5;
    }
#endif
    Diagnostics::performancePoll();
    Diagnostics::uartPoll();
    audioDownlinkPollDiagnostics();
    uiStatusRefreshStartScreen();
    // Drain again after UI and diagnostics, before deliberately yielding. The
    // conversation and UART consumer remain owned by this loop task.
    uiStatusHandleEarsNotice(earsSessionPoll());
    eyesPoll(); // At most four TFT rows; service UART/audio again next loop.
    delay(1);
}
