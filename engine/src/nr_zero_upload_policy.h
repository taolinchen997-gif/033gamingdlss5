#pragma once
namespace nrzero033 {
enum class Result {Ready,Waiting,Failed};
// CPU never waits on the GPU. A recorded upload retains its resources until
// the same observed submission/reset lease used by NR has retired.
class Gate {
    enum class Phase {Empty,Prepared,Recorded};
    Phase phase=Phase::Empty;
    unsigned currentW=0,currentH=0,pendingW=0,pendingH=0;
public:
    template<class Backend> Result Poll(Backend& b,unsigned w,unsigned h) {
        if(!w||!h)return Result::Failed;
        if(phase==Phase::Recorded){
            if(!b.Complete())return Result::Waiting;
            if(w==pendingW&&h==pendingH){b.Publish();currentW=w;currentH=h;}
            else b.Discard();
            phase=Phase::Empty;
        }
        if(phase==Phase::Prepared&&(w!=pendingW||h!=pendingH)){
            b.Discard();phase=Phase::Empty;
        }
        if(currentW==w&&currentH==h)return Result::Ready;
        if(phase==Phase::Empty){
            if(!b.Prepare(w,h)){b.Discard();return Result::Failed;}
            pendingW=w;pendingH=h;phase=Phase::Prepared;
        }
        if(!b.Record())return Result::Waiting;
        phase=Phase::Recorded;
        return Result::Waiting;
    }
};
}
