#include "hardware/app_log.h"
#include "audio/audio_output.h"
#include "hardware/pins.h"
#include "config/app_config.h"
#include "diagnostics/heap_snapshot.h"
#include <esp_err.h>
#include <algorithm>
#include "audio/volume_control.h"

static bool outputInitialized = false;
static AudioVolumeRamp volumeRamp; // Playback task only.

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

    HeapSnapshot::capture().print(appLog(),"i2s_before_install");
    esp_err_t err = i2s_driver_install(I2S_DAC_PORT, &config, 0, nullptr);
    if (err != ESP_OK)
    {
        appLog().printf("[I2S] driver_install failed: %d (%s)\n", err, esp_err_to_name(err));
        HeapSnapshot::capture().print(appLog(),"i2s_install_failed");
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

    HeapSnapshot::capture().print(appLog(),"i2s_ready");
    outputInitialized = true;
    return true;
}

AudioOutputWriteResult audioOutputWrite(const int16_t *pcm, size_t samples)
{
    constexpr size_t chunk = 256;
    size_t bytesWrittenTotal = 0;
    esp_err_t lastError = ESP_OK;
#if PIUU_DAC_DIAGNOSTICS
    static int lastVolume = -1;
    static unsigned traceChunks = 0;
#endif
    for (size_t offset = 0; offset < samples; offset += chunk) {
        const size_t n = std::min(chunk, samples - offset);
        int16_t scaled[chunk];
        volumeRamp.setTarget(audioVolumeGainQ16());
        for (size_t i = 0; i < n; ++i)
            scaled[i] = volumeRamp.apply(pcm[offset + i]);

        size_t bytesWritten = 0;
        lastError = i2s_write(I2S_DAC_PORT, scaled, n * sizeof(int16_t),
                              &bytesWritten, portMAX_DELAY);
        bytesWrittenTotal += bytesWritten;
#if PIUU_DAC_DIAGNOSTICS
        const int volume = int(audioVolumePercent());
        if (volume != lastVolume) { lastVolume = volume; traceChunks = 3; }
        if (traceChunks) {
            --traceChunks;
            int32_t inputPeak = 0, outputPeak = 0;
            for (size_t i = 0; i < n; ++i) {
                const int32_t in = pcm[offset + i], out = scaled[i];
                inputPeak = std::max(inputPeak, in < 0 ? -in : in);
                outputPeak = std::max(outputPeak, out < 0 ? -out : out);
            }
            appLog().printf("[VOLUME_PCM] percent=%d gain_q16=%ld input_peak=%ld output_peak=%ld bytes=%u error=%ld\n",
                volume, (long)audioVolumeGainQ16(), (long)inputPeak, (long)outputPeak,
                unsigned(bytesWritten), (long)lastError);
        }
#endif
        // A short write must not silently skip its unwritten tail and send the
        // next chunk as though all requested samples had been accepted.
        if (lastError != ESP_OK || bytesWritten != n * sizeof(int16_t)) break;
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
