#include "contour.h"
#include <algorithm>
namespace Eyes {
Contour makeContour(const Eye&e,float cx,float cy,float scale,uint8_t roundness){
    float hw=e.width*.5f,hh=e.height*.5f;
    float top=-hh+e.lid*e.height;
    float arch=e.arch;
    float leftTop=top-e.slope*hw*.65f,rightTop=top+e.slope*hw*.65f;
    leftTop+=arch*hh*.45f;rightTop+=arch*hh*.45f;
    float bottom=hh*(1-arch)+(-hh+14)*arch;
    float shoulder=hh*(1-arch)+(-hh+14+hh*.7f)*arch;
    float sideTop=top*(1-arch)+(-hh+hh*.7f)*arch;
    // The whole outline is rounded, including lid/side intersections and arch tips.
    Point anchors[8]={
        {-hw*.65f,leftTop},{hw*.65f,rightTop},
        {hw,(sideTop+e.slope*hw+shoulder)*.5f},
        {hw*.65f,shoulder},{0,bottom},
        {-hw*.65f,shoulder},{-hw,(sideTop-e.slope*hw+shoulder)*.5f},
        {0,top}
    };
    // Last anchor belongs to the upper contour, not after the left side:
    // reorder to keep a simple clockwise outline with a centered upper arch.
    Point ordered[8]={anchors[0],anchors[7],anchors[1],anchors[2],anchors[3],anchors[4],anchors[5],anchors[6]};
    float rounding=std::min(100u,unsigned(roundness))*.01f;
    Contour result{};
    result.original=e;result.cx=cx;result.cy=cy;result.scale=scale;result.legacy=roundness==0;
    for(unsigned i=0;i<8;++i){
        Point a=ordered[(i+7)%8],b=ordered[i],c=ordered[(i+1)%8];
        a={(a.x+b.x)*.5f,(a.y+b.y)*.5f};c={(c.x+b.x)*.5f,(c.y+b.y)*.5f};
        for(unsigned j=0;j<4;++j){float t=j*.25f,u=1-t;
            Point sharp=t<.5f?Point{a.x+(b.x-a.x)*2*t,a.y+(b.y-a.y)*2*t}:
                Point{b.x+(c.x-b.x)*(2*t-1),b.y+(c.y-b.y)*(2*t-1)};
            Point curved{u*u*a.x+2*u*t*b.x+t*t*c.x,u*u*a.y+2*u*t*b.y+t*t*c.y};
            result.points[i*4+j]={cx+scale*(sharp.x+(curved.x-sharp.x)*rounding),
                cy+scale*(sharp.y+(curved.y-sharp.y)*rounding)};
        }
    }
    return result;
}
unsigned scanline(const Contour&c,float y,float* crossings){
    if(c.legacy){
        const Eye&e=c.original;float yy=(y-c.cy)/c.scale;
        float hw=e.width*.5f,hh=e.height*.5f;
        if(fabsf(yy)>hh)return 0;
        float radius=fminf(hw,hh)*.85f;
        float dy=fmaxf(fabsf(yy)-(hh-radius),0);
        float extent=hw-radius+sqrtf(fmaxf(0,radius*radius-dy*dy));
        float lo=-extent,hi=extent;
        float lid=-hh+e.lid*e.height;
        if(e.slope>0)hi=fminf(hi,(yy-lid)/e.slope);
        else if(e.slope<0)lo=fmaxf(lo,(yy-lid)/e.slope);
        else if(yy<lid)return 0;
        if(lo>=hi)return 0;
        float bottom=hh+( -hh+14-hh)*e.arch;
        float k=.75f*hh*e.arch/(hw*hw);
        unsigned n=0;
        auto span=[&](float a,float b){if(a<b){crossings[n++]=c.cx+a*c.scale;crossings[n++]=c.cx+b*c.scale;}};
        if(yy<=bottom)span(lo,hi);
        else if(k>0){float cut=sqrtf((yy-bottom)/k);span(lo,fminf(hi,-cut));span(fmaxf(lo,cut),hi);}
        return n;
    }
    unsigned n=0;
    for(unsigned i=0;i<ContourPoints;++i){const auto&a=c.points[i];const auto&b=c.points[(i+1)%ContourPoints];
        // Half-open edges avoid duplicate crossings at shared vertices.
        if((a.y<=y&&b.y>y)||(b.y<=y&&a.y>y))crossings[n++]=a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);
    }
    std::sort(crossings,crossings+n);return n;
}
}
