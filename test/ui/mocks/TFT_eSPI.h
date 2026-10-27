#pragma once
#include <Arduino.h>
#include <cassert>
#define TFT_BLACK 0
#define TFT_GREEN 1
#define TFT_WHITE 2
#define TFT_ORANGE 3
class TFT_eSPI {
    int x_=0,y_=0,size_=1;
public:
    static void bounds(int x,int y,int w,int h) { assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=320 && y+h<=240); }
    void fillScreen(int) {}
    void setTextColor(int,int) {}
    void setTextSize(int size) {size_=size;}
    void setCursor(int x,int y) {x_=x;y_=y;}
    int textWidth(const String &s) {return s.length()*6*size_;}
    void print(const String &s) {if(s.length()) bounds(x_,y_,textWidth(s),8*size_);x_+=textWidth(s);}
    void print(int n) {print(String(n));}
    void fillRect(int x,int y,int w,int h,int) {bounds(x,y,w,h);}
    void drawRect(int x,int y,int w,int h,int) {bounds(x,y,w,h);}
    void fillRoundRect(int x,int y,int w,int h,int,int) {bounds(x,y,w,h);}
    void drawRoundRect(int x,int y,int w,int h,int,int) {bounds(x,y,w,h);}
    void drawBitmap(int x,int y,const uint8_t*,int w,int h,int) {bounds(x,y,w,h);}
    void fillCircle(int x,int y,int r,int) {bounds(x-r,y-r,2*r+1,2*r+1);}
    void drawCircle(int x,int y,int r,int) {bounds(x-r,y-r,2*r+1,2*r+1);}
};
class TFT_eSPI_Button {
    int x_=0,y_=0,w_=0,h_=0;
public:
    void initButton(TFT_eSPI*,int x,int y,int w,int h,int,int,int,char*,int) {x_=x-w/2;y_=y-h/2;w_=w;h_=h;TFT_eSPI::bounds(x_,y_,w_,h_);}
    void drawButton(bool) {TFT_eSPI::bounds(x_,y_,w_,h_);}
    bool contains(int x,int y) {return x>=x_ && y>=y_ && x<x_+w_ && y<y_+h_;}
};
