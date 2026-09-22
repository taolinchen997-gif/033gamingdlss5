#pragma once
#include "../src/nr_zero_upload_policy.h"
static void nrZeroUploadChecks(){
    struct Backend {
        bool allocated=true,observed=false,completed=false,inflight=false,unsafe=false;
        int prepares=0,records=0,publishes=0,discards=0;
        bool Prepare(unsigned,unsigned){++prepares;return allocated;}
        bool Record(){if(!observed)return false;++records;inflight=true;return true;}
        bool Complete(){if(completed)inflight=false;return completed;}
        void Publish(){unsafe|=inflight;++publishes;}
        void Discard(){unsafe|=inflight;++discards;}
        // No Wait/Flush/Execute API: production control flow must yield.
    } b;
    using nrzero033::Result;nrzero033::Gate gate;
    check(gate.Poll(b,1920,1080)==Result::Waiting,"unobserved upload yields instead of waiting");
    for(int i=0;i<100;++i)check(gate.Poll(b,1920,1080)==Result::Waiting,"pending registration remains nonblocking");
    check(b.prepares==1&&b.records==0&&b.publishes==0,"registration retries retain one unsubmitted allocation");
    b.observed=true;check(gate.Poll(b,1920,1080)==Result::Waiting,"recording does not prematurely publish textures");
    for(int i=0;i<100;++i)check(gate.Poll(b,5120,2160)==Result::Waiting,"resize cannot release recorded upload before completion");
    check(!b.unsafe&&b.discards==0&&b.prepares==1&&b.records==1,"in-flight resize retains all texture and staging references");
    b.completed=true;
    check(gate.Poll(b,5120,2160)==Result::Waiting,"retired wrong-size upload discarded before replacement");
    check(b.discards==1&&b.prepares==2&&b.records==2&&!b.unsafe,"replacement recorded exactly once after retirement");
    b.completed=false;check(gate.Poll(b,5120,2160)==Result::Waiting,"new upload has independent completion");
    b.completed=true;check(gate.Poll(b,5120,2160)==Result::Ready,"matching completed upload published");
    for(int i=0;i<100;++i)check(gate.Poll(b,5120,2160)==Result::Ready,"stable dimensions never re-upload");
    check(b.prepares==2&&b.records==2&&b.publishes==1&&!b.unsafe,"stationary runtime has no per-frame allocation or wait");
    Backend failure;failure.allocated=false;nrzero033::Gate failed;
    check(failed.Poll(failure,800,600)==Result::Failed&&failure.discards==1&&failure.records==0,"allocation failure records no GPU work");
    Backend resizing;nrzero033::Gate resized;
    resized.Poll(resizing,800,600);resized.Poll(resizing,1920,1080);
    check(resizing.prepares==2&&resizing.discards==1&&!resizing.unsafe,"unsubmitted size change can discard immediately");
    check(resized.Poll(resizing,0,0)==Result::Failed,"zero size never enters initialization");
}
