#pragma once
#include "diagnostics/config.h"
namespace Diagnostics {
#if PIUU_UART_DIAGNOSTICS
void uartPoll();
#else
inline void uartPoll() {}
#endif
}
