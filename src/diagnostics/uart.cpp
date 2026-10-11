#include "diagnostics/uart.h"
#if PIUU_UART_DIAGNOSTICS
#include <Arduino.h>
#include "hardware/app_log.h"
#include "input/ears_link.h"
namespace Diagnostics {
void uartPoll() {
    static uint32_t lastReport=0,lastLine=0;
    static unsigned remaining=0;
    const uint32_t now=millis();
    if(uint32_t(now-lastReport)>=5000) {
        lastReport=lastLine=now;remaining=8;
        const auto s=earsLinkUartStats();
        appLog().printf("[UART_ERR t=%lu] fifo=%lu ring_full=%lu framing=%lu parity=%lu break=%lu rx_gap_events=%lu rx_missing=%lu resets=%lu sample_drop=%lu\n",
            (unsigned long)now,(unsigned long)s.fifoOverflow,(unsigned long)s.bufferFull,
            (unsigned long)s.frameError,(unsigned long)s.parityError,(unsigned long)s.breakError,
            (unsigned long)s.rxGaps,(unsigned long)s.rxMissing,(unsigned long)s.sequenceResets,(unsigned long)s.samplesDropped);
        return;
    }
    if(!remaining || uint32_t(now-lastLine)<80)return;
    lastLine=now;
    EarsGapSample sample{};
    if(!earsLinkTakeGapSample(sample)) { remaining=0;return; }
    --remaining;
    const uint32_t distance=sample.received-sample.expected;
    appLog().printf("[UART_GAP t=%lu] stage=%s expected=%lu received=%lu missing=%lu backwards=%d order=%lu\n",
        (unsigned long)sample.timeMs,sample.consumer?"app":"rx",
        (unsigned long)sample.expected,(unsigned long)sample.received,
        (unsigned long)(distance<0x80000000u?distance:0),distance>=0x80000000u,(unsigned long)sample.order);
}
}
#endif
