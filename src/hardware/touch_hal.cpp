#include "hardware/touch_hal.h"
#include <Arduino.h>
#include <XPT2046_Bitbang.h>
#include "hardware/pins.h"
static XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);
static constexpr int TOUCH_MIN_X=16, TOUCH_MAX_X=224, TOUCH_MIN_Y=14, TOUCH_MAX_Y=297;
static constexpr bool swapXY=true, invertX=true, invertY=false;
void initTouchHardware() { ts.begin(); }
bool getTouchPoint(uint16_t &x, uint16_t &y) {
  const int samples = 8;
  long sumX = 0;
  long sumY = 0;
  int valid = 0;

  for (int i = 0; i < samples; i++) {
    TouchPoint p = ts.getTouch();

    if (!(p.x == 0 && p.y == 0)) {
      sumX += p.x;
      sumY += p.y;
      valid++;
    }
    delay(2);
  }

  if (valid < 3) return false;

  int rawX = sumX / valid;
  int rawY = sumY / valid;

  if (swapXY) {
    int tmp = rawX;
    rawX = rawY;
    rawY = tmp;
  }

  int sx = map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, 239);
  int sy = map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 319);

  if (invertX) sx = 239 - sx;
  if (invertY) sy = 319 - sy;

  sx = constrain(sx, 0, 239);
  sy = constrain(sy, 0, 319);

  x = sx;
  y = sy;
  return true;
}

