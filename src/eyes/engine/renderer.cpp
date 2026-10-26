#include "renderer.h"
#include <algorithm>
namespace Eyes {
Renderer::Rect Renderer::bounds(const Contour& c){
    if(c.legacy){
        float hw=c.original.width*.5f*c.scale,hh=c.original.height*.5f*c.scale;
        return {std::max(0,std::min(Width,int(floorf(c.cx-hw)))),std::max(0,std::min(Height,int(floorf(c.cy-hh)))),
            std::max(0,std::min(Width,int(ceilf(c.cx+hw)))),std::max(0,std::min(Height,int(ceilf(c.cy+hh))))};
    }
    float x=Width,y=Height,r=0,b=0;
    for(const auto&p:c.points){x=std::min(x,p.x);y=std::min(y,p.y);r=std::max(r,p.x);b=std::max(b,p.y);}
    return {std::max(0,std::min(Width,int(floorf(x)))),std::max(0,std::min(Height,int(floorf(y)))),
        std::max(0,std::min(Width,int(ceilf(r)))),std::max(0,std::min(Height,int(ceilf(b))))};
}
Renderer::Rect Renderer::unite(Rect a,Rect b){
    if(a.x>=a.right||a.y>=a.bottom)return b;
    if(b.x>=b.right||b.y>=b.bottom)return a;
    return {std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};
}
void Renderer::begin(const Pose&p,const Config&c){
    float scale=std::max(25,std::min(200,int(c.sizePercent)))*.01f;
    float cx=Width*.5f+c.offsetX+p.gazeX,cy=Height*.5f+c.offsetY+p.gazeY;
    contours_[0]=makeContour(p.left,cx-c.separation*.5f,cy,scale,c.roundness);
    contours_[1]=makeContour(p.right,cx+c.separation*.5f,cy,scale,c.roundness);
    bool backgroundChanged=invalid_ || background_!=c.backgroundColor;
    invalid_=false;
    background_=c.backgroundColor;
    color_=c.color;count_=0;region_=0;
    for(unsigned i=0;i<2;++i){auto current=bounds(contours_[i]);auto dirty=unite(old_[i],current);old_[i]=current;
        if(dirty.x<dirty.right&&dirty.y<dirty.bottom)regions_[count_++]=dirty;
    }
    if(count_==2){auto a=regions_[0],b=regions_[1];
        if(a.x<b.right&&b.x<a.right&&a.y<b.bottom&&b.y<a.bottom){regions_[0]=unite(a,b);count_=1;}
    }
    if(backgroundChanged){regions_[0]=Rect{0,0,Width,Height};count_=1;}
    row_=count_?regions_[0].y:0;
}
void Renderer::step(){
    if(!busy())return;
    const auto&r=regions_[region_];int width=r.right-r.x,rows=std::min(4,r.bottom-row_);
    std::fill_n(pixels_,width*rows,background_);
    float crossings[ContourPoints];
    for(int yy=0;yy<rows;++yy)for(const auto&c:contours_){
        unsigned n=scanline(c,row_+yy+.5f,crossings);
        for(unsigned i=0;i+1<n;i+=2){
            int start=std::max(r.x,int(ceilf(crossings[i]-.5f)));
            int end=std::min(r.right,int(ceilf(crossings[i+1]-.5f)));
            if(end>start)std::fill_n(pixels_+yy*width+start-r.x,end-start,color_);
        }
    }
    display_.pushImage(r.x,row_,width,rows,pixels_);row_+=rows;
    if(row_>=r.bottom){++region_;if(busy())row_=regions_[region_].y;}
}
}
