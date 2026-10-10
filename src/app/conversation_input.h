#pragma once
// Synchronous controller callbacks submit nonblocking input intent. start=false
// means input is unavailable. Keep input streaming until stop (not end-of-input).
struct ConversationInputPort { bool (*start)(); void (*stop)(); };
void conversationSetInputPort(ConversationInputPort port);
