#pragma once
#include <Arduino.h>
// Controller task: init/send/reset/poll. RX task: parse/read only.
// Backend protocol adapter; the UDP transport below remains opaque.
bool serverLinkInit();
bool serverLinkSend(const uint8_t *data, size_t size);
void serverLinkResetHeartbeat();
bool serverLinkPollHeartbeat(bool online);
int serverLinkParsePacket();
int serverLinkRead(uint8_t *data, size_t capacity);
// Controller-only v2 lifecycle. RX never mutates the session client.
bool serverLinkBeginTurn();
void serverLinkCancelTurn(uint32_t playedMs);
bool serverLinkTakeFailure();

void serverLinkFinishTurn();

void serverLinkLogDiagnostics();
