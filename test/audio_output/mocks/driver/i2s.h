#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <cstddef>
using i2s_port_t=int;
using i2s_mode_t=int;
constexpr int I2S_NUM_1=1, I2S_MODE_MASTER=1, I2S_MODE_TX=2;
constexpr int I2S_BITS_PER_SAMPLE_16BIT=16,I2S_CHANNEL_FMT_ONLY_LEFT=1;
constexpr int I2S_COMM_FORMAT_STAND_I2S=1, ESP_INTR_FLAG_LEVEL1=1, I2S_PIN_NO_CHANGE=-1;
struct i2s_config_t {
    int mode,sample_rate,bits_per_sample,channel_format,communication_format;
    int intr_alloc_flags,dma_buf_count,dma_buf_len;
    bool use_apll,tx_desc_auto_clear;
    int fixed_mclk;
};
struct i2s_pin_config_t { int bck_io_num,ws_io_num,data_out_num,data_in_num; };
esp_err_t i2s_driver_install(i2s_port_t,const i2s_config_t *,int,void *);
esp_err_t i2s_driver_uninstall(i2s_port_t);
esp_err_t i2s_set_pin(i2s_port_t,const i2s_pin_config_t *);
esp_err_t i2s_zero_dma_buffer(i2s_port_t);
esp_err_t i2s_stop(i2s_port_t);
esp_err_t i2s_start(i2s_port_t);
esp_err_t i2s_write(i2s_port_t,const void *,size_t,size_t *,TickType_t);
