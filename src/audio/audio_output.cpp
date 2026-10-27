#include "hardware/app_log.h"
#include "audio/audio_output.h"
#include "hardware/pins.h"
#include "config/app_config.h"

static bool outputInitialized = false;

bool audioOutputInit()
{
    if (outputInitialized) return true;
    i2s_config_t config = {};
    config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = SAMPLE_RATE_HZ;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_DAC_BCK;
    pins.ws_io_num = I2S_DAC_WS;
    pins.data_out_num = I2S_DAC_DATA;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_DAC_PORT, &config, 0, nullptr);
    if (err != ESP_OK)
    {
        appLog().printf("[I2S] driver_install failed: %d\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_DAC_PORT, &pins);
    if (err != ESP_OK)
    {
        appLog().printf("[I2S] set_pin failed: %d\n", err);
        i2s_driver_uninstall(I2S_DAC_PORT);
        return false;
    }

    err = i2s_zero_dma_buffer(I2S_DAC_PORT);
    if (err != ESP_OK)
    {
        appLog().printf("[I2S] zero_dma_buffer failed: %d\n", err);
        i2s_driver_uninstall(I2S_DAC_PORT);
        return false;
    }

    outputInitialized = true;
    return true;
}

// AudioOutputWriteResult audioOutputWrite(const int16_t *pcm, size_t samples)
// {
//     size_t bytesWritten = 0;
//     const esp_err_t error=i2s_write(I2S_DAC_PORT, pcm, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
//     return {int32_t(error),bytesWritten};
// }

AudioOutputWriteResult audioOutputWrite(const int16_t *pcm, size_t samples)
{
    const size_t chunk = 256;          // tamaño de bloque seguro
    size_t bytesWrittenTotal = 0;
    esp_err_t lastError = ESP_OK;
    
    // Factor de escala ajustable (ej. 0.25 para reducir al 25% / -12dB, 
    // o puedes ajustarlo según necesites para evitar la saturación)
    const float volumeScale = 0.25f; 

    for (size_t offset = 0; offset < samples; offset += chunk) {
        size_t n = std::min(chunk, samples - offset);
        int16_t scaled[chunk];
        
        for (size_t i = 0; i < n; ++i) {
            // Aplicar escala y asegurar que no haya desbordamiento
            int32_t val = static_cast<int32_t>(pcm[offset + i] * volumeScale);
            
            // Clamping para evitar distorsión digital si excede los límites
            if (val > 32767)  val = 32767;
            if (val < -32768) val = -32768;
            
            scaled[i] = static_cast<int16_t>(val);
        }
        
        size_t bytesWritten = 0;
        lastError = i2s_write(I2S_DAC_PORT,
                              scaled,
                              n * sizeof(int16_t),
                              &bytesWritten,
                              portMAX_DELAY);
        bytesWrittenTotal += bytesWritten;
        if (lastError != ESP_OK) break;
    }
    return {int32_t(lastError), bytesWrittenTotal};
}

// Startup rollback only: caller must ensure playback workers are not running.
void audioOutputDeinit()
{
    if (!outputInitialized) return;
    i2s_driver_uninstall(I2S_DAC_PORT);
    outputInitialized = false;
}

void audioOutputFlush() {
    if(!outputInitialized) return;
    i2s_stop(I2S_DAC_PORT);
    i2s_zero_dma_buffer(I2S_DAC_PORT);
    i2s_start(I2S_DAC_PORT);
}
