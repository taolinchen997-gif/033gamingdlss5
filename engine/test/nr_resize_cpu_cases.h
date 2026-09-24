#pragma once
#include "../src/nr_resize_policy.h"
static void nrResizeChecks() {
    using namespace nrresize033;
    Geometry old{reinterpret_cast<void*>(1),800,600,800,600,28};
    Geometry next{old.device,5120,2160,5120,2160,28};
    check(DrainBeforeBuild(true,true,old,next,true) && DrainBeforeBuild(true,true,old,next,false),"presentation resize must retire the old model before creation");
    check(!DrainBeforeBuild(false,true,old,next,true),"upscale route keeps the overlap when the new model fits beside the old one");
    check(DrainBeforeBuild(false,true,old,next,false),"upscale route retires an old-size model that blocks the new one");
    check(!DrainBeforeBuild(true,false,old,next,false) && !DrainBeforeBuild(false,false,old,next,false),"first creation has no old model to retire");
    check(!DrainBeforeBuild(true,true,next,next,false) && !DrainBeforeBuild(false,true,next,next,false),"same-size tuning keeps the running model even when memory is short");
    for(int field=0;field<6;++field){auto changed=old;
        switch(field){case 0:changed.device=reinterpret_cast<void*>(2);break;case 1:++changed.width;break;
        case 2:++changed.height;break;case 3:++changed.guideWidth;break;case 4:++changed.guideHeight;break;case 5:changed.format=10;break;}
        check(DrainBeforeBuild(true,true,old,changed,true),"each geometry dimension participates in retirement");
        check(DrainBeforeBuild(false,true,old,changed,false) && !DrainBeforeBuild(false,true,old,changed,true),"each geometry dimension participates on the upscale route");
    }
    // YanYun S33 log, 13:24:00: game default (DLAA, guides 5120x2160) -> L (Balanced, guides 3011x1270),
    // three layers, 17068 MiB used of an 18323 MiB budget. The new model was refused for 57 s.
    const uint64_t MiB=uint64_t(1)<<20;
    const Geometry dlaa{old.device,5120,2160,5120,2160,28},balanced{old.device,5120,2160,3011,1270,28};
    const uint64_t bytes=CreationBytes(5120,2160,3011,1270,3);
    check(bytes==uint64_t(5120)*2160*448+uint64_t(10240)*4320*16,"creation estimate keeps the production bound");
    check(CreationBytes(5120,2160,3011,1270,2)==uint64_t(5120)*2160*320,"two layers add no third-input bound");
    check(CreationBytes(64,64,0,0,0)==uint64_t(64)*64*192,"at least one layer is counted");
    check(!OverlapFits(17068*MiB,18323*MiB,bytes),"logged incident: the new model did not fit beside the old one");
    check(DrainBeforeBuild(false,true,dlaa,balanced,OverlapFits(17068*MiB,18323*MiB,bytes)),"logged incident: the old-size model is retired first");
    check(OverlapFits(14927*MiB,23370*MiB,bytes),"the full budget of 13:24:57 keeps the overlap");
    check(OverlapFits(1,0,bytes),"an unknown budget never blocks");
    check(!OverlapFits(18323*MiB,18323*MiB,1) && OverlapFits(0,2,2) && !OverlapFits(0,2,3),"budget edges");
    {
        int retires=0,builds=0;bool parked=false;
        auto pump=[&]{if(Prepare(DrainBeforeBuild(false,true,old,next,true),[&]{++retires;parked=true;},[]{},[&]{return parked;}))++builds;};
        pump();check(retires==0 && builds==1,"upscale overlap builds beside the running model without retiring it");
    }
    for(int route=0;route<2;++route){
        const bool presentation=route==0; // The upscale route drains here because the new model does not fit.
        bool active=true,parked=false,submitted=false,discarded=false;uint64_t completed=0,target=9;int retires=0,builds=0;
        auto retire=[&]{active=false;parked=true;++retires;};
        auto collect=[&]{if(submitted && discarded && completed!=UINT64_MAX && completed>=target)parked=false;};
        auto pending=[&]{return parked;};
        auto pump=[&]{collect();if(Prepare(DrainBeforeBuild(presentation,active,old,next,presentation),retire,collect,pending))++builds;};
        pump();check(retires==1 && builds==0,"old bank is retired once; unsubmitted work blocks new model");
        submitted=true;completed=9;pump();check(builds==0,"GPU completion alone does not release replayable command references");
        discarded=true;completed=8;pump();check(builds==0,"list retirement alone cannot release in-flight GPU work");
        completed=UINT64_MAX;pump();check(builds==0,"device removal never counts as successful GPU completion");
        completed=9;pump();check(builds==1 && retires==1 && !parked,"new model starts only after old bank is safely retired");
    }
}
