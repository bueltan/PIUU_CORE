#include "audio/audio_output.h"
#include "audio/volume_control.h"
#include "driver/i2s.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
Logger Serial;
static std::vector<int16_t> sent;
static unsigned calls=0;
static bool shortWrite=false,failWrite=false;
esp_err_t i2s_driver_install(i2s_port_t,const i2s_config_t *cfg,int,void *) {
    assert(cfg->sample_rate==16000 && cfg->bits_per_sample==16);
    return ESP_OK;
}
esp_err_t i2s_driver_uninstall(i2s_port_t) { return ESP_OK; }
esp_err_t i2s_set_pin(i2s_port_t,const i2s_pin_config_t *) { return ESP_OK; }
esp_err_t i2s_zero_dma_buffer(i2s_port_t) { return ESP_OK; }
esp_err_t i2s_stop(i2s_port_t) { return ESP_OK; }
esp_err_t i2s_start(i2s_port_t) { return ESP_OK; }
esp_err_t i2s_write(i2s_port_t,const void *data,size_t bytes,size_t *written,TickType_t) {
    ++calls;*written=failWrite ? 0 : shortWrite ? 20 : bytes;
    const auto samples=static_cast<const int16_t *>(data);
    sent.insert(sent.end(),samples,samples+*written/2);
    return failWrite ? ESP_FAIL : ESP_OK;
}
static void reset() { sent.clear();calls=0; }
int main() {
    assert(audioVolumePercent()==VolumeConfig::DefaultPercent);
    int32_t previous=0;
    for(unsigned v=0;v<=100;++v) {
        const int32_t gain=audioVolumeGainForPercent(v);
        assert(gain>=previous && gain<=VolumeConfig::Unity);
        previous=gain;
    }
    assert(audioVolumeGainForPercent(0)==0 && audioVolumeGainForPercent(100)==65536);
    // New default is below the old gain of 0.02, with useful low-volume resolution.
    assert(audioVolumeGainForPercent(20)>0 && audioVolumeGainForPercent(20)<65536*0.02);
    assert(audioOutputInit());
    const int16_t pattern[]={-32768,32767,-16384,16384,0};
    std::vector<int16_t> pcm(640);
    for(size_t i=0;i<pcm.size();++i) pcm[i]=pattern[i%5];
    const auto original=pcm;
    for(unsigned v : {100u,20u,0u}) {
        assert(audioVolumeSetPercent(v));reset();
        const auto result=audioOutputWrite(pcm.data(),pcm.size());
        assert(result.error==0 && result.bytes==1280 && calls==3 && sent.size()==640);
        for(size_t i=80;i<sent.size();++i)
            assert(sent[i]==int16_t(int32_t(pcm[i])*audioVolumeGainQ16()/65536));
        assert(pcm==original);
        if (!v) for(size_t i=80;i<sent.size();++i) assert(sent[i]==0);
    }
    const int32_t oldGain=audioVolumeGainQ16();
    assert(!audioVolumeSetPercent(101) && audioVolumePercent()==0 && audioVolumeGainQ16()==oldGain);
    AudioVolumeRamp ramp;
    ramp.setTarget(65536);int last=0;
    for(int i=0;i<80;++i) {
        const int next=ramp.apply(32767);assert(next>=last && next-last<=410);last=next;
    }
    assert(last==32767);
    ramp.setTarget(0);
    for(int i=0;i<80;++i) {const int next=ramp.apply(32767);assert(next<=last);last=next;}
    assert(last==0 && ramp.apply(-32768)==0);
    // A short write must stop here instead of dropping its tail and sending chunk 2.
    reset();shortWrite=true;
    auto result=audioOutputWrite(pcm.data(),pcm.size());
    assert(result.error==0 && result.bytes==20 && calls==1);
    reset();shortWrite=false;failWrite=true;
    result=audioOutputWrite(pcm.data(),pcm.size());
    assert(result.error==ESP_FAIL && result.bytes==0 && calls==1);
    audioOutputDeinit();
    puts("PASS: I2S-bound volume, full-scale signs, exact mute, low levels, ramp, immutable input and short/error writes");
}
