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
#include <atomic>
#include "hardware/app_log.h"

#if PIUU_NETWORK_DIAGNOSTICS || PIUU_PLAYBACK_DIAGNOSTICS
namespace {
std::atomic<uint32_t> txOk{0},txUninit{0},txBusy{0},txBegin{0},txShort{0},txEnd{0};
std::atomic<uint32_t> rxParseBusy{0},rxReadBusy{0},rxEmpty{0},rxPackets{0},rxBytes{0};
std::atomic<uint32_t> rxPending{0},rxOversized{0},rxShort{0};
}
#define UDP_COUNT(counter) (++counter)
#else
#define UDP_COUNT(counter) ((void)0)
#endif

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
    if (udpMutex == nullptr) { UDP_COUNT(txUninit);return false; }
    if (xSemaphoreTake(udpMutex, 0) != pdTRUE) { UDP_COUNT(txBusy);return false; }
    const int begun=udp.beginPacket(serverIp, serverPort);
    if(begun!=1) { UDP_COUNT(txBegin);xSemaphoreGive(udpMutex);return false; }
    const size_t written=udp.write(data, len);
    const int ok = udp.endPacket();

    xSemaphoreGive(udpMutex);
    if(written!=len) UDP_COUNT(txShort);
    if(ok!=1) UDP_COUNT(txEnd);
    if(written==len && ok==1) { UDP_COUNT(txOk);return true; }
    return false;
}

/**
 * @brief Returns unread buffered bytes before parsing another datagram. A failed
 * read/lock acquisition leaves that datagram available for the next RX poll.
 */
int networkUdpParsePacket()
{
    if (udpMutex == nullptr)
        return 0;

    if (xSemaphoreTake(udpMutex, pdMS_TO_TICKS(20)) != pdTRUE) { UDP_COUNT(rxParseBusy);return 0; }

    // NetworkUDP::parsePacket() returns zero while its RX buffer is populated.
    // In particular, read may have missed the mutex since the previous parse.
    const int pending = udp.available();
    const int packetSize = pending > 0 ? pending : udp.parsePacket();
    if(pending > 0) UDP_COUNT(rxPending);

    xSemaphoreGive(udpMutex);
    return packetSize;
}

/**
 * @brief Reads one whole datagram. Lock misses and zero-byte reads are retryable;
 * oversized or partially consumed datagrams are discarded, including their tail.
 */
int networkUdpRead(uint8_t *buffer, size_t bufferSize)
{
    if (udpMutex == nullptr || buffer == nullptr || bufferSize == 0)
        return 0;

    if (xSemaphoreTake(udpMutex, pdMS_TO_TICKS(20)) != pdTRUE) { UDP_COUNT(rxReadBusy);return 0; }

    const int expected = udp.available();
    if(expected > 0 && size_t(expected) > bufferSize) {
        udp.clear();xSemaphoreGive(udpMutex);UDP_COUNT(rxOversized);return 0;
    }
    const int n = udp.read(buffer, bufferSize);
    if(n > 0 && n != expected) {
        udp.clear();xSemaphoreGive(udpMutex);UDP_COUNT(rxShort);return 0;
    }

    xSemaphoreGive(udpMutex);

    if (n <= 0) { UDP_COUNT(rxEmpty);return 0; }
    UDP_COUNT(rxPackets);
#if PIUU_NETWORK_DIAGNOSTICS || PIUU_PLAYBACK_DIAGNOSTICS
    rxBytes.fetch_add(uint32_t(n));
#endif
    return n;
}

void networkUdpLogDiagnostics() {
#if PIUU_NETWORK_DIAGNOSTICS
    appLog().printf("[UDP_IO t=%lu] tx_ok=%lu tx_uninit=%lu tx_busy=%lu tx_begin_fail=%lu tx_short=%lu tx_end_fail=%lu rx_parse_busy=%lu rx_read_busy=%lu rx_empty=%lu rx_packets=%lu rx_bytes=%lu rx_pending=%lu rx_oversized=%lu rx_short=%lu\n",
        (unsigned long)millis(),(unsigned long)txOk.load(),(unsigned long)txUninit.load(),
        (unsigned long)txBusy.load(),(unsigned long)txBegin.load(),(unsigned long)txShort.load(),
        (unsigned long)txEnd.load(),(unsigned long)rxParseBusy.load(),(unsigned long)rxReadBusy.load(),
        (unsigned long)rxEmpty.load(),(unsigned long)rxPackets.load(),(unsigned long)rxBytes.load(),
        (unsigned long)rxPending.load(),(unsigned long)rxOversized.load(),(unsigned long)rxShort.load());
#endif
}

void networkUdpLogPlaybackFault(uint32_t fault) {
#if PIUU_NETWORK_DIAGNOSTICS || PIUU_PLAYBACK_DIAGNOSTICS
    appLog().printf("[UDP_FAULT t=%lu] fault=%lu packets=%lu bytes=%lu parse_busy=%lu read_busy=%lu pending=%lu empty=%lu oversized=%lu short=%lu\n",
        (unsigned long)millis(),(unsigned long)fault,(unsigned long)rxPackets.load(),(unsigned long)rxBytes.load(),
        (unsigned long)rxParseBusy.load(),(unsigned long)rxReadBusy.load(),(unsigned long)rxPending.load(),
        (unsigned long)rxEmpty.load(),(unsigned long)rxOversized.load(),(unsigned long)rxShort.load());
#else
    (void)fault;
#endif
}
