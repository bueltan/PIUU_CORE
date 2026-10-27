#pragma once
#include <Arduino.h>
#include "protocol/session_protocol.h"
// Register before RX starts. Sink must copy immediately and never block or draw.
using ServerEyesSink=void(*)(const SessionProtocol::Packet&);
void serverLinkSetEyesSink(ServerEyesSink);
SessionProtocol::Identity serverLinkIdentity(); // loop only
// Controller task: init/send/reset/poll. RX task: parse/read only.
// Backend protocol adapter; the UDP transport below remains opaque.
bool serverLinkInit();
bool serverLinkSend(const uint8_t *data, size_t size);
void serverLinkResetHeartbeat();
bool serverLinkReachable();
// RX-only: call after validating the inner Opus format; throttled owner notification.
void serverLinkObserveMedia();
bool serverLinkPollHeartbeat(bool online);
int serverLinkParsePacket();
int serverLinkRead(uint8_t *data, size_t capacity);
// Controller-only v3 lifecycle. RX never mutates the session client.
bool serverLinkOpenConversation();
void serverLinkCloseConversation(uint32_t playedMs);
bool serverLinkTakeFailure();


void serverLinkLogDiagnostics();

void serverLinkPlaybackFinished(uint32_t playedMs);
bool serverLinkInterrupt(uint32_t playedMs);

// RX-only: acknowledge a validated END after admission to playback state.
void serverLinkAcknowledgeEnd(uint32_t sequence);
// Controller-only: bounded downlink recovery requests.
bool serverLinkRequestDownlink(uint32_t base,uint16_t mask);

// Loop-only eye-plan negotiation and nonblocking receipt ACK.
bool serverLinkEyesPlansEnabled();
bool serverLinkAcknowledgeEyesPlan(SessionProtocol::Identity,uint32_t sequence);
