#include "app/conversation.h"
#include "hardware/app_log.h"
#include "app/conversation_input.h"
#include "app/conversation_transport.h"
#include "app/app_services.h"
#include "audio/audio_downlink.h"
#include "audio/audio_uplink.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace { ConversationMachine machine; QueueHandle_t events = nullptr; ConversationInputPort input{}; ConversationTransportPort transport{}; }
namespace {
const char *stateName(ConversationState state) {
    switch(state) {
        case ConversationState::Idle: return "Idle";
        case ConversationState::Listening: return "Listening";
        case ConversationState::ClosingInput: return "ClosingInput";
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
        machine.state() != ConversationState::Idle || audioUplinkIsClosing()) return false;
    if (input.start && !input.start()) return false;
    if (transport.begin && !transport.begin()) { if(input.stop) input.stop();return false; }
    if (!audioUplinkBeginTurn()) { if(transport.cancel) transport.cancel(0);if (input.stop) input.stop();return false; }
    const auto before=machine.state();machine.begin();transition(before,"start");
    audioDownlinkBeginTurn(machine.epoch());
    return true;
}
void conversationEndInput() {
    const auto before=machine.state();
    if (!machine.closeInput()) return;
    transition(before,"end_input");
    audioUplinkEndTurn();
}
void conversationAbort() {
#if PIUU_DIAGNOSTICS
    if(machine.state()!=ConversationState::Idle) audioDownlinkLogDiagnostics();
#endif
    if (input.stop) input.stop();
    if(transport.cancel) transport.cancel(transport.playedMs?transport.playedMs():0);
    audioUplinkAbortTurn();
    const auto before=machine.state();machine.abort();transition(before,"abort");
    audioDownlinkBeginTurn(machine.epoch());
}
bool conversationInterrupt() {
    if(!transport.cancel || machine.state()==ConversationState::Idle || machine.state()==ConversationState::Listening) return false;
    appLog().printf("[CONV t=%lu] interrupt epoch=%lu\n",(unsigned long)millis(),(unsigned long)machine.epoch());
    conversationAbort();return conversationStart();
}
void conversationPoll() {
    if(transport.takeFailure && transport.takeFailure()) conversationAbort();
    if (machine.state() != ConversationState::Idle && !appServicesWifiReady()) conversationAbort();
    audioUplinkPoll();
    if (machine.state() == ConversationState::ClosingInput && !audioUplinkIsClosing()) {
        const auto before=machine.state();machine.inputClosed();transition(before,"input_closed");
        audioDownlinkExpectResponse();
    }
    const ConversationState beforePlayback=machine.state();
    PlaybackEvent event{};
    for (int i = 0; i < 8 && events && xQueueReceive(events, &event, 0) == pdTRUE; ++i)
        { const auto before=machine.state();machine.playback(event);transition(before,"playback_event"); }
    if (beforePlayback!=ConversationState::Idle && machine.state()==ConversationState::Idle) {
#if PIUU_DIAGNOSTICS
        audioDownlinkLogDiagnostics();
#endif
        if(transport.finish) transport.finish();
        if(input.stop) input.stop();
    }
}
ConversationAction conversationToggleInput() {
    // Closing an existing input remains possible even if backend heartbeat expires.
    if (machine.state() == ConversationState::Listening) {
        conversationEndInput(); return ConversationAction::Stopped;
    }
    if(transport.cancel && machine.state()!=ConversationState::Idle)
        return conversationInterrupt()?ConversationAction::Started:ConversationAction::InputUnavailable;
    if (!appServicesWifiReady()) return ConversationAction::NoWifi;
    if (!appServicesReady()) return ConversationAction::ServicesUnavailable;
    if (!appServicesServerReady()) return ConversationAction::NoServer;
    if (machine.state()!=ConversationState::Idle || audioUplinkIsClosing()) return ConversationAction::Busy;
    return conversationStart() ? ConversationAction::Started : ConversationAction::InputUnavailable;
}
