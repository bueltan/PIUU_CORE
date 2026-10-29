#include "app/device_volume.h"
#include "audio/volume_control.h"
#include "config/settings.h"
#include "hardware/app_log.h"

void deviceVolumeCommand(const uint8_t request[3],uint8_t reply[4]) {
    const unsigned operation=request[0];
    const int value=request[1]<128 ? int(request[1]) : int(request[1])-256;
    const bool valid=operation<=3 && request[2]<=1 && (!request[2] || operation==1) &&
        ((operation==0 && value==0) || (operation==1 && value>=0 && value<=100) ||
         (operation==2 && value>=-100 && value<=100) || (operation==3 && (value==0 || value==1)));
    reply[0]=valid?0:1;
    if(valid) {
        if(operation==1) audioVolumeSetPercent(unsigned(value));
        else if(operation==2) {
            const int next=int(audioVolumePercent())+value;
            audioVolumeSetPercent(unsigned(next<0?0:next>100?100:next));
        } else if(operation==3) audioVolumeSetMuted(value!=0);
        if(request[2] && !settingsSetVolume(audioVolumePercent())) reply[0]=2;
    }
    reply[1]=uint8_t(audioVolumePercent());
    reply[2]=uint8_t(audioVolumeBeforeMute());
    reply[3]=settings().volumePercent;
#if PIUU_VOLUME_DIAGNOSTICS
    appLog().printf("[VOLUME] source=remote op=%u value=%d persist=%u status=%u live=%u restore=%u saved=%u\n",
        operation,value,unsigned(request[2]),unsigned(reply[0]),unsigned(reply[1]),unsigned(reply[2]),unsigned(reply[3]));
#endif
}
