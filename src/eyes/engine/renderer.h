#pragma once
#include <TFT_eSPI.h>
#include "contour.h"
namespace Eyes {
class Renderer {
public:
    explicit Renderer(TFT_eSPI& display):display_(display) {}
    bool busy() const { return region_<count_; }
    void invalidate() { region_=count_=0; invalid_=true; }
    void begin(const Pose& pose,const Config& config);
    void step(); // One bounded transfer, up to four rows.
private:
    struct Rect {int x,y,right,bottom; Rect(int a=0,int b=0,int c=0,int d=0):x(a),y(b),right(c),bottom(d) {}};
    TFT_eSPI& display_;
    uint16_t pixels_[Width*4]{};
    Contour contours_[2]{};
    Rect old_[2]{},regions_[2]{};
    uint16_t color_=0,background_=0;
    unsigned region_=0,count_=0;
    int row_=0;
    bool invalid_=true;
    static Rect bounds(const Contour& c);
    static Rect unite(Rect a,Rect b);
};
}
