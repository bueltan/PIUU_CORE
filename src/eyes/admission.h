#pragma once
#include "control.h"
#include "protocol/session_protocol.h"
namespace EyesControl {
class Admission {
public:
    bool update(SessionProtocol::Identity id) {
        if(SessionProtocol::sameTurn(id,id_) && id.response==id_.response)return false;
        id_=id;haveSequence_=false;return true;
    }
    bool accept(SessionProtocol::Identity id,uint32_t sequence,uint32_t received,uint32_t now,const Command& c) {
        if(!id.server || !SessionProtocol::sameTurn(id,id_) || id.response!=id_.response ||
           uint32_t(now-received)>1000 || (visual(c)&&(!id.turn||!id.response)))return false;
        if(haveSequence_ && int32_t(sequence-sequence_)<=0)return false;
        sequence_=sequence;haveSequence_=true;return true;
    }
private:
    SessionProtocol::Identity id_{};
    bool haveSequence_=false;
    uint32_t sequence_=0;
};
}
