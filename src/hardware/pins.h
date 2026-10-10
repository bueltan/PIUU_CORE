/**
 * @file pins.h
 * @brief GPIO and I2S port assignments for the DAC and touch controller. Display bus
 * configuration lives in platformio.ini.
 */

#pragma once
#include <driver/i2s.h>

// ====================== I2S DAC (TX) ======================
constexpr int I2S_DAC_BCK  = 26;
constexpr int I2S_DAC_WS   = 27;
constexpr int I2S_DAC_DATA = 22;
constexpr i2s_port_t I2S_DAC_PORT = I2S_NUM_1;

// ====================== TOUCH ======================
constexpr int XPT2046_IRQ  = 36;
constexpr int XPT2046_MOSI = 32;
constexpr int XPT2046_MISO = 39;
constexpr int XPT2046_CLK  = 25;
constexpr int XPT2046_CS   = 33;
