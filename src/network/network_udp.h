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
 * @brief Queries the next datagram size under udpMutex. Returns zero when no packet is available
 * or the lock cannot be acquired within 20 ms.
 */
int networkUdpParsePacket();
/**
 * @brief Reads up to bufferSize bytes unchanged; returns zero for no data or a locking failure.
 */
int networkUdpRead(uint8_t *buffer, size_t bufferSize);