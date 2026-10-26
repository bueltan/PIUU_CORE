#pragma once
#include "eyes.h"
namespace Eyes {
struct Point {float x,y;};
constexpr unsigned ContourPoints=32;
struct Contour {Point points[ContourPoints]; Eye original{}; float cx=0,cy=0,scale=1; bool legacy=false;};
// Rounded quadratic joins sampled once per presentation, never per pixel.
Contour makeContour(const Eye& eye,float cx,float cy,float scale, uint8_t roundness=100);
unsigned scanline(const Contour& contour,float y,float* crossings);
}
