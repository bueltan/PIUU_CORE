/**
 * @file network_udp.cpp
 * @brief Mutex-protected access to the shared UDP socket. Datagrams are returned unchanged.
 */

#include "network/network_udp.h"

#include <Arduino.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

static SemaphoreHandle_t udpMutex = nullptr;

static WiFiUDP udp;

/**
 * @brief Binds the socket to localPort. Returns true if udp.begin successfully opens the local
 * port.
 */
bool networkUdpInit(uint16_t localPort)
{
    if (!udpMutex) udpMutex = xSemaphoreCreateMutex();
    return udpMutex && udp.begin(localPort) == 1;
}

/**
 * @brief Sends len bytes to serverIp:serverPort under udpMutex, without waiting for the
 * lock. Returns true on successful local transmission.
 */
bool networkUdpSendRaw(const char *serverIp, uint16_t serverPort, const uint8_t *data, size_t len)
{
    if (udpMutex == nullptr)
        return false;

    if (xSemaphoreTake(udpMutex, 0) != pdTRUE)
        return false;

    udp.beginPacket(serverIp, serverPort);
    udp.write(data, len);
    const int ok = udp.endPacket();

    xSemaphoreGive(udpMutex);
    return ok == 1;
}

/**
 * @brief Queries the next datagram size under udpMutex. Returns zero when no packet is available
 * or the lock cannot be acquired within 20 ms.
 */
int networkUdpParsePacket()
{
    if (udpMutex == nullptr)
        return 0;

    if (xSemaphoreTake(udpMutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return 0;

    const int packetSize = udp.parsePacket();

    xSemaphoreGive(udpMutex);
    return packetSize;
}

/**
 * @brief Reads up to bufferSize bytes unchanged; returns zero for no data or a locking failure.
 */
int networkUdpRead(uint8_t *buffer, size_t bufferSize)
{
    if (udpMutex == nullptr || buffer == nullptr || bufferSize == 0)
        return 0;

    if (xSemaphoreTake(udpMutex, pdMS_TO_TICKS(20)) != pdTRUE)
        return 0;

    const int n = udp.read(buffer, bufferSize);

    xSemaphoreGive(udpMutex);

    if (n <= 0)
        return 0;

    return n;
}