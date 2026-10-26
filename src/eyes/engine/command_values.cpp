#include "command_values.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
namespace Eyes {
bool parseInteger(const char* s,long& result) {
    char* end=nullptr;errno=0;long v=strtol(s,&end,10);
    if(end==s||*end||errno==ERANGE)return false;
    result=v;return true;
}
bool parseExpression(const char* s,Expression& result) {
    for(unsigned i=0;i<unsigned(Expression::Count);++i)
        if(!strcmp(s,name(Expression(i)))){result=Expression(i);return true;}
    long v;
    if(!parseInteger(s,v)||v<0||v>=long(Expression::Count))return false;
    result=Expression(v);return true;
}
bool parseColor(const char* s,uint16_t& result) {
    struct NamedColor {const char* name;uint32_t rgb;};
    static const NamedColor colors[]={
        {"blue",0x168BFF},{"red",0xFF0000},{"green",0x00FF00},
        {"cyan",0x00FFFF},{"white",0xFFFFFF},{"yellow",0xFFFF00},
        {"orange",0xFF8000},{"purple",0x8000FF},{"pink",0xFF4080},
        {"black",0x000000}
    };
    uint32_t rgb=0;bool found=false;
    for(const auto& c:colors)if(!strcmp(s,c.name)){rgb=c.rgb;found=true;break;}
    if(!found){
        if(strlen(s)!=6)return false;
        for(unsigned i=0;i<6;++i){char c=s[i];unsigned digit;
            if(c>='0'&&c<='9')digit=c-'0';
            else if(c>='a'&&c<='f')digit=c-'a'+10;
            else if(c>='A'&&c<='F')digit=c-'A'+10;
            else return false;
            rgb=(rgb<<4)|digit;
        }
    }
    result=((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x001f);
    return true;
}
}
