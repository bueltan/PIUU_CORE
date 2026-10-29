#include "app/conversation.h"
#include "app/conversation_event.h"
#include "app/conversation_clock.h"
#include "config/app_config.h"
#include "hardware/app_log.h"
#include "app/conversation_input.h"
#include "app/conversation_transport.h"
#include "app/app_services.h"
#include "audio/audio_downlink.h"
#include "audio/audio_uplink.h"
#include "audio/silence_monitor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace { ConversationClock activityClock; ConversationCloseInfo lastClose; bool notificationPending=false, notificationStandby=false; uint32_t notificationAt=0; uint32_t providerActivityAt=0; SilenceMonitor silence; bool speaking=false; bool inputStandby=false; uint32_t standbyAt=0; bool inputMuted=false, resumePending=false; uint32_t resumeAt=0; ConversationMachine machine; QueueHandle_t events = nullptr; ConversationInputPort input{}; ConversationTransportPort transport{}; }
namespace {
uint32_t activityNow() { return activityClock.update(millis(),appServicesServerResponsive()); }
const char *stateName(ConversationState state) {
    switch(state) {
        case ConversationState::Idle: return "Idle";
        case ConversationState::Connecting: return "Connecting";
        case ConversationState::Listening: return "Listening";
        case ConversationState::WaitingResponse: return "WaitingResponse";
        case ConversationState::Playing: return "Playing";
    }
    return "Unknown";
}
void transition(ConversationState before,const char *cause) {
#if PIUU_DIAGNOSTICS
    if(before!=machine.state()) appLog().printf("[CONV t=%lu] %s -> %s epoch=%lu cause=%s\n",
        (unsigned long)millis(),stateName(before),stateName(machine.state()),(unsigned long)machine.epoch(),cause);
#endif
}
}
void conversationSetTransportPort(ConversationTransportPort port) { transport=port; }
void conversationSetInputPort(ConversationInputPort port) { input=port; }
bool conversationInit() {
    if (!events) events = xQueueCreate(8, sizeof(PlaybackEvent));
    return events != nullptr;
}
ConversationState conversationState() { return machine.state(); }
bool conversationInputMuted() { return inputMuted || inputStandby; }
bool conversationInputStandby() { return inputStandby; }
bool conversationKeywordsMuted() {
    return machine.state()==ConversationState::Playing || resumePending;
}

bool conversationPostPlaybackEvent(PlaybackEvent event) {
    return events && xQueueSend(events, &event, 0) == pdTRUE;
}
bool conversationStart() {
    if (!events || !appServicesWifiReady() || !appServicesReady() || !appServicesServerReady() ||
        machine.state() != ConversationState::Idle) return false;
    if (input.start && !input.start()) return false;
    if (transport.begin && !transport.begin()) { if(input.stop) input.stop();return false; }
    if (!audioUplinkBeginConversation()) { if(transport.cancel) transport.cancel(0);if (input.stop) input.stop();return false; }
    const auto before=machine.state();machine.begin();transition(before,"start");
    activityClock.reset(millis());audioDownlinkBeginTurn(machine.epoch());silence.reset(activityNow());providerActivityAt=activityNow();speaking=false;inputStandby=false;inputMuted=false;resumePending=false;
    return true;
}
bool conversationResumeInput() {
    if(!inputStandby || notificationPending || notificationStandby || machine.state()!=ConversationState::Listening ||
       !appServicesWifiReady() || !appServicesServerResponsive()) return false;
    if(input.start && !input.start()) return false;
    inputStandby=false;inputMuted=false;resumePending=false;speaking=false;
    silence.reset(activityNow());providerActivityAt=activityNow();
    appLog().println("[MIC_GATE] resumed=user_activation session_preserved=1");
    return true;
}
bool conversationStartNotification() {
    if(!conversationStart()) return false;
    inputMuted=true;notificationPending=true;notificationAt=millis();
    appLog().println("[CONV] notification_start mic_muted=1");
    return true;
}
bool conversationAcceptStandbyNotification() {
    if(!inputStandby || notificationPending || notificationStandby || resumePending || speaking ||
       machine.state()!=ConversationState::Listening || !appServicesServerResponsive() || !appServicesWifiReady()) return false;
    notificationPending=notificationStandby=true;notificationAt=millis();
    appLog().println("[CONV] notification_standby session_preserved=1 mic_muted=1");
    return true;
}
ConversationCloseInfo conversationLastClose() { return lastClose; }
const char *conversationCloseReasonName(ConversationCloseReason reason) {
    switch(reason) {
        case ConversationCloseReason::UserStop: return "user_stop";
        case ConversationCloseReason::ProviderIdle: return "provider_idle";
        case ConversationCloseReason::SpeechSilence: return "speech_silence";
        case ConversationCloseReason::SpeechWatchdog: return "speech_watchdog";
        case ConversationCloseReason::TransportFailure: return "transport_failure";
        case ConversationCloseReason::WifiLost: return "wifi_lost";
        case ConversationCloseReason::PlaybackStalled: return "playback_stalled";
        case ConversationCloseReason::EarsOffline: return "ears_offline";
        case ConversationCloseReason::EarsRestarted: return "ears_restarted";
        case ConversationCloseReason::EarsControlLost: return "ears_control_lost";
        case ConversationCloseReason::ServerClosed: return "server_closed";
        case ConversationCloseReason::NotificationTimeout: return "notification_timeout";
        case ConversationCloseReason::ConfigurationChanged: return "configuration_changed";
        case ConversationCloseReason::StandbyTimeout: return "standby_timeout";
    }
    return "unknown";
}
const char *conversationCloseReasonLabel(ConversationCloseReason reason) {
    switch(reason) {
        case ConversationCloseReason::UserStop: return "Stopped by user";
        case ConversationCloseReason::ProviderIdle: return "No speech detected";
        case ConversationCloseReason::SpeechSilence: return "Speech became silent";
        case ConversationCloseReason::SpeechWatchdog: return "Speech end timed out";
        case ConversationCloseReason::TransportFailure: return "Server link failed";
        case ConversationCloseReason::WifiLost: return "WiFi disconnected";
        case ConversationCloseReason::PlaybackStalled: return "Audio playback stalled";
        case ConversationCloseReason::EarsOffline: return "EARS disconnected";
        case ConversationCloseReason::EarsRestarted: return "EARS stream restarted";
        case ConversationCloseReason::EarsControlLost: return "EARS control overflow";
        case ConversationCloseReason::ServerClosed: return "Closed by backend";
        case ConversationCloseReason::NotificationTimeout: return "Reminder timed out";
        case ConversationCloseReason::ConfigurationChanged: return "Connection settings changed";
        case ConversationCloseReason::StandbyTimeout: return "Standby time expired";
    }
    return "Unknown reason";
}
void conversationClose(ConversationCloseReason reason) { conversationAbort(reason); }
void conversationAbort(ConversationCloseReason reason) {
    if(machine.state()!=ConversationState::Idle) {
        ++lastClose.sequence;
        if(!lastClose.sequence) ++lastClose.sequence;
        lastClose.atMs=millis();lastClose.epoch=machine.epoch();lastClose.state=machine.state();
        lastClose.reason=reason;lastClose.providerQuietMs=uint32_t(activityNow()-providerActivityAt);
        lastClose.localQuietMs=silence.quietMs(activityNow());lastClose.speaking=speaking;lastClose.muted=conversationInputMuted();
        // One low-volume closure record, independent of verbose diagnostic flags.
        appLog().printf("[CONV_CLOSE t=%lu] reason=%s state=%s epoch=%lu provider_quiet_ms=%lu local_quiet_ms=%lu speaking=%d muted=%d\n",
            (unsigned long)lastClose.atMs,conversationCloseReasonName(reason),stateName(lastClose.state),
            (unsigned long)lastClose.epoch,(unsigned long)lastClose.providerQuietMs,
            (unsigned long)lastClose.localQuietMs,lastClose.speaking,lastClose.muted);
    }
#if PIUU_DIAGNOSTICS
    if(machine.state()!=ConversationState::Idle) audioDownlinkLogDiagnostics();
#endif
    inputStandby=false;inputMuted=false;resumePending=false;notificationPending=notificationStandby=false;
    if (input.stop) input.stop();
    if(transport.cancel) transport.cancel(transport.playedMs?transport.playedMs():0);
    audioUplinkClose();
    const auto before=machine.state();machine.abort();transition(before,"abort");
    audioDownlinkBeginTurn(machine.epoch());
}
bool conversationInterrupt() {
    if(machine.state()==ConversationState::Idle || !transport.interrupt) return false;
    const uint32_t played=transport.playedMs?transport.playedMs():0;
    if(!transport.interrupt(played)) return false;
    inputMuted=false;resumePending=false;
    const auto before=machine.state();machine.speechStarted();
    audioDownlinkBeginTurn(machine.epoch());silence.activity(activityNow());providerActivityAt=activityNow();speaking=false;
    transition(before,"manual_interrupt");return true;
}
bool conversationResponseReady() { return audioDownlinkEpochReady(); }
uint32_t conversationServerEvent(ConversationEvent event,uint32_t response,uint32_t utterance) {
    if(machine.state()==ConversationState::Idle) return 0;
    const uint32_t played=transport.playedMs?transport.playedMs():0;
    const auto before=machine.state();silence.activity(activityNow());providerActivityAt=activityNow();
    if(event==ConversationEvent::SpeechStarted) {
        inputMuted=false;resumePending=false;
        speaking=true;machine.speechStarted();audioDownlinkBeginTurn(machine.epoch());
    } else if(event==ConversationEvent::SpeechStopped) {
        speaking=false;machine.speechStopped();
    } else if(event==ConversationEvent::ResponseStarted) {
        notificationPending=false;
        inputMuted=true;resumePending=false;
        appLog().println("[MIC_GATE] muted=response_started");
        speaking=false;machine.responseStarted();audioDownlinkBeginTurn(machine.epoch());audioDownlinkExpectResponse();
    } else if(event==ConversationEvent::Ready) {
        machine.ready();silence.reset(activityNow());
    } else if(event==ConversationEvent::Closed) conversationAbort(ConversationCloseReason::ServerClosed);
    appLog().printf("[CONV_EVENT t=%lu] code=%u response=%lu utterance=%lu played_ms=%lu\n",
        (unsigned long)millis(),unsigned(event),(unsigned long)response,(unsigned long)utterance,(unsigned long)played);
    transition(before,"server_event");return played;
}
void conversationObserveAudio(const int16_t *samples,size_t count) {
    if(machine.state()==ConversationState::Listening && !conversationInputMuted())
        silence.observe(samples,count,activityNow(),CONVERSATION_ACTIVITY_LEVEL,CONVERSATION_ACTIVITY_HOLD_MS);
}
void conversationPoll() {
    static bool reportedPaused=false;
    activityNow();
    if(machine.state()!=ConversationState::Idle && reportedPaused!=activityClock.paused())
        appLog().printf("[CONV_TIMER t=%lu] silence=%s reason=server_traffic\n",
            (unsigned long)millis(),activityClock.paused()?"paused":"resumed");
    reportedPaused=activityClock.paused();
    if(notificationPending && uint32_t(millis()-notificationAt)>=20000) {
        if(notificationStandby) {
            notificationPending=notificationStandby=false;
            appLog().println("[CONV] notification_response_timeout session_preserved=1 standby=1");
        } else {
            appLog().println("[CONV] notification_response_timeout");conversationAbort(ConversationCloseReason::NotificationTimeout);
        }
    }
    if(transport.takeFailure && transport.takeFailure()) conversationAbort(ConversationCloseReason::TransportFailure);
    if (machine.state() != ConversationState::Idle && !appServicesWifiReady()) conversationAbort(ConversationCloseReason::WifiLost);
    const ConversationState beforePlayback=machine.state();
    PlaybackEvent event{};
    for (int i = 0; i < 8 && events && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
        { const auto before=machine.state();
          if(event.epoch==machine.epoch() && event.kind==PlaybackEventKind::Failed) {
              audioDownlinkLogQueueFault();
              if(transport.playbackFaultDiagnostics) transport.playbackFaultDiagnostics();
              conversationAbort(ConversationCloseReason::PlaybackStalled);
          } else machine.playback(event);
          transition(before,"playback_event"); }
    if (beforePlayback!=ConversationState::Listening && machine.state()==ConversationState::Listening) {
        if(transport.played) transport.played(transport.playedMs?transport.playedMs():0);
        silence.activity(activityNow());providerActivityAt=activityNow();
        if(inputMuted && (!inputStandby || notificationStandby)) { resumePending=true;resumeAt=millis()+CONVERSATION_ECHO_GUARD_MS; }
    }
    if(resumePending && int32_t(millis()-resumeAt)>=0) {
        if(notificationStandby) {
            notificationStandby=false;
            if(input.start && !input.start()) {
                resumePending=false;
                appLog().println("[MIC_GATE] standby=notification_input_unavailable session_preserved=1");
            } else inputStandby=false;
        }
    }
    if(resumePending && int32_t(millis()-resumeAt)>=0) {
        resumePending=false;inputMuted=false;silence.activity(activityNow());providerActivityAt=activityNow();
        appLog().println("[MIC_GATE] resumed=playback_finished");
    }
    // Ambient energy is not evidence of speech. Without provider speech, only
    // provider events/playback completion renew the listening deadline.
    const uint32_t providerQuiet=uint32_t(activityNow()-providerActivityAt);
    const bool staleSpeech=speaking && (silence.quietMs(activityNow())>=CONVERSATION_IDLE_TIMEOUT_MS ||
                                       providerQuiet>=CONVERSATION_SPEECH_WATCHDOG_MS);
    if(machine.state()==ConversationState::Listening && !conversationInputMuted() && !activityClock.paused() &&
       ((!speaking && providerQuiet>=CONVERSATION_IDLE_TIMEOUT_MS) || staleSpeech)) {
        if(speaking && providerQuiet>=CONVERSATION_SPEECH_WATCHDOG_MS) {
            conversationClose(ConversationCloseReason::SpeechWatchdog);
        } else {
            inputStandby=true;standbyAt=activityNow();inputMuted=true;resumePending=false;speaking=false;
            if(input.stop) input.stop();
            appLog().printf("[MIC_GATE t=%lu] standby=silence session_preserved=1 provider_quiet_ms=%lu local_quiet_ms=%lu\n",
                (unsigned long)millis(),(unsigned long)providerQuiet,(unsigned long)silence.quietMs(activityNow()));
        }
    }
    if(inputStandby && !notificationPending && !notificationStandby && machine.state()==ConversationState::Listening && !activityClock.paused() &&
       uint32_t(activityNow()-standbyAt)>=CONVERSATION_STANDBY_TIMEOUT_MS)
        conversationClose(ConversationCloseReason::StandbyTimeout);
#if PIUU_CONVERSATION_DIAGNOSTICS
    static uint32_t lastDiagnostics=0;
    if(machine.state()!=ConversationState::Idle && uint32_t(millis()-lastDiagnostics)>=5000) {
        lastDiagnostics=millis();conversationLogDiagnostics();
    }
#endif
}
ConversationAction conversationToggleInput() {
    if(inputStandby && machine.state()==ConversationState::Listening)
        return conversationResumeInput() ? ConversationAction::Resumed : ConversationAction::InputUnavailable;
    if(machine.state()!=ConversationState::Idle) {
        conversationAbort();return ConversationAction::Stopped;
    }
    if (!appServicesWifiReady()) return ConversationAction::NoWifi;
    if (!appServicesReady()) return ConversationAction::ServicesUnavailable;
    if (!appServicesServerReady()) return ConversationAction::NoServer;
    if (machine.state()!=ConversationState::Idle) return ConversationAction::Busy;
    return conversationStart() ? ConversationAction::Started : ConversationAction::InputUnavailable;
}

void conversationLogDiagnostics() {
#if PIUU_CONVERSATION_DIAGNOSTICS
    appLog().printf("[CONV_STATS t=%lu] state=%s level=%u quiet_ms=%lu threshold=%u close_ms=%lu provider_speech=%d hold_ms=%u muted=%d provider_quiet_ms=%lu\n",
        (unsigned long)millis(),stateName(machine.state()),unsigned(silence.level()),
        (unsigned long)silence.quietMs(activityNow()),unsigned(CONVERSATION_ACTIVITY_LEVEL),
        (unsigned long)CONVERSATION_IDLE_TIMEOUT_MS,speaking,unsigned(CONVERSATION_ACTIVITY_HOLD_MS),inputMuted,(unsigned long)(activityNow()-providerActivityAt));
#endif
}
