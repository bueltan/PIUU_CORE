#include "app/conversation.h"
#include "app/conversation_event.h"
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

namespace { SilenceMonitor silence; bool speaking=false; ConversationMachine machine; QueueHandle_t events = nullptr; ConversationInputPort input{}; ConversationTransportPort transport{}; }
namespace {
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
    audioDownlinkBeginTurn(machine.epoch());silence.reset(millis());speaking=false;
    return true;
}
void conversationClose() { conversationAbort(); }
void conversationAbort() {
#if PIUU_DIAGNOSTICS
    if(machine.state()!=ConversationState::Idle) audioDownlinkLogDiagnostics();
#endif
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
    const auto before=machine.state();machine.speechStarted();
    audioDownlinkBeginTurn(machine.epoch());silence.activity(millis());speaking=false;
    transition(before,"manual_interrupt");return true;
}
bool conversationResponseReady() { return audioDownlinkEpochReady(); }
uint32_t conversationServerEvent(ConversationEvent event,uint32_t response,uint32_t utterance) {
    if(machine.state()==ConversationState::Idle) return 0;
    const uint32_t played=transport.playedMs?transport.playedMs():0;
    const auto before=machine.state();silence.activity(millis());
    if(event==ConversationEvent::SpeechStarted) {
        speaking=true;machine.speechStarted();audioDownlinkBeginTurn(machine.epoch());
    } else if(event==ConversationEvent::SpeechStopped) {
        speaking=false;machine.speechStopped();
    } else if(event==ConversationEvent::ResponseStarted) {
        speaking=false;machine.responseStarted();audioDownlinkBeginTurn(machine.epoch());audioDownlinkExpectResponse();
    } else if(event==ConversationEvent::Ready) {
        machine.ready();silence.reset(millis());
    } else if(event==ConversationEvent::Closed) conversationAbort();
    appLog().printf("[CONV_EVENT t=%lu] code=%u response=%lu utterance=%lu played_ms=%lu\n",
        (unsigned long)millis(),unsigned(event),(unsigned long)response,(unsigned long)utterance,(unsigned long)played);
    transition(before,"server_event");return played;
}
void conversationObserveAudio(const int16_t *samples,size_t count) {
    if(machine.state()!=ConversationState::Idle)
        silence.observe(samples,count,millis(),CONVERSATION_ACTIVITY_LEVEL);
}
void conversationPoll() {
    if(transport.takeFailure && transport.takeFailure()) conversationAbort();
    if (machine.state() != ConversationState::Idle && !appServicesWifiReady()) conversationAbort();
    const ConversationState beforePlayback=machine.state();
    PlaybackEvent event{};
    for (int i = 0; i < 8 && events && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
        { const auto before=machine.state();machine.playback(event);transition(before,"playback_event"); }
    if (beforePlayback!=ConversationState::Listening && machine.state()==ConversationState::Listening) {
        if(transport.played) transport.played(transport.playedMs?transport.playedMs():0);
        silence.activity(millis());
    }
    if(machine.state()==ConversationState::Listening && !speaking && silence.quietMs(millis())>=CONVERSATION_IDLE_TIMEOUT_MS) {
        appLog().printf("[CONV] silence_close quiet_ms=%lu level=%u threshold=%u\n",
            (unsigned long)silence.quietMs(millis()),unsigned(silence.level()),unsigned(CONVERSATION_ACTIVITY_LEVEL));
        conversationClose();
    }
}
ConversationAction conversationToggleInput() {
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
#if PIUU_DIAGNOSTICS
    appLog().printf("[CONV_STATS t=%lu] state=%s level=%u quiet_ms=%lu threshold=%u close_ms=%lu provider_speech=%d\n",
        (unsigned long)millis(),stateName(machine.state()),unsigned(silence.level()),
        (unsigned long)silence.quietMs(millis()),unsigned(CONVERSATION_ACTIVITY_LEVEL),
        (unsigned long)CONVERSATION_IDLE_TIMEOUT_MS,speaking);
#endif
}
