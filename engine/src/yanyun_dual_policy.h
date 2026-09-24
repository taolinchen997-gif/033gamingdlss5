#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace yanyundual {
struct FrameIdentity {uint64_t stream=0,generation=0,frame=0,capturedMs=0;unsigned width=0,height=0;};
enum class MaskReason : unsigned {Ready,Empty,Stream,Generation,FutureFrame,Extent,Clock,Age};
// S18 continuity. S17 game log: ~22 real fps, capture->GPU-complete ~60 ms plus
// ~23 ms inference, so a mask reaches composition 85-140 ms after its source
// frame. The former hard 100 ms gate therefore switched the WHOLE picture
// between the regional result and the original every few frames. Identity
// checks stay hard; age now only scales the regional effect: full weight up to
// MaskFullWeightMs, linear fade to zero at MaskFadeEndMs, and any rise after a
// gap is limited to 0->1 over MaskFadeInMs. The source capture time is never
// rewritten and a mask at/after MaskFadeEndMs is never used.
// S27b (user 「疯狂的闪啊，就是启用失败启用失败」): in the S27 session the
// recognition fell to ~4-5 per second (GPU inference 100+ ms while the game used
// the GPU), so the newest recognition was 250-600 ms old: the millisecond fade
// dimmed the partition and at 600 ms dropped it (original picture, person history
// reset) on almost every recognition. Since S22 each latched mask follows the
// motion recorded for every frame since its capture, so the age that matters is
// in FRAMES, against that chain: full weight up to MaskFullWeightFrames, a linear
// fade to MaskFadeEndFrames (== latemask::MaxFrames), refused beyond. A stalled
// stream is still refused after MaskHardLimitMs.
inline constexpr uint64_t MaskFullWeightFrames=16,MaskFadeEndFrames=24,MaskHardLimitMs=4000;
// S30 (user: 「渐变可不可以调更自然点，就是更慢一点会更好感觉」): the rise of the
// shown weight takes MaskFadeInMs = 800 ms (was 250).
inline constexpr uint64_t MaskFullWeightMs=250,MaskFadeEndMs=600,MaskFadeInMs=800,MaskFadeStepCapMs=100;
inline MaskReason MaskStatus(const FrameIdentity& captured,const FrameIdentity& current,uint64_t now,uint64_t maxAgeMs=MaskHardLimitMs){
 if(!captured.stream||!captured.generation||!captured.frame||!captured.width||!captured.height)return MaskReason::Empty;
 if(captured.stream!=current.stream)return MaskReason::Stream;
 if(captured.generation!=current.generation)return MaskReason::Generation;
 if(captured.frame>current.frame)return MaskReason::FutureFrame;
 if(captured.width!=current.width||captured.height!=current.height)return MaskReason::Extent;
 if(now<captured.capturedMs)return MaskReason::Clock;
 if(current.frame-captured.frame>=MaskFadeEndFrames)return MaskReason::Age;
 return now-captured.capturedMs<maxAgeMs?MaskReason::Ready:MaskReason::Age;
}
inline bool Fresh(const FrameIdentity& captured,const FrameIdentity& current,uint64_t now){
 return MaskStatus(captured,current,now)==MaskReason::Ready;
}
// S27b ceiling from the source age in frames: 1 while the GPU chain can follow
// it comfortably, 0 at/after the chain's end.
inline float MaskFrameWeight(uint64_t framesOld){
 if(framesOld<=MaskFullWeightFrames)return 1.f;
 if(framesOld>=MaskFadeEndFrames)return 0.f;
 return float(MaskFadeEndFrames-framesOld)/float(MaskFadeEndFrames-MaskFullWeightFrames);
}
// S18 millisecond ceiling, no longer used for composition (kept for its tests).
inline float MaskAgeWeight(uint64_t ageMs){
 if(ageMs<=MaskFullWeightMs)return 1.f;
 if(ageMs>=MaskFadeEndMs)return 0.f;
 return float(MaskFadeEndMs-ageMs)/float(MaskFadeEndMs-MaskFullWeightMs);
}
// Weight actually shown this frame. A fall follows the age ceiling at once (it
// is already continuous); a rise is rate limited so a returning mask never
// pops. elapsedMs = wall time since the previous composition call, any outcome.
inline float ShownMaskWeight(float previous,uint64_t elapsedMs,float ceiling){
 if(!(ceiling>0.f))return 0.f;
 if(ceiling>1.f)ceiling=1.f;
 if(!(previous>0.f))previous=0.f;
 const float rise=float((std::min)(elapsedMs,MaskFadeStepCapMs))/float(MaskFadeInMs);
 return (std::min)(ceiling,previous+rise);
}
// S30 temporal smoothing of the person mask, per texel on the GPU (TemporalShader):
// the shown mask moves toward this frame's voted mask with time constant
// MaskRiseTauMs when it grows and MaskFallTauMs when it shrinks, and its history
// follows the motion recorded for this frame. A person therefore fades in over
// ~0.3-0.9 s and a missed recognition fades out over ~0.6-1.8 s instead of
// switching. A frame gap, a new stream or a new GPU mask set restarts the history
// (reset: the shown mask is this frame's vote). The CPU keeps composing for
// MaskHistoryHoldMs after the last usable recognition so the history can fade.
inline constexpr float MaskRiseTauMs=300.f,MaskFallTauMs=600.f;
inline constexpr uint64_t MaskHistoryGapMs=250,MaskHistoryHoldMs=3000;
struct TemporalStep {float rise=1.f,fall=1.f;unsigned reset=1;};
inline TemporalStep MaskTemporalStep(double elapsedMs,bool continuous){
 if(!continuous||!(elapsedMs>0.)||elapsedMs>double(MaskHistoryGapMs))return {};
 return {float(1.-std::exp(-elapsedMs/MaskRiseTauMs)),float(1.-std::exp(-elapsedMs/MaskFallTauMs)),0u};
}
// CPU mirror of one texel of TemporalShader (history already moved along the motion).
inline float TemporalValue(float history,float fresh,const TemporalStep& s){
 return s.reset?fresh:history+(fresh-history)*(fresh>history?s.rise:s.fall);
}
// S20 late-latched, motion-forwarded person mask.
// Latency: S19's composite chose the mask when the CPU RECORDED the frame, about
// one GPU frame before that frame executes, so masks were 2-3 frames old (S19
// log: 94-125 ms at ~20 fps). Now the worker writes every finished mask into a
// persistently mapped UPLOAD ring and flips one 32-bit index last; the frame's
// command list reads that index on the GPU when it EXECUTES (late latch) and
// uses the newest finished mask, normally the previous frame's.
// Alignment: S18/S19 sampled the old mask at uv + d*mv(current pixel). Pixels
// the person just uncovered carry background motion and pulled the old person
// area along: a white trail chasing a running character. Now each person texel
// of the source mask is pushed forward with the motion it had in ITS OWN frame
// (stored per frame in a small GPU ring at capture time), so nothing is left
// behind. Feathering runs after the push, so the soft edge moves with the body.
// S22: the S20 push assumed every texel keeps the velocity of its source frame.
// The S21 game log (three votes per frame) showed the older masks disagreeing
// at the edges on 3321 of ~3680 person frames: accelerating, stopping, turning
// and swinging limbs all break that assumption, and with a median the edge then
// follows the middle mask (slow edges, a shadow behind the body; on a sudden
// stop the two older masks overshoot and out-vote the person entirely). Each
// latched mask is now GATHERED along the motion actually recorded for every
// frame since it was captured (current texel -> previous frame -> ... -> its
// own frame). A step whose depth jumps beyond the tolerance below means the
// trajectory crossed onto another object -- background the person just
// uncovered -- and that mask then says nothing there (0): this is the S18 white
// trail, rejected instead of drawn. Depth, not velocity, decides: a sudden stop
// or start, a turn or a swinging arm changes velocity but keeps the body's
// depth, while a slow walk past a still background changes depth but hardly
// velocity. Only where a frame has no usable depth does a velocity jump decide.
// Sub-texel positions are read bilinearly, so no holes and no closing pass.
namespace latemask {
inline constexpr unsigned Slots=8,ControlBytes=256,HeaderBytes=64,MaskWidth=640,Pitch=640,MaxMaskHeight=1280;
inline constexpr unsigned SlotStride=HeaderBytes+Pitch*MaxMaskHeight;
inline constexpr uint64_t RingBytes=uint64_t(ControlBytes)+uint64_t(Slots)*SlotStride;
// GPU accepts a mask at most 24 real frames old (the CPU ms fade still applies to
// the newest recognition); the motion ring holds every frame from the oldest such
// mask up to now. S27 doubled both (16/12) for the protagonist hold below.
inline constexpr unsigned MotionSlots=32,MaxFrames=24;
static_assert(MaxFrames<MotionSlots,"a latched mask's whole motion chain fits the ring");
static_assert(MaskFadeEndFrames==MaxFrames&&MaskFullWeightFrames<MaskFadeEndFrames,"the CPU weight fades out exactly where the GPU stops following");
inline constexpr uint32_t Magic=0x4B53414Du;
// S22 fallback (no depth) chain tolerance, mask texels per frame: the same
// object's velocity in two consecutive frames may differ by 1.5 texels plus
// half its speed (limbs, acceleration); a larger jump is a different object.
inline constexpr float ChainToleranceTexels=1.5f,ChainToleranceRelative=.5f;
inline bool ChainConsistent(float ax,float ay,float bx,float by){
 const float d=std::hypot(ax-bx,ay-by),s=(std::max)(std::hypot(ax,ay),std::hypot(bx,by));
 return std::isfinite(d)&&std::isfinite(s)&&d<=ChainToleranceTexels+ChainToleranceRelative*s;
}
// Depth is stored as q ~ near/z (the game's value when inverted, 1-depth
// otherwise), -1 when unknown. The same surface stays within 15% between two
// frames (running toward the camera at 20 fps moves ~10%); a person against the
// background behind it differs by far more. Unknown depth never rejects.
inline constexpr float DepthTolerance=.15f,DepthFloor=1e-4f;
inline bool SameSurface(float a,float b){
 if(a<0.f||b<0.f)return true;
 return std::fabs(a-b)<=DepthTolerance*(std::max)(a,b)+DepthFloor;
}
// One step of the chain (velocities in texels per frame, depths as stored):
// depth decides when both frames have it, velocity otherwise.
inline bool ChainContinues(float ax,float ay,float bx,float by,float depthA,float depthB){
 if(depthA>=0.f&&depthB>=0.f)return SameSurface(depthA,depthB);
 return ChainConsistent(ax,ay,bx,by);
}
struct SlotHeader {uint32_t magic,seq,frame,capturedLo,capturedHi,streamLo,streamHi,genLo,genHi,width,height,maskW,maskH,pitch,seqCheck,reserved;};
static_assert(sizeof(SlotHeader)==HeaderBytes,"GPU reads the header as four uint4");
static_assert(SlotStride%16==0&&ControlBytes%16==0,"ByteAddressBuffer Load4 alignment");
// Index word at ring offset 0: 0 = nothing published; otherwise (seq<<3)|slot, seq>=1.
inline uint32_t EncodeIndex(uint32_t seq,unsigned slot){return (seq<<3)|(slot&7u);}
inline unsigned IndexSlot(uint32_t index){return index&7u;}
inline uint32_t IndexSeq(uint32_t index){return index>>3;}
inline unsigned MaskHeight(unsigned width,unsigned height){
 if(!width||!height)return 0;
 return (std::max)(1u,unsigned(double(height)*MaskWidth/width+.5));
}
// S22: when the recognition grid already IS the late-mask grid (landscape
// frames: DetectorSize == 640 x MaskHeight), the person strength byte is
// 255-coverage on person texels and 0 elsewhere -- exactly the bytes DXL
// ComposeSemanticMask writes there with feather 0, since both of its resampling
// steps are identities at equal size -- without the 7-13 ms that compose cost
// on the worker thread before EVERY publication (S21 log publish_ms). Returns
// the strength sum. G/B/A stay 0 like DXL's background byte.
inline uint64_t DirectPersonStrength(const uint8_t* coverage,const uint8_t* groups,size_t texels,uint8_t* rgba){
 uint64_t sum=0;
 for(size_t i=0;i<texels;++i){
  const uint8_t v=groups[i]==0?uint8_t(255-coverage[i]):uint8_t(0);
  rgba[i*4]=v;rgba[i*4+1]=rgba[i*4+2]=rgba[i*4+3]=0;sum+=v;
 }
 return sum;
}
// Same kernel as DXL ComposeSemanticMask's feather (radius ceil(f), sigma max(.35, f/2)).
inline int FeatherRadius(float feather){return std::isfinite(feather)?int(std::ceil((std::clamp)(feather,0.f,8.f))):2;}
inline float FeatherSigma(float feather){return (std::max)(.35f,(std::isfinite(feather)?(std::clamp)(feather,0.f,8.f):2.f)*.5f);}
// S21 three-vote mask. Every S20 composite used exactly one recognition, and
// each recognition is an independent YOLO pass: a person anchor under CONF_TH
// (.35) or out-scored by another class simply vanishes, and an empty result is
// still a valid all-scene mask. One missed (or spurious) recognition therefore
// switched the person between the person look (o+0.62(p-o) with the user's
// fidelity) and the scene look (o+0.99(s-o)) for a frame: the person layer
// "dropping out and coming back". The GPU now latches the THREE newest
// published masks, pushes each forward with the motion of its own frame and
// takes the per-texel median, so one missing or extra recognition is out-voted
// and nothing is smeared (all three are aligned first). With fewer than three
// valid masks the newest is used exactly as in S20.
// The worker additionally holds back recognitions in a row whose person coverage
// fell below 40% of the last published one, so a whole-body dropout never
// reaches the vote. S21 held two; S25 (user 2026-09-22 「人物识别还是会断，导致
// 闪烁」) holds six (~250 ms at ~24 recognitions/s): one 2-minute S23 session
// still published 23 dropouts longer than two, and with the three-vote every one
// longer than three flipped the person to the scene look. Holding longer is safe
// since S22: masks are gathered along the recorded motion with a depth check, so
// where the person really left, the old mask finds the background and gives 0;
// the S18 age fade then softens anything longer. The seventh is published anyway.
inline constexpr unsigned Votes=3;
inline constexpr float DropoutRatio=.4f,MinReferenceCoverage=.002f;
inline constexpr unsigned MaxHeldRecognitions=6;
static_assert(MaxHeldRecognitions+Votes<=MaxFrames,"held and voted masks stay within the motion chain the GPU can follow");
// S27 protagonist lock (user: 「主角一直在画面中间，按理说永远不掉识别才对？」).
// The worker tracks the main character (a person near the picture centre, locked
// after three recognitions in a row) and, when a recognition has no detection on
// the locked track even at its lowered rescue threshold, reports it missing.
// Those recognitions are held back up to 18 in a row (~1 s at ~18/s) instead of
// the coverage rule's six; the masks already published keep following the
// recorded motion with the depth check, so where the character really moved the
// old mask finds the background and gives 0. The coverage rule stays for all
// other collapses. One frame of pipeline lag is added to the chain budget.
inline constexpr unsigned MaxProtagonistHeld=18;
static_assert(MaxProtagonistHeld+Votes+1<=MaxFrames,"a held protagonist stays within the motion chain the GPU can follow");
// Telemetry bucket of a dropout that recovered after `held` held recognitions:
// 0 = 1-2 (hidden since S21), 1 = 3-4, 2 = 5-6 (hidden since S25).
inline unsigned DropoutRunBucket(unsigned held){return held<=2?0u:held<=4?1u:2u;}
inline bool HoldAsDropout(float coverage,float reference,unsigned heldInRow){
 return std::isfinite(coverage)&&std::isfinite(reference)&&reference>=MinReferenceCoverage&&
  coverage<reference*DropoutRatio&&heldInRow<MaxHeldRecognitions;
}
enum class HoldKind : unsigned {None,Dropout,Protagonist};
// S27b: a hold also ends before the last published mask leaves the chain the GPU
// follows (in the S27 session 18 held recognitions spanned far more than 24
// frames, and the GPU then latched nothing). framesSincePublished = this
// recognition's source frame minus the last published one's.
inline constexpr unsigned MaxHeldFrames=MaxFrames-Votes-1;
inline HoldKind HoldDecision(float coverage,float reference,unsigned heldInRow,bool protagonistMissing,uint64_t framesSincePublished){
 if(framesSincePublished>=MaxHeldFrames)return HoldKind::None;
 if(protagonistMissing&&heldInRow<MaxProtagonistHeld)return HoldKind::Protagonist;
 return HoldAsDropout(coverage,reference,heldInRow)?HoldKind::Dropout:HoldKind::None;
}
// Telemetry bucket of a protagonist hold that ended after `held` recognitions:
// 0 = 1-2, 1 = 3-6 (the coverage rule's reach), 2 = 7-12, 3 = 13-18.
inline unsigned ProtagonistRunBucket(unsigned held){return held<=2?0u:held<=6?1u:held<=12?2u:3u;}
// Coverage change against the last published recognition: 0 fell >60%, 1 fell
// 30-60%, 2 steady, 3 rose 43-150%, 4 rose >150%.
inline unsigned CoverageChangeBucket(float coverage,float reference){
 if(!(reference>0.f)||!std::isfinite(coverage))return 2;
 const float r=coverage/reference;
 return r<DropoutRatio?0u:r<.7f?1u:r<=1.43f?2u:r<=2.5f?3u:4u;
}
inline float Median3(float a,float b,float c){return (std::max)((std::min)(a,b),(std::min)((std::max)(a,b),c));}
// newest/older/oldest = the aligned masks at one texel; count = valid latched masks.
inline float VoteValue(unsigned count,float newest,float older,float oldest){
 return count>=Votes?Median3(newest,older,oldest):count?newest:0.f;
}
// Latched buffer (u2, 256 bytes), written by LatchShader and VoteShader and
// copied every composite into a READBACK ring for the log (telemetry only).
namespace word {enum : unsigned {Count=0,Age0=1,Frame0=2,Offset0=3,Valid1=4,Age1=5,Frame1=6,Offset1=7,Valid2=8,Age2=9,Frame2=10,Offset2=11,
 Seq0=12,Stamp=13,Index=14,Latched=15,NewestPx=16,VotedPx=17,FilledPx=18,RemovedPx=19,TrailRejectedPx=20,GatedPx=21,HeldPx=22,HistoryRejectedPx=23,Words=64};}
inline constexpr unsigned LatchedBytes=word::Words*4,ReadbackSlots=16,ReadbackLag=8;
// Per-frame verdict from the vote counters (texels at >= .5 in the mask grid).
enum class VoteKind : unsigned {None,Steady,Edge,Dropout,Extra};
inline VoteKind ClassifyVote(uint32_t newestPx,uint32_t votedPx,uint32_t filledPx,uint32_t removedPx){
 if(!newestPx&&!votedPx)return VoteKind::None;
 if(filledPx>=64&&uint64_t(filledPx)*4>=votedPx)return VoteKind::Dropout; // >= a quarter of the person restored
 if(removedPx>=64&&uint64_t(removedPx)*4>=newestPx)return VoteKind::Extra; // >= a quarter of the newest voted out
 if(filledPx+removedPx>=16)return VoteKind::Edge;
 return VoteKind::Steady;
}
}
struct Dimensions {unsigned w=0,h=0;};
inline Dimensions DetectorSize(unsigned w,unsigned h){
 if(!w||!h||w>8192||h>8192)return {};
 const double scale=640.0/(std::max)(w,h);
 return {(std::max)(1u,unsigned(w*scale+.5)),(std::max)(1u,unsigned(h*scale+.5))};
}
inline constexpr char CaptureShader[]=R"(
Texture2D<float4> source : register(t0);
RWTexture2D<float4> reduced : register(u0);
cbuffer Sizes : register(b0) {uint2 sourceSize;uint2 targetSize;};
[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){
 if(any(p.xy>=targetSize))return;
 uint2 q=min(sourceSize-1,p.xy*sourceSize/targetSize);
 reduced[p.xy]=source.Load(int3(q,0));
})";
inline Dimensions ModelSize(unsigned w,unsigned h,unsigned gw,unsigned gh,bool full,int work,int relative=100){
 if(!w||!h||!gw||!gh||work<25||work>200||relative<50||relative>100)return {};
 const uint64_t bw=full?w:gw,bh=full?h:gh;
 const uint64_t x=((bw*unsigned(work)/100)&~uint64_t(1))*unsigned(relative)/100;
 const uint64_t y=((bh*unsigned(work)/100)&~uint64_t(1))*unsigned(relative)/100;
 if(x<64||y<64||x>8192||y>8192||x*y>33554432)return {};
 return {unsigned(x)&~1u,unsigned(y)&~1u};
}
inline int MaximumWork(unsigned w,unsigned h,unsigned gw,unsigned gh,bool full){
 for(int work=200;work>=25;--work)if(ModelSize(w,h,gw,gh,full,work).w)return work;return 0;
}
inline bool RecognitionRequested(bool regional,bool preview){return regional||preview;}
inline bool CanComposite(bool preview,bool maskSurfaceReady,bool modelReady){return preview?maskSurfaceReady:modelReady;}
inline uint64_t EstimateBytes(unsigned w,unsigned h,unsigned gw,unsigned gh,bool full,int work,int second,int third,int layers){
 if(layers<1||layers>3||!w||!h||w>8192||h>8192||uint64_t(w)*h>33554432)return 0;
 uint64_t bytes=uint64_t(w)*h*64+512ull*1024*1024; // textures plus bounded inference/driver reserve; not an exact allocator prediction
 for(int i=0;i<layers;++i){const auto d=ModelSize(w,h,gw,gh,full,work,i==0?100:i==1?second:third);if(!d.w)return 0;bytes+=uint64_t(d.w)*d.h*192;}
 return bytes;
}
inline bool FitsBudget(uint64_t usage,uint64_t budget,uint64_t bytes){return bytes&&budget&&usage<budget&&bytes<=budget-usage;}
// S32 模式二: the person bank holds no NR model, only the finish of the chain's first
// layer (model size) and the person composition targets (output size).
inline uint64_t SharedEstimateBytes(unsigned w,unsigned h,unsigned modelW,unsigned modelH){
 if(!w||!h||!modelW||!modelH||w>8192||h>8192||modelW>8192||modelH>8192||uint64_t(w)*h>33554432||uint64_t(modelW)*modelH>33554432)return 0;
 return uint64_t(w)*h*16*3+uint64_t(modelW)*modelH*16+64ull*1024*1024;
}
// Identity of a 模式二 person bank: geometry and formats only. Person model settings
// are not rendered in 模式二, so editing them never rebuilds this bank.
inline uint64_t SharedKey(unsigned w,unsigned h,unsigned modelW,unsigned modelH,unsigned format,unsigned modelFormat){
 uint64_t hash=14695981039346656037ull;auto mix=[&](uint32_t u){hash=(hash^u)*1099511628211ull;};
 mix(0x53333221u);mix(w);mix(h);mix(modelW);mix(modelH);mix(format);mix(modelFormat);return hash;
}
// S30: the weight fades the PERSON look toward the scene look; the scene look
// itself never fades (S18-S29 faded both toward the original picture, so an old
// recognition dimmed the whole screen). At weight 1 the result is unchanged.
inline float BlendRegion(float original,float character,float scene,float mask,float fidelity,float enhancement,float weight=1.f){
 const float m=std::clamp(mask,0.f,1.f);
 const float sceneLook=original+enhancement*(scene-original),personLook=original+(1.f-fidelity)*(character-original);
 return sceneLook+std::clamp(weight,0.f,1.f)*m*(personLook-sceneLook);
}
// Root constants of BlendShader, in cbuffer order (12 x 32-bit).
struct BlendConstants {unsigned w=0,h=0;float fidelity=0,strength=0;unsigned preview=0,available=0;float weight=0;unsigned pad[5]{};};
// S28 (user: 「人物保真度和场景强度默认不打折，直接去掉！」): the person column's look
// and the scene column's look are applied in full, exactly like the whole-picture
// settings; saved records and share codes keep the old fields, which are ignored.
inline constexpr float RegionalPersonFidelity=0.f,RegionalSceneStrength=1.f;
static_assert(sizeof(BlendConstants)==48,"blend root constants");
// t3 = mask latched, aligned, voted, gated, smoothed in time and feathered on the
// GPU (S20-S30). S30: nothing latched no longer means the original picture: the
// smoothed mask (fading, or zero) blends the person look into the scene look.
inline constexpr char BlendShader[]=R"(
Texture2D<float4> original : register(t0);
Texture2D<float4> character : register(t1);
Texture2D<float4> scene : register(t2);
Texture2D<float> personMask : register(t3);
RWTexture2D<float4> result : register(u0);
RWByteAddressBuffer latched : register(u1);
SamplerState linearClamp : register(s0);
cbuffer Settings : register(b0) {uint2 size;float fidelity;float enhancement;uint preview;uint available;float weight;uint pad0;uint4 pad1;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=size))return;
 float4 o=original.Load(int3(id.xy,0));
 if(!available){result[id.xy]=o;return;}
 float2 uv=(float2(id.xy)+.5)/float2(size);
 float m=saturate(personMask.SampleLevel(linearClamp,uv,0));
 if(preview){result[id.xy]=float4(m,m,m,o.a);return;}
 float3 p=character.Load(int3(id.xy,0)).rgb;
 float3 s=scene.Load(int3(id.xy,0)).rgb;
 float3 sceneLook=o.rgb+enhancement*(s-o.rgb),personLook=o.rgb+(1-fidelity)*(p-o.rgb);
 result[id.xy]=float4(sceneLook+saturate(weight)*m*(personLook-sceneLook),o.a);
})";
// ---- late-mask passes (S20-S22): one root signature for all of them ----
// b0: 16 root constants | t0 game motion (typed) | t1 upload ring (raw) | t2 game depth (typed)
// u0 motion ring (uint array: packed half2 motion, then float depth slices) | u1 motion ring frames (raw)
// u2 latch result (raw) | u3 aligned masks (float array, one slice per vote)
// u4 tempA (float) | u5 tempB (float) | u6 mask (unorm R8) | u7 history (float, S30)
struct LateConstants {uint32_t v[16]{};};
static_assert(sizeof(LateConstants)==64,"late root constants");
// Per real frame, at the capture point: this frame's motion (slice) and depth
// (slice + depthSlices, as q ~ near/z, -1 unknown) at the mask grid. S22:
// point-sampled (the pixel under the texel centre), so a texel carries the
// velocity and depth of ONE object instead of a blend across a silhouette.
// depthMode: 0 none, 1 standard (near = 0), 2 inverted (near = 1).
inline constexpr char MotionRingShader[]=R"(
Texture2D<float4> motion : register(t0);
Texture2D<float> depth : register(t2);
RWTexture2DArray<uint> mvRing : register(u0);
RWByteAddressBuffer mvFrames : register(u1);
cbuffer C : register(b0) {uint2 size;float2 toUv;uint slice;uint frame;uint depthMode;uint depthSlices;uint4 pad1;uint4 pad2;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(all(id.xy==0))mvFrames.Store(slice*4,frame);
 if(any(id.xy>=size))return;
 float2 uv=(float2(id.xy)+.5)/float2(size);
 uint mw,mh;motion.GetDimensions(mw,mh);
 float2 mv=motion.Load(int3(min(uint2(uv*float2(mw,mh)),uint2(mw,mh)-1),0)).xy*toUv;
 if(!all(abs(mv)<65536.0))mv=0;
 mvRing[uint3(id.xy,slice)]=f32tof16(mv.x)|(f32tof16(mv.y)<<16);
 float q=-1;
 if(depthMode!=0){
  uint dw,dh;depth.GetDimensions(dw,dh);
  float d=depth.Load(int3(min(uint2(uv*float2(dw,dh)),uint2(dw,dh)-1),0));
  q=depthMode==2?d:1-d;
  if(!(q>=0&&q<=1))q=-1;
 }
 mvRing[uint3(id.xy,slice+depthSlices)]=asuint(q);
})";
// One thread, at EXECUTION time: the newest published slot and, walking back
// one publication at a time, up to Votes-1 older ones. Each must pass the S20
// checks and be strictly older than the one before; the walk stops at the
// first slot that does not. Also zeroes the vote counters for this frame.
inline constexpr char LatchShader[]=R"(
ByteAddressBuffer ring : register(t1);
RWByteAddressBuffer latched : register(u2);
cbuffer C : register(b0) {uint currentFrame;uint streamLo;uint streamHi;uint genLo;uint genHi;uint width;uint height;uint maskW;uint maskH;uint maxFrames;uint slotStride;uint pitch;uint votes;uint pad0;uint pad1;uint pad2;};
[numthreads(1,1,1)]void main(){
 uint index=ring.Load(0);
 uint slot=index&7u,seq=index>>3,count=0,newer=0xFFFFFFFFu;
 uint4 e0=(uint4)0,e1=(uint4)0,e2=(uint4)0;
 bool alive=index!=0;
 [unroll]for(uint k=0;k<3;++k){
  if(alive&&k<votes&&seq!=0){
   uint base=256+slot*slotStride;
   uint4 a=ring.Load4(base),b=ring.Load4(base+16),c=ring.Load4(base+32),h=ring.Load4(base+48);
   uint frame=a.z;
   bool ok=a.x==0x4B53414Du&&a.y==seq&&h.z==seq&&b.y==streamLo&&b.z==streamHi&&b.w==genLo&&c.x==genHi&&
    c.y==width&&c.z==height&&c.w==maskW&&h.x==maskH&&h.y==pitch&&frame!=0&&frame<newer&&frame<currentFrame&&currentFrame-frame<=maxFrames;
   if(ok){
    uint4 e=uint4(1,currentFrame-frame,frame,base+64);
    if(k==0)e0=e;else if(k==1)e1=e;else e2=e;
    count+=1;newer=frame;slot=(slot+7u)&7u;seq-=1u;
   }else alive=false;
  }
 }
 latched.Store4(0,uint4(count,e0.y,e0.z,e0.w));
 latched.Store4(16,e1);latched.Store4(32,e2);
 latched.Store4(48,uint4(index>>3,currentFrame,index,count));
 latched.Store4(64,(uint4)0);latched.Store4(80,(uint4)0);
})";
// S22, per latched mask (dispatch z = Votes): from each texel follow the motion
// recorded for every frame back to the mask's own frame and read the mask there
// (bilinear). A depth jump (a velocity jump where depth is unknown) between two
// consecutive frames of the trajectory -- including the mask frame itself at
// the end -- means another object (background the person uncovered): that
// mask then gives 0. Missing motion for
// any frame of the chain: the mask stays in place (S20 rule). Counts, for the
// newest mask, texels where a rejection removed person strength >= .5.
inline constexpr char GatherShader[]=R"(
ByteAddressBuffer ring : register(t1);
RWTexture2DArray<uint> mvRing : register(u0);
RWByteAddressBuffer mvFrames : register(u1);
RWByteAddressBuffer latched : register(u2);
RWTexture2DArray<float> aligned : register(u3);
cbuffer C : register(b0) {uint2 size;uint motionSlots;uint pitch;float tolTexels;float tolRelative;float depthTolerance;float depthFloor;uint4 pad1;uint4 pad2;};
groupshared uint trail;
float2 Motion(uint frame,float2 p){
 int2 t=clamp(int2(floor(p)),int2(0,0),int2(size)-1);
 uint packed=mvRing[uint3(t,frame%motionSlots)];
 float2 v=float2(f16tof32(packed&0xFFFFu),f16tof32(packed>>16));
 return all(abs(v)<65536.0)?v:float2(0,0);
}
float Depth(uint frame,float2 p){
 int2 t=clamp(int2(floor(p)),int2(0,0),int2(size)-1);
 return asfloat(mvRing[uint3(t,frame%motionSlots+motionSlots)]);
}
bool Known(uint frame){return mvFrames.Load((frame%motionSlots)*4)==frame;}
bool Consistent(float2 a,float2 b){
 float2 s=float2(size);
 return length((a-b)*s)<=tolTexels+tolRelative*max(length(a*s),length(b*s));
}
bool Continues(float2 v,float2 vBefore,float d,float dBefore){
 if(d>=0&&dBefore>=0)return abs(d-dBefore)<=depthTolerance*max(d,dBefore)+depthFloor;
 return Consistent(v,vBefore);
}
float Texel(uint offset,int2 t){
 t=clamp(t,int2(0,0),int2(size)-1);
 uint w=ring.Load(offset+uint(t.y)*pitch+(uint(t.x)&~3u));
 return float((w>>((uint(t.x)&3u)*8u))&255u)/255.0;
}
float Bilinear(uint offset,float2 p){
 float2 c=p-.5;int2 i=int2(floor(c));float2 f=c-float2(i);
 return lerp(lerp(Texel(offset,i),Texel(offset,i+int2(1,0)),f.x),lerp(Texel(offset,i+int2(0,1)),Texel(offset,i+int2(1,1)),f.x),f.y);
}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 if(gi==0)trail=0;
 GroupMemoryBarrierWithGroupSync();
 uint4 l=latched.Load4(id.z*16);
 if(all(id.xy<size)){
  float value=0;
  if(l.x!=0){
   uint source=l.z,now=l.z+l.y;
   bool chain=true;
   for(uint k=source;k<=now;++k)chain=chain&&Known(k);
   float2 p=float2(id.xy)+.5,before=float2(0,0);
   float depthBefore=-1;
   bool ok=true;
   if(chain){
    for(uint j=now;j>source;--j){
     float2 v=Motion(j,p);float d=Depth(j,p);
     if(j!=now&&!Continues(v,before,d,depthBefore))ok=false;
     p+=v*float2(size);before=v;depthBefore=d;
    }
    if(!Continues(Motion(source,p),before,Depth(source,p),depthBefore))ok=false;
    if(any(p<0)||any(p>float2(size)))ok=false;
   }
   float m=Bilinear(l.w,p);
   value=ok?m:0;
   if(!ok&&id.z==0&&m>=.5){uint o;InterlockedAdd(trail,1u,o);}
  }
  aligned[id]=value;
 }
 GroupMemoryBarrierWithGroupSync();
 if(gi==0&&trail!=0){uint o;latched.InterlockedAdd(80,trail,o);}
})";
// Per texel: median of the three aligned masks (newest alone when fewer are
// valid) -> tempB, the feather input. Counts texels (>= .5) for the log.
inline constexpr char VoteShader[]=R"(
RWByteAddressBuffer latched : register(u2);
RWTexture2DArray<float> aligned : register(u3);
RWTexture2D<float> tempB : register(u5);
cbuffer C : register(b0) {uint2 size;uint votes;uint pad0;uint4 pad1;uint4 pad2;uint4 pad3;};
groupshared uint counts[4];
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 if(gi<4)counts[gi]=0;
 GroupMemoryBarrierWithGroupSync();
 uint count=latched.Load(0);
 bool inside=all(id.xy<size);
 float n=0,v=0;
 if(inside&&count>0){
  n=aligned[uint3(id.xy,0)];v=n;
  if(count>=votes&&votes>=3){float b=aligned[uint3(id.xy,1)],c=aligned[uint3(id.xy,2)];v=max(min(n,b),min(max(n,b),c));}
 }
 if(inside){
  tempB[id.xy]=v;
  uint o;
  if(n>=.5)InterlockedAdd(counts[0],1u,o);
  if(v>=.5)InterlockedAdd(counts[1],1u,o);
  if(n<.5&&v>=.5)InterlockedAdd(counts[2],1u,o);
  if(n>=.5&&v<.5)InterlockedAdd(counts[3],1u,o);
 }
 GroupMemoryBarrierWithGroupSync();
 if(gi<4&&counts[gi]!=0){uint o;latched.InterlockedAdd(64+gi*4,counts[gi],o);}
})";
// Separable feather with the DXL kernel. vertical 0: tempB -> tempA; 1: tempA -> mask.
inline constexpr char FeatherShader[]=R"(
RWTexture2D<float> tempA : register(u4);
RWTexture2D<float> tempB : register(u5);
RWTexture2D<unorm float> mask : register(u6);
cbuffer C : register(b0) {uint2 size;uint vertical;int radius;float sigma;uint3 pad0;uint4 pad1;uint4 pad2;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=size))return;
 float sum=0,total=0;
 for(int k=-radius;k<=radius;++k){
  float w=exp(-float(k*k)/(2*sigma*sigma));
  float s;
  if(vertical)s=tempA[int2(id.x,clamp(int(id.y)+k,0,int(size.y)-1))];
  else s=tempB[int2(clamp(int(id.x)+k,0,int(size.x)-1),id.y)];
  sum+=s*w;total+=w;
 }
 float v=total>0?sum/total:0;
 if(vertical)mask[id.xy]=saturate(v);else tempA[id.xy]=v;
})";
// S28 depth gate (user: 「你做S28吧」, proposed as edges that follow the current
// frame). After the vote, a texel that is farther from the camera than every
// confident person texel within GateRadius (by more than GateTolerance, depth as
// q ~ near/z of THIS frame) is background the mask slid onto -- the halo around
// the head and shoulders, or the trail of an older recognition -- and is cleared.
// Unknown depth (-1) or no confident person nearby leaves the texel as voted;
// nothing is ever added. Reads tempB, writes tempA (S30: TemporalShader reads it).
// Tolerance .2 = 1.25x farther (about 0.75 m behind a person 3 m away), so a
// limb or a cape swinging back stays; a confident texel (>= GateConfident) is
// its own reference and is never cleared. Counts cleared texels that were >= .5.
inline constexpr int GateRadius=8;inline constexpr unsigned GateStride=2;
inline constexpr float GateTolerance=.2f,GateConfident=.9f;
inline bool GateTexel(float v,float q,float qRef){return v>0.f&&q>=0.f&&qRef>=0.f&&q<qRef*(1.f-GateTolerance);}
inline constexpr char DepthGateShader[]=R"(
RWTexture2DArray<uint> mvRing : register(u0);
RWByteAddressBuffer mvFrames : register(u1);
RWByteAddressBuffer latched : register(u2);
RWTexture2D<float> tempA : register(u4);
RWTexture2D<float> tempB : register(u5);
cbuffer C : register(b0) {uint2 size;uint now;uint motionSlots;int radius;uint stride;float tolerance;float confident;uint4 pad1;uint4 pad2;};
groupshared uint gated;
float Depth(int2 t){return asfloat(mvRing[uint3(uint2(t),now%motionSlots+motionSlots)]);}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 if(gi==0)gated=0;
 GroupMemoryBarrierWithGroupSync();
 if(all(id.xy<size)){
  float v=tempB[id.xy],result=v;
  if(v>0&&mvFrames.Load((now%motionSlots)*4)==now){
   float q=Depth(int2(id.xy));
   if(q>=0){
    float qRef=-1;
    for(int dy=-radius;dy<=radius;dy+=int(stride))for(int dx=-radius;dx<=radius;dx+=int(stride)){
     int2 t=clamp(int2(id.xy)+int2(dx,dy),int2(0,0),int2(size)-1);
     if(tempB[t]>=confident){float qt=Depth(t);if(qt>=0)qRef=qRef<0?qt:min(qRef,qt);}
    }
    if(qRef>=0&&q<qRef*(1-tolerance)){result=0;if(v>=.5){uint o;InterlockedAdd(gated,1u,o);}}
   }
  }
  tempA[id.xy]=result;
 }
 GroupMemoryBarrierWithGroupSync();
 if(gi==0&&gated!=0){uint o;latched.InterlockedAdd(84,gated,o);}
})";
// S30 temporal pass: tempA (this frame's gated vote) and the history (u7, the shown
// mask of the previous composited frame) -> tempB, the feather input. The history
// is read where this texel was one frame ago (this frame's recorded motion); a
// depth jump there (another object: background the person uncovered) or a missing
// motion record drops the history for that texel. Counts texels the history held
// (>= .5 while the vote said < .5) and texels whose history was dropped.
inline constexpr char TemporalShader[]=R"(
RWTexture2DArray<uint> mvRing : register(u0);
RWByteAddressBuffer mvFrames : register(u1);
RWByteAddressBuffer latched : register(u2);
RWTexture2D<float> tempA : register(u4);
RWTexture2D<float> tempB : register(u5);
RWTexture2D<float> history : register(u7);
cbuffer C : register(b0) {uint2 size;uint now;uint motionSlots;float rise;float fall;uint reset;float depthTolerance;float depthFloor;uint3 pad0;uint4 pad1;};
groupshared uint held,dropped;
bool Known(uint frame){return mvFrames.Load((frame%motionSlots)*4)==frame;}
float2 Motion(int2 t){uint packed=mvRing[uint3(uint2(t),now%motionSlots)];float2 v=float2(f16tof32(packed&0xFFFFu),f16tof32(packed>>16));return all(abs(v)<65536.0)?v:float2(0,0);}
float Depth(uint frame,int2 t){return asfloat(mvRing[uint3(uint2(t),frame%motionSlots+motionSlots)]);}
float History(float2 p){
 float2 c=p-.5;int2 i=int2(floor(c));float2 f=c-float2(i);int2 hi=int2(size)-1;
 float a=history[clamp(i,int2(0,0),hi)],b=history[clamp(i+int2(1,0),int2(0,0),hi)];
 float d=history[clamp(i+int2(0,1),int2(0,0),hi)],e=history[clamp(i+int2(1,1),int2(0,0),hi)];
 return lerp(lerp(a,b,f.x),lerp(d,e,f.x),f.y);
}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 if(gi==0){held=0;dropped=0;}
 GroupMemoryBarrierWithGroupSync();
 if(all(id.xy<size)){
  float n=tempA[id.xy],v=n;
  if(!reset&&Known(now)&&Known(now-1)){
   int2 t=int2(id.xy);float2 p=float2(id.xy)+.5+Motion(t)*float2(size);
   if(all(p>=0)&&all(p<=float2(size))){
    float d=Depth(now,t),dPrev=Depth(now-1,clamp(int2(floor(p)),int2(0,0),int2(size)-1));
    if(d<0||dPrev<0||abs(d-dPrev)<=depthTolerance*max(d,dPrev)+depthFloor){
     float h=History(p);v=h+(n-h)*(n>h?rise:fall);
     if(h>=.5&&n<.5&&v>=.5){uint o;InterlockedAdd(held,1u,o);}
    }else{uint o;InterlockedAdd(dropped,1u,o);}
   }
  }
  tempB[id.xy]=saturate(v);
 }
 GroupMemoryBarrierWithGroupSync();
 if(gi==0){uint o;if(held!=0)latched.InterlockedAdd(88,held,o);if(dropped!=0)latched.InterlockedAdd(92,dropped,o);}
})";
// S30: the shown (pre-feather) mask becomes the next frame's history.
inline constexpr char HistoryStoreShader[]=R"(
RWTexture2D<float> tempB : register(u5);
RWTexture2D<float> history : register(u7);
cbuffer C : register(b0) {uint2 size;uint2 pad0;uint4 pad1;uint4 pad2;uint4 pad3;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(all(id.xy<size))history[id.xy]=tempB[id.xy];})";
}
