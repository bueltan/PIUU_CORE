#include <initializer_list>
#include "renderer.h"
#include <assert.h>
using namespace Eyes;
int main(){
    static TFT_eSPI display;
    Renderer renderer(display); Config c;
    static_assert(sizeof(Renderer)+sizeof(Animator)+sizeof(Config)+2*sizeof(Pose)<4096,"Eye state budget exceeded");
    auto draw=[&](Pose p){renderer.begin(p,c);unsigned steps=0;while(renderer.busy()){renderer.step();assert(++steps<=120);}};
    draw(preset(Expression::Rest));
    unsigned lit=0;for(auto pixel:display.screen)lit+=pixel!=0;assert(lit>0);
    assert(display.transferred==320*240); // First presentation clears the previous UI.
    unsigned initial=display.transferred;
    draw(preset(Expression::Rest));
    assert(display.transferred-initial<16000); // Later frames only transfer dirty eyes.
    for(auto& pixel:display.screen)pixel=0x7777; // Simulate legacy UI drawing.
    renderer.invalidate();
    initial=display.transferred;draw(preset(Expression::Rest));
    assert(display.transferred-initial==320*240);
    assert(display.screen[0]==0);
    c.sizePercent=25;draw(preset(Expression::Rest));
    unsigned small=0;for(auto pixel:display.screen)small+=pixel!=0;assert(small<lit);
    c.sizePercent=200;draw(preset(Expression::Rest));
    unsigned large=0;for(auto pixel:display.screen)large+=pixel!=0;assert(large>lit);
    c.sizePercent=25;draw(preset(Expression::Rest));
    unsigned shrunk=0;for(auto pixel:display.screen)shrunk+=pixel!=0;assert(shrunk==small);
    c.sizePercent=200;
    c.offsetX=160;c.offsetY=120;c.separation=260;draw(preset(Expression::Surprised));
    c.offsetX=-160;c.offsetY=-120;c.separation=0;draw(preset(Expression::Happy));
    for(unsigned i=0;i<unsigned(Expression::Count);++i){
        c.offsetX=0;c.offsetY=0;c.separation=104;c.sizePercent=100;
        draw(preset(Expression(i)));
        // Compare incremental rendering to a clean full-screen scanline oracle.
        auto p=preset(Expression(i));Contour contours[2]={makeContour(p.left,160-52+p.gazeX,120+p.gazeY,1,c.roundness),makeContour(p.right,160+52+p.gazeX,120+p.gazeY,1,c.roundness)};
        for(int y=0;y<240;++y){bool expected[320]{};float crossings[ContourPoints];
            for(auto& contour:contours){unsigned n=scanline(contour,y+.5f,crossings);assert(n%2==0);
                for(unsigned k=0;k+1<n;k+=2)for(int x=0;x<320;++x)if(x+.5f>=crossings[k]&&x+.5f<crossings[k+1])expected[x]=true;
            }
            for(int x=0;x<320;++x)assert(display.screen[y*320+x]==(expected[x]?c.color:0));
        }
    }
    // Intermediate poses must keep finite, paired scanline intersections.
    for(unsigned a=0;a<unsigned(Expression::Count);++a)for(unsigned b=0;b<unsigned(Expression::Count);++b)
        for(unsigned t=1;t<4;++t){auto p=blend(preset(Expression(a)),preset(Expression(b)),t*.25f);
            auto contour=makeContour(p.left,160,120,2);float xs[ContourPoints];
            unsigned filled=0;
            for(int y=0;y<240;++y){unsigned n=scanline(contour,y+.5f,xs);assert(n%2==0);
                for(unsigned k=0;k<n;++k){assert(isfinite(xs[k]));}
                filled+=n;
            }assert(filled>0);
        }
    c.offsetX=0;c.offsetY=0;c.separation=104;c.color=0;draw(preset(Expression::Rest));
    for(auto pixel:display.screen)assert(pixel==0);
    c.backgroundColor=0x1234;c.color=0x1234;
    unsigned before=display.transferred;draw(preset(Expression::Happy));
    assert(display.transferred-before==320*240);
    for(auto pixel:display.screen)assert(pixel==0x1234);
    before=display.transferred;draw(preset(Expression::Rest));
    assert(display.transferred-before<320*240);
    c.color=0xffff;
    for(unsigned roundness: {0u,25u,50u,100u}){
        c.roundness=roundness;draw(preset(Expression::Happy));
        auto p=preset(Expression::Happy);
        auto contour=makeContour(p.left,108,120,1,c.roundness);float xs[ContourPoints];
        for(int y=0;y<240;++y){unsigned n=scanline(contour,y+.5f,xs);assert(n%2==0);}
    }
    c.backgroundColor=0;c.color=0;draw(preset(Expression::Rest));
    for(auto pixel:display.screen)assert(pixel==0); // Old geometry erased across layout changes.
}
