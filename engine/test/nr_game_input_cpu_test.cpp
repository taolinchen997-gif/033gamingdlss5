#include "nr_game_input_policy.h"
#include "nr_input_policy.h"
#include <cstdio>
#include <limits>
static unsigned checks=0,failed=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failed;printf("FAIL %s\n",name);}}
int main(){
    using namespace nrgame033;
    Frame f;f.source=ReEngineScene;f.flags=DepthReversed|MotionUnjittered|DisplayResolved|FullExtent;
    f.depth=(void*)1;f.motion=(void*)2;f.queue=(void*)3;f.stream=4;f.serial=1;
    f.width=5120;f.height=2160;f.scaleX=2560;f.scaleY=-1080;
    Check(Check(f,5120,2160,true,true,true,0)==Result::Ready,"complete native scene accepted");
    for(unsigned i=0;i<14;++i){auto bad=f;
        switch(i){case 0:bad.depth=nullptr;break;case 1:bad.motion=nullptr;break;case 2:bad.queue=nullptr;break;
        case 3:bad.serial=0;break;case 4:bad.stream=0;break;case 5:bad.version++;break;case 6:bad.size--;break;
        case 7:bad.flags&=~MotionUnjittered;break;case 8:bad.flags&=~FullExtent;break;
        case 9:bad.width=2560;break;case 10:bad.source=ImageEstimated;break;
        case 11:bad.scaleX=0;break;case 12:bad.scaleY=std::numeric_limits<float>::quiet_NaN();break;
        case 13:bad.flags|=JitterKnown;bad.jitterX=std::numeric_limits<float>::infinity();break;}
        Check(nrgame033::Check(bad,5120,2160,true,true,true,0)!=Result::Ready,"reject incomplete or incompatible input");
    }
    Check(nrgame033::Check(f,5120,2160,false,true,true,0)==Result::Device,"device ownership required");
    Check(nrgame033::Check(f,5120,2160,true,false,true,0)==Result::Queue,"same submission queue required");
    Check(nrgame033::Check(f,5120,2160,true,true,false,0)==Result::State,"unknown state never guessed");
    Check(nrgame033::Check(f,5120,2160,true,true,true,1)==Result::Stale,"no reuse of prior frame");
    Frame2 independent;independent.frame=f;
    independent.outputWidth=5120;independent.outputHeight=2160;
    independent.frame.width=2560;independent.frame.height=1080;
    independent.frame.scaleX=1280;independent.frame.scaleY=-540;
    independent.motionWidth=2560;independent.motionHeight=1080;
    Check(nrgame033::Check(independent,5120,2160,true,true,true,0)==Result::Ready,
        "v2 lower-resolution guides with verified output extent accepted");
    Check(nrgame033::Check(independent.frame,5120,2160,true,true,true,0)==Result::Extent,
        "v1 caller retains strict original extent contract");
    Check(ResourceExtents(independent,2560,1080,2560,1080),"actual two resource descriptors match reported extents");
    for(unsigned i=0;i<10;++i){auto bad=independent;
        switch(i){case 0:bad.size--;break;case 1:bad.version++;break;case 2:bad.outputWidth=2560;break;
        case 3:bad.outputHeight=1080;break;case 4:bad.motionWidth=0;break;case 5:bad.motionHeight=8193;break;
        case 6:bad.frame.width=0;break;case 7:bad.frame.height=8193;break;
        case 8:bad.frame.flags&=~FullExtent;break;case 9:bad.frame.source=ImageEstimated;break;}
        Check(nrgame033::Check(bad,5120,2160,true,true,true,0)!=Result::Ready,"v2 geometry does not weaken metadata admission");
    }
    Check(!ResourceExtents(independent,2560,1080,1280,540),"motion resource mismatch rejected");
    Check(!ResourceExtents(independent,5120,2160,2560,1080),"depth resource mismatch rejected");
    Check(nrgame033::Check(independent,5120,2160,false,true,true,0)==Result::Device,"v2 device ownership retained");
    Check(nrgame033::Check(independent,5120,2160,true,false,true,0)==Result::Queue,"v2 queue ownership retained");
    Check(nrgame033::Check(independent,5120,2160,true,true,false,0)==Result::State,"v2 unknown state still rejected");
    Check(nrgame033::Check(independent,5120,2160,true,true,true,1)==Result::Stale,"v2 stale serial still rejected");
    Check(FullTargetRect(0,0,1280,540,1280,540),"full semantic target rectangle accepted");
    Check(!FullTargetRect(1,0,1280,540,1280,540),"cropped semantic target rejected");
    Check(!FullTargetRect(0,0,1279,540,1280,540),"partial semantic target rejected");
    Check(!FullTargetRect(0,0,std::numeric_limits<float>::quiet_NaN(),540,1280,540),"NaN target rectangle rejected");
    Check(!FullTargetRect(0,0,8193,540,8193,540),"unsupported semantic target extent rejected");
    auto separate=independent;separate.motionWidth=1280;separate.motionHeight=540;
    Check(nrgame033::Check(separate,5120,2160,true,true,true,0)==Result::MotionExtent,
        "different depth/motion normalization remains explicitly unsupported");
    Check(ResourceExtents(separate,2560,1080,1280,540),"unsupported distinct metadata remains truthful");
    const auto actualGuides=GuideRects(separate.frame,separate.motionWidth,separate.motionHeight);
    Check(actualGuides.depth.width==2560&&actualGuides.depth.height==1080&&
        actualGuides.motion.width==1280&&actualGuides.motion.height==540&&!actualGuides.offset(),
        "production Stage guide packing preserves both true extents");
    struct Match{uintptr_t module=0,address=0;bool ambiguous=false;};
    InputExports selected;
    Check(SelectInputExports(Match{10,11},Match{10,12},Match{10,13},selected)&&selected.acquire2==12,
        "same-owner v2 and descriptor publish together");
    Check(!SelectInputExports(Match{10,11},Match{10,12},Match{0,0,true},selected)&&
        !selected.acquire&&!selected.acquire2&&!selected.describe,
        "late ambiguous descriptor cannot leave a callable v2 pointer");
    Check(!SelectInputExports(Match{10,11},Match{10,12},Match{20,13},selected)&&!selected.acquire2,
        "foreign descriptor cannot publish v2");
    Check(!SelectInputExports(Match{10,11},Match{20,12},Match{10,13},selected)&&!selected.acquire,
        "foreign v2 owner rejected");
    Check(!SelectInputExports(Match{},Match{10,12},Match{10,13},selected)&&!selected.acquire2,
        "v2 without the original exact-owner anchor is rejected");
    Check(SelectInputExports(Match{10,11},Match{},Match{},selected)&&selected.acquire==11&&!selected.acquire2,
        "legacy exact-owner acquire remains supported without optional exports");
    StateBook book;book.Watch(10,50);book.Watch(20,50);uint32_t state=0;
    Check(!book.Read(10,state),"unseen resource unknown");
    book.Barrier(100,10,0x40);book.Barrier(200,10,0x20);
    Check(!book.Read(10,state),"recording alone does not publish state");
    book.Submit(200,50);Check(book.Read(10,state)&&state==0x20,"second recorded list submitted first");
    book.Submit(100,50);Check(book.Read(10,state)&&state==0x40,"submission order decides final state");
    book.Reset(100);book.Barrier(100,10,0x4);book.Reset(100);book.Submit(100,50);
    Check(book.Read(10,state)&&state==0x40,"unsubmitted reset discards barriers");
    book.Barrier(300,10,0x8);book.Submit(300,60);Check(!book.Read(10,state),"foreign queue poisons certainty");
    book.Submit(200,50);Check(book.Read(10,state)&&state==0x20,"known selected-queue barrier recovers");
    book.Forget(10);book.Watch(10,50);book.Submit(200,50);
    Check(!book.Read(10,state),"address reuse cannot resurrect old generation");
    book.Barrier(400,20,0);book.Submit(400,50);Check(!book.Read(20,state),"undefined is not COMMON");
    book.Reset(400);book.Barrier(400,20,0x80000000u);book.Submit(400,50);
    Check(book.Read(20,state)&&state==0,"explicit GENERAL maps to COMMON");
    book.Watch(30,51);Check(!book.Read(20,state),"queue change invalidates state");
    for(uintptr_t i=1;i<=300;++i)book.Barrier(1000+i,30,0x40);
    Check(!book.Read(30,state),"bounded recording overflow cannot reuse stale state");
    for(uint32_t u:{0u,0x80000004u,0xC00u,0x14u,0x4000u})
        Check(!StateBook::State(u,state),"reject undefined or contradictory state");
    {nrinput033::PresentScope placeholder;
        Check(!nrinput033::HasNativeGuide(true)&&nrinput033::Reset(0)==1,"placeholder never enables temporal history");
        {nrinput033::PresentScope native(true,0x20,0x40);
            Check(nrinput033::HasNativeGuide(true)&&!nrinput033::NeedsGameState(),"real guides preserve presentation binding ownership");
            Check(nrinput033::Reset(0)==0&&nrinput033::Reset(1)==1,"native guide path preserves explicit reset");
            Check(nrinput033::context.depthState==0x20&&nrinput033::context.motionState==0x40,"exact arrival states carried");
        }
        Check(nrinput033::Reset(0)==1&&!nrinput033::context.nativeGuides,"nested scope restores fallback contract");
    }
    Check(!nrinput033::context.presentation&&nrinput033::HasNativeGuide(true),"upscale route unaffected by presentation adapter");
    Check(!NeedsReadTransition(0x40u),"exact compute-read state needs no transition");
    for(uint32_t u:{0u,0x20u,0x60u,0xc0u,0x10u,0x80u})
        Check(NeedsReadTransition(u),"COMMON and combined reads normalized to exact host read state");
    StateBook split;split.Watch(10,50);split.Barrier(100,10,0x40,1);split.Submit(100,50);
    Check(!split.Read(10,state),"BEGIN_ONLY never declares a readable resource");
    split.Reset(100);split.Barrier(100,10,0x40,2);split.Submit(100,50);
    Check(split.Read(10,state)&&state==0x40,"END_ONLY publishes completed transition");
    split.Reset(100);split.Barrier(100,10,0);split.Submit(100,50);
    Check(!split.Read(10,state),"aliasing does not imply COMMON");
    split.Reset(100);split.Barrier(100,10,0x40,3);split.Submit(100,50);
    Check(!split.Read(10,state),"unsupported split combination or subresource rejected");
    printf("Native input CPU checks=%u failures=%u; no graphics device created\n",checks,failed);
    return failed?1:0;
}
