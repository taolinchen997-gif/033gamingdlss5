#pragma once
#include "../src/nr_resize_policy.h"
static void nrResizeChecks() {
    using namespace nrresize033;
    Geometry old{reinterpret_cast<void*>(1),800,600,800,600,28};
    Geometry next{old.device,5120,2160,5120,2160,28};
    check(DrainBeforeBuild(true,true,old,next),"presentation resize must retire the old model before creation");
    check(!DrainBeforeBuild(false,true,old,next),"native input model switching behavior is unchanged");
    check(!DrainBeforeBuild(true,false,old,next),"first creation has no old model to retire");
    check(!DrainBeforeBuild(true,true,next,next),"same-size tuning can retain the current overlap path");
    for(int field=0;field<6;++field){auto changed=old;
        switch(field){case 0:changed.device=reinterpret_cast<void*>(2);break;case 1:++changed.width;break;
        case 2:++changed.height;break;case 3:++changed.guideWidth;break;case 4:++changed.guideHeight;break;case 5:changed.format=10;break;}
        check(DrainBeforeBuild(true,true,old,changed),"each geometry dimension participates in retirement");
    }
    bool active=true,parked=false,submitted=false,discarded=false;uint64_t completed=0,target=9;int retires=0,builds=0;
    auto retire=[&]{active=false;parked=true;++retires;};
    auto collect=[&]{if(submitted && discarded && completed!=UINT64_MAX && completed>=target)parked=false;};
    auto pending=[&]{return parked;};
    auto pump=[&]{collect();if(Prepare(DrainBeforeBuild(true,active,old,next),retire,collect,pending))++builds;};
    pump();check(retires==1 && builds==0,"old bank is retired once; unsubmitted work blocks new model");
    submitted=true;completed=9;pump();check(builds==0,"GPU completion alone does not release replayable command references");
    discarded=true;completed=8;pump();check(builds==0,"list retirement alone cannot release in-flight GPU work");
    completed=UINT64_MAX;pump();check(builds==0,"device removal never counts as successful GPU completion");
    completed=9;pump();check(builds==1 && retires==1 && !parked,"new model starts only after old bank is safely retired");
}
