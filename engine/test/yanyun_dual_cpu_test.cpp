#define NOMINMAX
#include "../src/yanyun_dual_policy.h"
#include "../../semantic/third_party/DXL/SemanticMask.h"
#include <windows.h>
#include "../src/hang_watchdog.h"
#include "../../semantic/worker_protocol.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>
#include <stdexcept>
#include <string>
static unsigned checks=0;
static void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
int main(){try{
 {// S25 hang watchdog: dump only for a real hang, never a pause, a minimise or a load screen.
  using namespace hangwatch033;const ULONGLONG t0=100000;
  Check(!ShouldDump(t0+60000,0,true,true,false,true,false),"no present yet: never");
  Check(!ShouldDump(t0+StallMs-1,t0,true,true,false,true,false)&&ShouldDump(t0+StallMs,t0,true,true,false,true,false),"six seconds without a present and a hung window");
  Check(!ShouldDump(t0+60000,t0,true,true,false,false,false),"window still answering (loading, paused): never");
  Check(!ShouldDump(t0+60000,t0,true,true,true,true,false)&&!ShouldDump(t0+60000,t0,true,false,false,true,false),"minimised or hidden: never");
  Check(!ShouldDump(t0+60000,t0,false,true,false,true,false),"unknown window: never");
  Check(!ShouldDump(t0+60000,t0,true,true,false,true,true),"one dump per session");
  Check(StallMs>=5000,"never before Windows' own five-second not-responding rule");
 }
 using namespace yanyundual;
 FrameIdentity captured{101,7,30,1000,2560,1440},current{101,7,34,1060,2560,1440};
 // S18 continuity: identity stays hard; age only scales the effect.
 Check(Fresh(captured,current,1100),"S17 100ms boundary still admitted");
 Check(Fresh(captured,current,1101)&&Fresh(captured,current,1140),"S17 game-measured 101-140ms masks no longer blank the whole frame");
 // S27b: age is judged in frames; only a stalled stream is refused by time.
 Check(Fresh(captured,current,1000+MaskFadeEndMs)&&Fresh(captured,current,1000+MaskHardLimitMs-1)&&!Fresh(captured,current,1000+MaskHardLimitMs),"a 600 ms old mask four frames back is still used; a stalled stream is refused after MaskHardLimitMs");
 Check(!Fresh(captured,current,999),"future clock excluded");
 for(int field=0;field<5;++field){auto mismatch=current;switch(field){case 0:++mismatch.stream;break;case 1:++mismatch.generation;break;case 2:--mismatch.width;break;case 3:--mismatch.height;break;case 4:mismatch.frame=29;break;}
  Check(!Fresh(captured,mismatch,1050),"foreign stream, reset, resize or future frame excluded");}
 Check(!Fresh({},current,1050),"empty recognition excluded");
 Check(MaskStatus(captured,current,1156)==MaskReason::Ready,"actual S6 156ms delay is now shown at full weight");
 {auto late=current;late.frame=captured.frame+MaskFadeEndFrames-1;Check(MaskStatus(captured,late,1100)==MaskReason::Ready,"23 frames old: still followed along the GPU chain");
  late.frame=captured.frame+MaskFadeEndFrames;Check(MaskStatus(captured,late,1100)==MaskReason::Age,"24 frames old: refused where the GPU chain ends");}
 Check(MaskFrameWeight(0)==1.f&&MaskFrameWeight(MaskFullWeightFrames)==1.f&&MaskFrameWeight(MaskFadeEndFrames)==0.f&&MaskFrameWeight(UINT64_MAX)==0.f,"frame weight: full while the chain follows comfortably, zero at its end");
 Check(std::abs(MaskFrameWeight((MaskFullWeightFrames+MaskFadeEndFrames)/2)-.5f)<1e-6f,"frame fade midpoint");
 {float last=2.f;bool monotone=true;for(uint64_t f=0;f<=MaskFadeEndFrames+4;++f){const float w=MaskFrameWeight(f);if(w>last||w<0.f||w>1.f)monotone=false;last=w;}Check(monotone,"frame weight is monotone and bounded");}
 {// Replay of the S27 session: a recognition every ~4-5 frames, 250-600 ms old.
  float shown=0.f,shownMs=0.f;unsigned dim=0,dimMs=0;
  for(unsigned i=0;i<4096;++i){const uint64_t framesOld=1+(i%5),ms=250+(i*37)%350;
   shown=ShownMaskWeight(shown,45,MaskFrameWeight(framesOld));shownMs=ShownMaskWeight(shownMs,45,MaskAgeWeight(ms));
   if(i>20&&shown<1.f)++dim;if(i>20&&shownMs<1.f)++dimMs;}
  Check(dim==0&&dimMs>2000,"S27 session replay: the frame rule stays at full weight where the millisecond rule dimmed most frames");}
 auto resetFrame=current;resetFrame.generation++;Check(MaskStatus(captured,resetFrame,1100)==MaskReason::Generation,"generation rejection remains distinct");
 Check(MaskAgeWeight(0)==1.f&&MaskAgeWeight(MaskFullWeightMs)==1.f,"full weight while young");
 Check(MaskAgeWeight(MaskFadeEndMs)==0.f&&MaskAgeWeight(UINT64_MAX)==0.f,"zero weight at/after fade end");
 {float last=2.f;bool monotone=true;for(uint64_t a=0;a<=MaskFadeEndMs+50;++a){const float w=MaskAgeWeight(a);if(w>last||w<0.f||w>1.f)monotone=false;last=w;}Check(monotone,"age weight is monotone and bounded");}
 Check(std::abs(MaskAgeWeight((MaskFullWeightMs+MaskFadeEndMs)/2)-.5f)<.01f,"linear fade midpoint");
 {float shown=0.f;const uint64_t frameMs=45;unsigned frames=0;while(shown<1.f&&frames<100){shown=ShownMaskWeight(shown,frameMs,1.f);++frames;}
  Check(MaskFadeInMs==800&&frames==(MaskFadeInMs+44)/45,"S30: fade-in 0->1 takes MaskFadeInMs (800 ms, 18 frames at 22 fps), never a pop");}
 Check(ShownMaskWeight(0.f,100000,1.f)<=float(MaskFadeStepCapMs)/MaskFadeInMs+1e-6f,"a long pause cannot jump the weight");
 Check(ShownMaskWeight(1.f,45,.3f)==.3f,"a falling age ceiling applies at once");
 Check(ShownMaskWeight(1.f,45,0.f)==0.f&&ShownMaskWeight(.5f,45,-1.f)==0.f,"no mask => zero weight");
 {// Replay of the S17 game distribution: ages 78-140ms at 45ms frames never blank a frame once faded in.
  float shown=0.f;unsigned blank=0;for(unsigned i=0;i<4096;++i){const uint64_t age=78+(i*37)%63;shown=ShownMaskWeight(shown,45,MaskAgeWeight(age));if(i>20&&shown<1.f)++blank;}
  Check(blank==0,"S17 age range stays at full weight after fade-in");}
 {// S22 motion chain: the same object's velocity in consecutive frames stays within 1.5 texels + half its speed.
  using namespace latemask;
  Check(ChainConsistent(0,0,0,0)&&ChainConsistent(5,0,5,0)&&ChainConsistent(8,0,5,0),"same or smoothly changing velocity continues the chain");
  Check(ChainConsistent(.6f,-.4f,-.5f,.5f)&&ChainConsistent(0,0,3,0)&&!ChainConsistent(0,0,3.1f,0),"standing object: small jitter tolerated; a mover faster than 3 texels/frame is another object");
  Check(!ChainConsistent(0,0,5,0)&&!ChainConsistent(5,0,0,0),"person running across still background: uncovered background is rejected either way");
  Check(!ChainConsistent(3.1f,0,-3.1f,0)&&ChainConsistent(3,0,0,0)&&!ChainConsistent(3.1f,0,0,0),"direction reversal and the relative bound");
  Check(!ChainConsistent(std::numeric_limits<float>::quiet_NaN(),0,0,0)&&!ChainConsistent(std::numeric_limits<float>::infinity(),0,0,0),"invalid motion never continues a chain");
  Check(MaxFrames<MotionSlots&&MotionSlots==32&&MaxFrames==24,"every latched mask's motion chain (up to MaxFrames+1 frames) fits the ring (S27: 32/24)");
  Check(ChainConsistent(0,0,2,0)&&!SameSurface(.03f,.005f)&&!SameSurface(.005f,.03f),"slow walk past a still background: velocity alone accepts, depth rejects");
  Check(SameSurface(.03f,.028f)&&SameSurface(.03f,.0333f),"same surface moving toward/away from the camera (<=15%) continues");
  Check(!SameSurface(.03f,.024f)&&!SameSurface(.03f,.039f),"a surface 25-30% nearer or farther is another object (person against the wall behind)");
  Check(SameSurface(-1.f,.03f)&&SameSurface(.03f,-1.f)&&SameSurface(-1.f,-1.f),"unknown depth never rejects");
  Check(SameSurface(0.f,.00005f)&&SameSurface(.00001f,0.f),"sky/far background (q near 0) compares by the floor, no division");
  Check(ChainContinues(40,0,0,0,.01f,.01f)&&ChainContinues(0,0,40,0,.03f,.03f),"with depth: a sudden stop or start (velocity 40 -> 0) keeps the same body");
  Check(!ChainContinues(2,0,0,0,.03f,.005f),"with depth: slow walk past the background is still rejected");
  Check(!ChainContinues(40,0,0,0,-1.f,.01f)&&ChainContinues(2,0,0,0,-1.f,-1.f),"without depth: the velocity rule decides");
  Check(IndexSlot(EncodeIndex(5,3))==3&&IndexSeq(EncodeIndex(5,3))==5&&EncodeIndex(1,0)!=0,"ring index: slot and sequence, never 0 once published");
  for(unsigned slot=0;slot<Slots;++slot)Check(IndexSlot(EncodeIndex((1u<<29)-1,slot))==slot,"largest sequence keeps the slot");
  Check(MaskHeight(3184,2232)==449&&MaskHeight(5120,2160)==270&&MaskHeight(1080,1920)==1138&&MaskHeight(0,10)==0,"mask grid equals the S19 composite grid");
  Check(MaskHeight(1080,1920)<=MaxMaskHeight,"portrait 1080x1920 fits the ring");
  Check(RingBytes<16ull*1024*1024,"ring stays a few MiB of upload memory");
  Check(FeatherRadius(6.784f)==7&&std::abs(FeatherSigma(6.784f)-3.392f)<1e-5f&&FeatherRadius(0.f)==0&&FeatherSigma(0.f)==.35f,"feather kernel equals DXL ComposeSemanticMask");
  Check(sizeof(LateConstants)==64,"16 late root constants");
 }
 {// S21 three-vote mask and worker dropout hold.
  using namespace latemask;
  Check(Votes==3&&Votes<Slots,"three votes fit the eight-slot ring (the worker overwrites only the slot seven behind)");
  Check(Median3(1,0,1)==1&&Median3(0,1,1)==1&&Median3(1,1,0)==1,"one missing recognition is out-voted");
  Check(Median3(1,0,0)==0&&Median3(0,0,1)==0&&Median3(0,1,0)==0,"one spurious recognition is out-voted");
  Check(Median3(.8f,.6f,.2f)==.6f&&Median3(.2f,.8f,.6f)==.6f,"soft edges take the middle value, no invented strength");
  Check(VoteValue(3,0,1,1)==1&&VoteValue(2,0,1,1)==0&&VoteValue(1,.4f,1,1)==.4f&&VoteValue(0,1,1,1)==0,"fewer than three: the newest exactly as S20; none: nothing");
  {bool bounded=true;for(int a=0;a<=4;++a)for(int b=0;b<=4;++b)for(int c=0;c<=4;++c){const float m=Median3(a/4.f,b/4.f,c/4.f);
    bounded&=m>=(std::min)({a,b,c})/4.f&&m<=(std::max)({a,b,c})/4.f;}Check(bounded,"median stays within the three inputs");}
  Check(HoldAsDropout(0.f,.05f,0)&&HoldAsDropout(.019f,.05f,1),"collapsed coverage is held (first and second in a row)");
  // S25: six in a row (~250 ms), still inside the motion chain the GPU follows.
  Check(MaxHeldRecognitions==6&&HoldAsDropout(0.f,.05f,2)&&HoldAsDropout(0.f,.05f,5),"S25 keeps holding a collapsed person through the sixth recognition");
  Check(!HoldAsDropout(0.f,.05f,MaxHeldRecognitions),"at most six held in a row: the seventh is published");
  Check(MaxHeldRecognitions+Votes<=MaxFrames&&MaxFrames<MotionSlots,"held + voted masks stay within the recorded motion chain");
  Check(DropoutRunBucket(1)==0&&DropoutRunBucket(2)==0&&DropoutRunBucket(3)==1&&DropoutRunBucket(4)==1&&DropoutRunBucket(5)==2&&DropoutRunBucket(6)==2,"dropout run telemetry buckets");
  Check(!HoldAsDropout(.021f,.05f,0)&&!HoldAsDropout(.04f,.05f,0),"a drop to 40% or more is a real change, never held");
  Check(!HoldAsDropout(0.f,MinReferenceCoverage*.99f,0)&&!HoldAsDropout(0.f,0.f,0),"no hold without a real previous person");
  Check(!HoldAsDropout(std::numeric_limits<float>::quiet_NaN(),.05f,0)&&!HoldAsDropout(0.f,std::numeric_limits<float>::infinity(),0),"invalid coverage never held");
  // S27 protagonist lock: a locked protagonist the worker reports missing is held
  // up to 18 in a row whatever the coverage; other collapses keep the six.
  Check(MaxProtagonistHeld==18&&MaxProtagonistHeld+Votes+1<=MaxFrames&&MaxFrames<MotionSlots,"protagonist hold (+votes, +1 frame of lag) stays within the recorded motion chain");
  Check(HoldDecision(.05f,.05f,0,true,0)==HoldKind::Protagonist,"a missing protagonist is held even when other people keep the coverage up");
  Check(HoldDecision(0.f,.05f,6,true,0)==HoldKind::Protagonist&&HoldDecision(0.f,.05f,17,true,0)==HoldKind::Protagonist,"held beyond the coverage rule's six, through the eighteenth");
  Check(HoldDecision(0.f,.05f,18,true,0)==HoldKind::None,"the nineteenth is published");
  Check(HoldDecision(0.f,.05f,0,false,0)==HoldKind::Dropout&&HoldDecision(0.f,.05f,5,false,0)==HoldKind::Dropout&&HoldDecision(0.f,.05f,6,false,0)==HoldKind::None,"without the protagonist flag the S25 coverage rule is unchanged");
  Check(HoldDecision(.05f,.05f,0,false,0)==HoldKind::None&&HoldDecision(std::numeric_limits<float>::quiet_NaN(),.05f,0,false,0)==HoldKind::None,"steady or invalid coverage without a protagonist loss is published");
  // S27b: no hold once the last published mask would leave the GPU chain.
  Check(MaxHeldFrames+Votes+1==MaxFrames&&HoldDecision(0.f,.05f,0,true,MaxHeldFrames)==HoldKind::None&&HoldDecision(0.f,.05f,0,false,MaxHeldFrames)==HoldKind::None,"no hold at the frame cap, protagonist or dropout");
  Check(HoldDecision(0.f,.05f,3,true,MaxHeldFrames-1)==HoldKind::Protagonist&&HoldDecision(0.f,.05f,0,false,MaxHeldFrames-1)==HoldKind::Dropout,"just inside the frame cap both holds still apply");
  Check(ProtagonistRunBucket(1)==0&&ProtagonistRunBucket(2)==0&&ProtagonistRunBucket(3)==1&&ProtagonistRunBucket(6)==1&&ProtagonistRunBucket(7)==2&&ProtagonistRunBucket(12)==2&&ProtagonistRunBucket(13)==3&&ProtagonistRunBucket(18)==3,"protagonist hold run buckets");
  {using namespace yyworker::protagonist;
   Check(Flags(2u)==0&&Flags(2u|((Locked|Missing)<<Shift))==(Locked|Missing)&&Flags(0xFFFFFFFFu)==0xFFu,"worker flags ride above the provider byte; an older worker reports none");
   Check(MissingOnTrack(Locked|Missing)&&!MissingOnTrack(Missing)&&!MissingOnTrack(Locked|Seen)&&!MissingOnTrack(Locked|Seen|Rescued),"only a locked, missing protagonist asks for the long hold");}
  Check(CoverageChangeBucket(.01f,.05f)==0&&CoverageChangeBucket(.025f,.05f)==1&&CoverageChangeBucket(.05f,.05f)==2&&CoverageChangeBucket(.1f,.05f)==3&&CoverageChangeBucket(.2f,.05f)==4,"coverage change buckets");
  Check(CoverageChangeBucket(.05f,0.f)==2,"no reference: counted as steady");
  Check(LatchedBytes==256&&word::RemovedPx*4+4<=LatchedBytes&&word::Stamp==13,"latched words fit the 256-byte raw buffer");
  Check(word::Count==0,"blend reads word 0: zero latched masks keeps the original picture");
  Check(ReadbackLag<ReadbackSlots&&ReadbackLag>=4,"telemetry is read well after the frame executed, before its slot is reused");
  Check(ClassifyVote(0,0,0,0)==VoteKind::None&&ClassifyVote(5000,5000,0,0)==VoteKind::Steady,"no person / identical");
  Check(ClassifyVote(0,5000,5000,0)==VoteKind::Dropout&&ClassifyVote(3000,5000,2000,0)==VoteKind::Dropout,"whole or large part restored by the vote");
  Check(ClassifyVote(9000,5000,0,4000)==VoteKind::Extra&&ClassifyVote(5000,5000,40,40)==VoteKind::Edge,"spurious area removed / edge-only corrections");
  Check(ClassifyVote(5000,5000,8,7)==VoteKind::Steady&&ClassifyVote(100,120,63,0)==VoteKind::Edge,"tiny corrections are steady; small absolute changes stay edge");
 }
 {// S22 fast publish: identical bytes to DXL ComposeSemanticMask (feather 0) when the recognition grid is the late grid.
  using namespace latemask;
  const unsigned mh=MaskHeight(3184,2232);const auto grid=DetectorSize(3184,2232);
  Check(grid.w==MaskWidth&&grid.h==mh,"landscape recognition grid equals the late-mask grid (fast path is the normal case)");
  std::mt19937 rng(20260921);DXL::SemanticMaskSnapshot m;m.width=MaskWidth;m.height=mh;m.version=1;m.publishedMs=1;
  const size_t n=size_t(MaskWidth)*mh;m.coverage.resize(n);m.groupIds.resize(n);
  for(size_t i=0;i<n;++i){m.coverage[i]=uint8_t(rng()&255);const unsigned g=rng()%4;m.groupIds[i]=uint8_t(g==0||g==1?0:g==2?255:1+rng()%11);}
  for(unsigned c=0;c<256;++c){m.coverage[c]=uint8_t(c);m.groupIds[c]=0;} // every coverage value on a person texel
  std::vector<uint8_t> a(n*4,77),b(n*4,99);float strengths[DXL::SEM_GROUP_COUNT]{};strengths[0]=1;
  DXL::ComposeSemanticMask(a.data(),MaskWidth*4,MaskWidth,mh,&m,0,strengths,1,0.f);
  const uint64_t sum=DirectPersonStrength(m.coverage.data(),m.groupIds.data(),n,b.data());
  Check(a==b,"fast publish writes exactly DXL's bytes (all four channels, all coverage values, person/other/no group)");
  uint64_t expect=0;for(size_t i=0;i<n;++i)expect+=a[i*4];Check(sum==expect,"fast publish returns the exact strength sum used for the dropout hold");
  Check(DetectorSize(1080,1920).w!=MaskWidth,"portrait grids differ and keep the DXL path");
 }
 Check(sizeof(BlendConstants)==48,"12 root constants match the shader cbuffer");
 for(auto extent:{Dimensions{5120,2160},Dimensions{2560,1440},Dimensions{1080,1920},Dimensions{8192,64}}){
  const auto grid=DetectorSize(extent.w,extent.h);Check((std::max)(grid.w,grid.h)==640,"detector resolution unchanged");
  for(unsigned y=0;y<grid.h;++y)for(unsigned x=0;x<grid.w;++x){
   const unsigned shaderX=x*extent.w/grid.w,shaderY=y*extent.h/grid.h;
   Check(shaderX==uint64_t(x)*extent.w/grid.w&&shaderY==uint64_t(y)*extent.h/grid.h,"GPU point grid equals previous CPU sample coordinates");
  }
 }
 Check(!DetectorSize(0,1080).w&&!DetectorSize(8193,1080).w,"invalid detector input refused");
 Check(MaximumWork(5120,2160,5120,2160,true)==160,"5K precision cap must reject 200 percent before budgeting");
 Check(!EstimateBytes(5120,2160,5120,2160,true,200,100,100,1),"invalid dimension is not a valid memory estimate");
 Check(RecognitionRequested(false,true),"preview must run before regional recipe apply");
 Check(!RecognitionRequested(false,false),"idle global mode does not request detector");
 Check(RecognitionRequested(true,false),"regional processing requires detector");
 Check(CanComposite(true,true,false),"mask preview independent from person NR availability");
 Check(!CanComposite(false,true,false),"regional effect still requires independent person NR");
 Check(!CanComposite(true,false,true),"preview cannot borrow unavailable surface");
 auto d=ModelSize(3840,2160,2560,1440,false,100,75);Check(d.w==1920&&d.h==1080,"render-base independent resolution");
 d=ModelSize(3840,2160,2560,1440,true,150,50);Check(d.w==2880&&d.h==1620,"output-base independent resolution");
 Check(!ModelSize(10,10,10,10,true,25).w,"tiny input blocked");Check(!ModelSize(8192,8192,8192,8192,true,200).w,"oversize input blocked");
 Check(!ModelSize(3840,2160,2560,1440,true,-1).w,"invalid scale blocked");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,1,1,1)-.2f)<1e-6f,"full fidelity retains original person (S30: to float rounding, via the scene look)");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,1,0,1)-.7f)<.00001f,"person uses independent result");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,0,0,1)-.9f)<.00001f,"scene uses independent result");
 Check(BlendRegion(.2f,.7f,.9f,0,0,0)==.2f,"scene disabled retains original");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,.5f,0,1)-.8f)<.00001f,"soft edge blends only selected branches");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,1,0,1,0)-.9f)<.00001f&&std::abs(BlendRegion(.2f,.7f,.9f,0,0,1,0)-.9f)<.00001f,"S30: zero weight is the scene look everywhere, never the original picture");
 Check(std::abs(BlendRegion(.2f,.7f,.9f,1,0,1,.5f)-.8f)<.00001f&&std::abs(BlendRegion(.2f,.7f,.9f,0,0,1,.5f)-.9f)<.00001f,"S30: the weight fades only the person look toward the scene look"); 
 Check(std::abs(BlendRegion(.2f,.7f,.9f,1,.3839907f,.3839907f)-(.2f+.6160093f*.5f))<.00001f,"S17 applied recipe: person keeps 61.6% of its delta");
 const auto size=EstimateBytes(3840,2160,2560,1440,false,100,75,50,3);
 Check(size>512ull*1024*1024,"memory estimate covers detector and all layers");
 Check(FitsBudget(100,size+100,size),"exact remaining budget allowed");Check(!FitsBudget(100,size+99,size),"insufficient budget held");
 Check(!FitsBudget(UINT64_MAX,UINT64_MAX,size),"budget cannot underflow");Check(!FitsBudget(0,0,size),"unknown budget held");
 Check(!EstimateBytes(3840,2160,2560,1440,true,100,75,50,4),"fourth layer refused");
 {// S28 depth gate: only a texel clearly behind every confident person texel nearby is cleared.
  using namespace latemask;
  Check(GateTexel(.6f,.005f,.03f),"far background under the soft rim is cleared");
  Check(!GateTexel(.6f,.025f,.03f)&&!GateTexel(.6f,.03f*(1.f-GateTolerance),.03f),"up to 1.25x farther stays (strict)");
  Check(!GateTexel(.6f,.05f,.03f),"nearer than the person (grass, a weapon held forward) stays");
  Check(!GateTexel(0.f,.005f,.03f),"nothing is ever added");
  Check(!GateTexel(.6f,-1.f,.03f)&&!GateTexel(.6f,.005f,-1.f),"unknown depth, or no confident person nearby: as voted");
  Check(!GateTexel(1.f,.03f,.03f),"a confident texel is its own reference");
  Check(GateRadius>0&&GateRadius%int(GateStride)==0,"the window samples the texel itself");
  Check(word::GatedPx*4==84&&word::GatedPx*4+4<=96&&word::GatedPx<word::Words,"gate counter lies in the words the latch zeroes each frame");
  const std::string gate=DepthGateShader;
  Check(gate.find("latched.InterlockedAdd(84,")!=std::string::npos,"the shader counts into word GatedPx");
  Check(gate.find("q<qRef*(1-tolerance)")!=std::string::npos&&gate.find("tempB[t]>=confident")!=std::string::npos&&gate.find("mvFrames.Load((now%motionSlots)*4)==now")!=std::string::npos,
   "the shader applies the same rule, only to this frame's depth");
  Check(RegionalPersonFidelity==0.f&&RegionalSceneStrength==1.f,"S28: person and scene looks at full effect");
 }
 {// S30 temporal smoothing: slow, motion-following fades instead of switches.
  const auto step=MaskTemporalStep(41.7,true);
  Check(!step.reset&&std::abs(step.rise-(1.f-std::exp(-41.7f/MaskRiseTauMs)))<1e-5f&&std::abs(step.fall-(1.f-std::exp(-41.7f/MaskFallTauMs)))<1e-5f,"one frame at 24 fps: rise and fall rates from their time constants");
  Check(step.rise>step.fall&&MaskRiseTauMs<MaskFallTauMs,"a person fades in faster than a missed recognition fades out");
  Check(MaskTemporalStep(41.7,false).reset&&MaskTemporalStep(0.,true).reset&&MaskTemporalStep(double(MaskHistoryGapMs)+1,true).reset&&!MaskTemporalStep(double(MaskHistoryGapMs),true).reset,"a gap, a new stream or no elapsed time restarts the history");
  Check(TemporalValue(.3f,.9f,TemporalStep{})==.9f,"restart: the shown mask is this frame's vote");
  float up=0,down=1;for(int i=0;i<24;++i){up=TemporalValue(up,1,step);down=TemporalValue(down,0,step);}
  Check(up>.95f&&down<.2f&&down>.1f,"after one second: in to 96%, out to 19%");
  float dip=1;for(int i=0;i<3;++i)dip=TemporalValue(dip,0,step);
  Check(dip>.8f,"a three-frame recognition dropout dims the person look by less than 20%");
  float flicker=0;for(int i=0;i<48;++i)flicker=TemporalValue(flicker,i%2?1.f:0.f,step);
  Check(flicker>.55f&&flicker<.75f,"an on/off flicker of the vote shows as a steady mid value, not a flash");
  using namespace latemask;
  Check(word::HeldPx*4==88&&word::HistoryRejectedPx*4==92&&word::HistoryRejectedPx*4+4<=96,"temporal counters lie in the words the latch zeroes each frame");
  const std::string temporal=TemporalShader,blend=BlendShader;
  Check(temporal.find("v=h+(n-h)*(n>h?rise:fall);")!=std::string::npos&&temporal.find("latched.InterlockedAdd(88,")!=std::string::npos&&temporal.find("latched.InterlockedAdd(92,")!=std::string::npos,"the shader applies the same rule and counts into HeldPx/HistoryRejectedPx");
  Check(temporal.find("Known(now)&&Known(now-1)")!=std::string::npos&&temporal.find("abs(d-dPrev)<=depthTolerance*max(d,dPrev)+depthFloor")!=std::string::npos,"history only along this frame's recorded motion and never across a depth jump");
  Check(blend.find("latched.Load(0)==0")==std::string::npos&&blend.find("sceneLook+saturate(weight)*m*(personLook-sceneLook)")!=std::string::npos,"blend: nothing latched is no longer the original picture");
 }
 ID3DBlob *shader=nullptr,*error=nullptr;const auto hr=D3DCompile(BlendShader,sizeof(BlendShader)-1,"033-person-scene",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&error);
 if(error){printf("%s\n",static_cast<const char*>(error->GetBufferPointer()));error->Release();}Check(SUCCEEDED(hr)&&shader,"production blend shader compiles");shader->Release();
 shader=nullptr;error=nullptr;const auto captureHr=D3DCompile(CaptureShader,sizeof(CaptureShader)-1,"033-capture-grid",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&error);
 if(error){printf("%s\n",static_cast<const char*>(error->GetBufferPointer()));error->Release();}Check(SUCCEEDED(captureHr)&&shader,"production point-grid capture shader compiles");shader->Release();
 struct {const char* text;size_t bytes;const char* name;} late[]={{MotionRingShader,sizeof(MotionRingShader)-1,"motion ring"},{LatchShader,sizeof(LatchShader)-1,"latch"},
  {GatherShader,sizeof(GatherShader)-1,"motion-chain gather"},
  {VoteShader,sizeof(VoteShader)-1,"three-vote"},{DepthGateShader,sizeof(DepthGateShader)-1,"S28 depth gate"},{TemporalShader,sizeof(TemporalShader)-1,"S30 temporal"},{HistoryStoreShader,sizeof(HistoryStoreShader)-1,"S30 history store"},
  {FeatherShader,sizeof(FeatherShader)-1,"feather"}};
 for(const auto& s:late){shader=nullptr;error=nullptr;const auto lateHr=D3DCompile(s.text,s.bytes,s.name,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&error);
  if(error){printf("%s: %s\n",s.name,static_cast<const char*>(error->GetBufferPointer()));error->Release();}Check(SUCCEEDED(lateHr)&&shader,"S20-S30 late-mask shader compiles");if(shader)shader->Release();}
 printf("YY DUAL CPU: %u checks, 0 failures; HLSL compilation only; no device or product execution\n",checks);return 0;
 }catch(const std::exception& e){printf("YY DUAL CPU FAILED: %s\n",e.what());return 1;}}
