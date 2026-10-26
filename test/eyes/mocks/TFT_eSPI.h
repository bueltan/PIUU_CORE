#pragma once
#include <stdint.h>
#include <assert.h>
#define TFT_BLACK 0
class TFT_eSPI {
public:
    unsigned calls=0,transferred=0,rotation=0;
    bool swap=false;
    void setRotation(unsigned value){rotation=value;}
    void setSwapBytes(bool value){swap=value;}
    uint16_t screen[320*240]{};
    void pushImage(int x,int y,int w,int h,uint16_t* pixels){
        assert(x>=0&&y>=0&&w>0&&h>0&&h<=4&&x+w<=320&&y+h<=240);
        ++calls;transferred+=w*h;
        for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)screen[(y+yy)*320+x+xx]=pixels[yy*w+xx];
    }
};
