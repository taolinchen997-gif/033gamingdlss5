#pragma once
#include <cstdint>

namespace paneldraft {
// UI-only state. A fresh requested snapshot is not the active model bank.
// No queues, OS calls, persistence, model ownership, or ImGui dependency.
template<unsigned Count> class Fields {
    struct Field {
        float requested=0,draft=0,submitted=0;
        uint64_t revision=0,submittedRevision=0;
        bool initialized=false,dirty=false,inFlight=false,rejected=false;
    } fields[Count];
public:
    bool Managed(unsigned id)const{return fields[id].initialized;}
    void Observe(unsigned id,float value,bool fresh,unsigned queued){
        auto& f=fields[id];
        if(!f.initialized){f.requested=f.draft=value;f.initialized=true;return;}
        if(!fresh)return;
        f.requested=value;
        // A reused snapshot or an undrained controls queue cannot acknowledge
        // a request which happens to equal an older value.
        // Fresh + drained means the prior local queue was processed. Another
        // accepted/shared edit may already supersede its value. Follow that
        // latest snapshot only when no newer dirty draft needs protection.
        if(f.inFlight && queued==0)f.inFlight=false;
        if(!f.dirty && !f.inFlight && !f.rejected)f.draft=value;
    }
    float Value(unsigned id)const{return fields[id].draft;}
    uint64_t Revision(unsigned id)const{return fields[id].revision;}
    bool NeedsSubmit(unsigned id)const{return fields[id].dirty||fields[id].rejected;}
    bool InFlight(unsigned id)const{return fields[id].inFlight;}
    bool Rejected(unsigned id)const{return fields[id].rejected;}
    void Edit(unsigned id,float value){
        auto& f=fields[id];f.draft=value;f.dirty=true;++f.revision;
    }
    void Accepted(unsigned id,uint64_t revision,float value){
        auto& f=fields[id];
        if(revision>=f.submittedRevision){f.submitted=value;f.submittedRevision=revision;f.inFlight=true;}
        // An older submission result must not clear a newer local edit.
        if(f.revision==revision){f.dirty=false;f.rejected=false;}
    }
    void Failed(unsigned id,uint64_t revision){
        auto& f=fields[id];
        if(revision>=f.submittedRevision)f.rejected=true;
    }
    void Discard(unsigned id){
        auto& f=fields[id];f.draft=f.inFlight?f.submitted:f.requested;
        f.dirty=false;f.rejected=false;++f.revision;
    }
    uint64_t RejectedMask()const{
        uint64_t mask=0;for(unsigned id=0;id<Count;++id)if(fields[id].rejected)mask|=uint64_t(1)<<id;return mask;
    }
};
}
