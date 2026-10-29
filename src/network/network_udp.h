/**
 * @file network_udp.h
 * @brief Mutex-protected access to the shared UDP socket. Datagrams are returned unchanged.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Binds the socket to localPort. Returns true if udp.begin successfully opens the local
 * port.
 */
bool networkUdpInit(uint16_t localPort);
/**
 * @brief Sends len bytes to serverIp:serverPort under udpMutex, without waiting for the
 * lock. Returns true on successful local transmission.
 */
bool networkUdpSendRaw(const char *serverIp, uint16_t serverPort, const uint8_t *data, size_t len);

/**
 * @brief Returns the pending datagram size, retrying an unread buffer before
 * parsing another packet. Returns zero for no data or a mutex timeout (20 ms).
 * One RX consumer owns the parse/read sequence; TX may run between the calls.
 */
int networkUdpParsePacket();
/**
 * @brief Reads a complete datagram unchanged. Mutex timeouts/zero-byte reads
 * preserve it for retry. Oversized/partial datagrams and their tails are dropped.
 */
int networkUdpRead(uint8_t *buffer, size_t bufferSize);
// Controller only; cumulative local I/O counters, no worker printing.
void networkUdpLogDiagnostics();
