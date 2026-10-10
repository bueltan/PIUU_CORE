#pragma once
#include <stdint.h>
// Bound at setup; lifecycle stays independent of transport and display drivers.
struct ConversationTransportPort {
    bool (*begin)();
    void (*cancel)(uint32_t playedMs);
    bool (*takeFailure)();
    uint32_t (*playedMs)();
    void (*finish)();
};
void conversationSetTransportPort(ConversationTransportPort);
bool conversationInterrupt();
