#pragma once
#include <stdint.h>
#include <stddef.h>
// Controller adapter: binds conversation input and routes ordered UART packets.
bool earsSessionInit();
enum class EarsSessionNotice { None, Started, Rejected, Offline, Restarted, ControlLost };
EarsSessionNotice earsSessionPoll();
using EarsExtraEventSink=void(*)(uint8_t code,const uint8_t *payload,size_t size);
// Optional extension hook, invoked only from loop; payload valid for this call.
void earsSessionSetExtraEventSink(EarsExtraEventSink sink);
struct EarsSessionStats { uint32_t forwarded,sequenceGaps,duplicates,unknownEvents,busyWakes; };
EarsSessionStats earsSessionStats();
