#pragma once
#include <stdint.h>
// Bound at setup; lifecycle stays independent of transport and display drivers.
struct ConversationTransportPort {
    bool (*begin)();
    void (*cancel)(uint32_t playedMs);
    bool (*takeFailure)();
    uint32_t (*playedMs)();
    bool (*interrupt)(uint32_t playedMs);
    void (*played)(uint32_t playedMs);
    void (*playbackFaultDiagnostics)();
};
void conversationSetTransportPort(ConversationTransportPort);
bool conversationInterrupt();
