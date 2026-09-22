// S27 CPU fixture for the person-first rule and the protagonist lock in the
// production decoder, on synthetic YOLO11-seg tensors. No model, GPU or game.
#include "yolo_mask_decode.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
static unsigned checks=0;
static void Check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
constexpr uint32_t A=8400;
struct Tensors {
 std::vector<float> det=std::vector<float>(116*A,0.f),proto=std::vector<float>(32*160*160,0.f);
 Tensors(){for(size_t i=0;i<160*160;++i)proto[i]=10.f;} // plane 0: sigmoid ~1 everywhere
 // One anchor: box in the 640 canvas, person score, optional other class, mask = plane 0.
 void Anchor(uint32_t a,float cx,float cy,float w,float h,float person,uint32_t otherClass=0,float other=0.f){
  det[a]=cx;det[A+a]=cy;det[2*A+a]=w;det[3*A+a]=h;det[4*A+a]=person;
  if(otherClass)det[size_t(4+otherClass)*A+a]=other;
  det[84*A+a]=1.f;
 }
 void Clear(uint32_t a){for(uint32_t r=0;r<116;++r)det[size_t(r)*A+a]=0.f;}
};
static size_t People(const DXL::SemanticMaskSnapshot& m){size_t n=0;for(size_t i=0;i<m.coverage.size();++i)n+=m.groupIds[i]==0&&m.coverage[i]<128;return n;}
int main()try{
 using namespace yanyunmask;
 constexpr uint32_t W=640,H=400; // landscape recognition grid, canvas content 640x400
 // 1) Person-first: an umbrella (class 25) scoring above the person on the same anchor.
 {Tensors t;t.Anchor(7,320,220,100,200,.5f,25,.6f);DXL::SemanticMaskSnapshot legacy,now;DecodeReport r;
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,legacy,nullptr,nullptr,false)&&People(legacy)==0,"legacy argmax rule drops the out-scored person");
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,now,nullptr,&r)&&People(now)>15000&&r.personFirstKept==1&&r.detections==1,"S27 keeps a person out-scored by an umbrella");
  t.Clear(7);t.Anchor(7,320,220,100,200,.3f,25,.6f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,now)&&People(now)==0,"the person threshold itself is unchanged");}
 // 2) Acquisition: a centred person locks on the third recognition in a row; an edge person never does.
 {Tensors t;ProtagonistTrack track;DecodeReport r;DXL::SemanticMaskSnapshot m;
  t.Anchor(11,320,220,100,200,.8f);
  for(int i=0;i<2;++i){Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==0&&!track.locked,"not locked before three in a row");}
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PSeen)&&track.locked&&track.acquired==1,"locked on the third");
  Tensors edge;ProtagonistTrack other;edge.Anchor(11,40,220,60,200,.9f);
  for(int i=0;i<6;++i)Check(Decode(edge.det.data(),edge.det.size(),edge.proto.data(),edge.proto.size(),W,H,1,1,m,&other,&r)&&!other.locked,"an edge person is not the protagonist");
  Tensors tiny;ProtagonistTrack small;tiny.Anchor(11,320,220,12,30,.9f);
  for(int i=0;i<6;++i)Check(Decode(tiny.det.data(),tiny.det.size(),tiny.proto.data(),tiny.proto.size(),W,H,1,1,m,&small,&r)&&!small.locked,"a distant small person is not the protagonist");
  // 3) Tracking: small moves stay seen; the box follows.
  t.Clear(11);t.Anchor(11,330,222,104,198,.7f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PSeen)&&track.box.x1>277.f,"moved a little: still seen, box updated");
  // 4) Rescue: the character's anchor dips to .2 (below CONF_TH) -> kept from RESCUE_TH.
  t.Clear(11);t.Anchor(11,330,222,104,198,.2f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PSeen|PRescued)&&People(m)>15000&&track.rescued==1,"a weak anchor on the track is rescued");
  Tensors plain;plain.Anchor(11,330,222,104,198,.2f);ProtagonistTrack none;
  Check(Decode(plain.det.data(),plain.det.size(),plain.proto.data(),plain.proto.size(),W,H,1,1,m,&none,&r)&&People(m)==0,"without a lock the same anchor stays below the threshold");
  // Rescue ignores other classes too (a hat scoring higher).
  t.Clear(11);t.Anchor(11,330,222,104,198,.2f,25,.7f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&(r.protagonist&PRescued)&&People(m)>15000,"rescue on the track ignores a higher other class");
  // 5) Not rescued: below RESCUE_TH, or off the track.
  t.Clear(11);t.Anchor(11,330,222,104,198,.1f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PMissing)&&People(m)==0&&track.missed==1,"below RESCUE_TH: missing");
  t.Clear(11);t.Anchor(11,520,222,104,198,.25f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PMissing)&&People(m)==0&&track.missed==2,"a weak anchor off the track is not rescued");
  // A confident person elsewhere is drawn but is not the protagonist.
  t.Clear(11);t.Anchor(11,560,222,80,160,.9f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PMissing)&&People(m)>5000&&track.missed==3,"another person does not stand in for the protagonist");
  // 6) Back on the track: the miss run is counted (3 -> bucket 3-6).
  t.Clear(11);t.Anchor(11,330,222,104,198,.8f);
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PSeen)&&track.missed==0&&track.missRuns[1]==1,"seen again: miss run 3 recorded");
  // 7) Release after ReleaseMisses misses in a row; then it must be re-acquired.
  t.Clear(11);
  for(unsigned i=1;i<=ReleaseMisses;++i)Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PMissing),"missing while within the release limit");
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&!track.locked&&track.released==1&&track.missRuns[3]==1,"released after the limit");
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==0,"no flags once released");
  t.Anchor(11,330,222,104,198,.8f);
  for(int i=0;i<2;++i)Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==0,"re-acquisition needs three again");
  Check(Decode(t.det.data(),t.det.size(),t.proto.data(),t.proto.size(),W,H,1,1,m,&track,&r)&&r.protagonist==(PLocked|PSeen)&&track.acquired==2,"re-acquired");
  // 8) Portrait-shaped content (height > width) uses the same prior in its own content box.
  Tensors tall;ProtagonistTrack t2;tall.Anchor(11,200,320,90,260,.8f);
  for(int i=0;i<3;++i)Check(Decode(tall.det.data(),tall.det.size(),tall.proto.data(),tall.proto.size(),400,640,1,1,m,&t2,&r),"portrait decode");
  Check(t2.locked,"portrait content: centred person locks");}
 // 10) S28 minimum person area with hysteresis (content 640x400 = 256000 canvas units).
 {ProtagonistTrack t;DecodeReport r;DXL::SemanticMaskSnapshot m;
  Tensors small;small.Anchor(21,120,120,16,32,.9f); // 512 = 0.2%
  Check(Decode(small.det.data(),small.det.size(),small.proto.data(),small.proto.size(),W,H,1,1,m,&t,&r)&&People(m)==0&&r.detections==0&&t.smallDropped==1,"a distant 0.2% person is left to the scene look");
  Tensors mid;mid.Anchor(21,120,120,24,32,.9f); // 768 = 0.3%
  Check(Decode(mid.det.data(),mid.det.size(),mid.proto.data(),mid.proto.size(),W,H,1,1,m,&t,&r)&&People(m)==0&&t.smallDropped==2,"a new 0.3% person is not shown (below MinPersonArea)");
  Tensors big;big.Anchor(21,120,120,33,32,.9f); // 1056 = 0.41%
  Check(Decode(big.det.data(),big.det.size(),big.proto.data(),big.proto.size(),W,H,1,1,m,&t,&r)&&People(m)>500&&r.detections==1,"at MinPersonArea the person is shown");
  Check(Decode(mid.det.data(),mid.det.size(),mid.proto.data(),mid.proto.size(),W,H,1,1,m,&t,&r)&&People(m)>300&&t.smallKept==1,"shrinking to 0.3% the same person stays (hysteresis)");
  Check(Decode(mid.det.data(),mid.det.size(),mid.proto.data(),mid.proto.size(),W,H,1,1,m,&t,&r)&&People(m)>300&&t.smallKept==2,"and keeps staying while it stays above KeepPersonArea");
  Check(Decode(small.det.data(),small.det.size(),small.proto.data(),small.proto.size(),W,H,1,1,m,&t,&r)&&People(m)==0,"below KeepPersonArea it is released even if it was shown");
  Check(Decode(mid.det.data(),mid.det.size(),mid.proto.data(),mid.proto.size(),W,H,1,1,m,&t,&r)&&People(m)==0,"once released, 0.3% needs MinPersonArea again");
  Tensors elsewhere;elsewhere.Anchor(21,120,120,33,32,.9f);elsewhere.Anchor(22,500,120,24,32,.9f);
  Check(Decode(elsewhere.det.data(),elsewhere.det.size(),elsewhere.proto.data(),elsewhere.proto.size(),W,H,1,1,m,&t,&r)&&r.detections==1,"hysteresis is per person: a new small one elsewhere is still dropped");
  Check(Decode(small.det.data(),small.det.size(),small.proto.data(),small.proto.size(),W,H,1,1,m,nullptr,&r)&&People(m)>200,"without the worker state (fixtures) nothing is filtered");}
 // 11) The locked protagonist is never filtered, even when the camera pulls far away.
 {ProtagonistTrack t;DecodeReport r;DXL::SemanticMaskSnapshot m;Tensors near;near.Anchor(11,320,220,100,200,.8f);
  for(int i=0;i<3;++i)Decode(near.det.data(),near.det.size(),near.proto.data(),near.proto.size(),W,H,1,1,m,&t,&r);
  Check(t.locked,"protagonist locked");
  Tensors far;far.Anchor(11,320,220,14,30,.8f); // 420 = 0.16%, overlapping the track only a little
  Check(Decode(far.det.data(),far.det.size(),far.proto.data(),far.proto.size(),W,H,1,1,m,&t,&r)&&(r.protagonist&PMissing),"a tiny box that is not on the track is not the protagonist");
  Tensors step;step.Anchor(11,320,220,70,140,.8f); // IoU .49 with the track, 3.8%
  Check(Decode(step.det.data(),step.det.size(),step.proto.data(),step.proto.size(),W,H,1,1,m,&t,&r)&&(r.protagonist&PSeen),"zooming out: still seen");
  Tensors tiny;tiny.Anchor(11,320,220,40,80,.8f); // IoU .33 with 70x140, 1.25% -> kept anyway
  Check(Decode(tiny.det.data(),tiny.det.size(),tiny.proto.data(),tiny.proto.size(),W,H,1,1,m,&t,&r)&&(r.protagonist&PSeen),"further out: still seen");
  Tensors speck;speck.Anchor(11,320,220,24,48,.8f); // 1152 = .45%, IoU .36 with 40x80
  Check(Decode(speck.det.data(),speck.det.size(),speck.proto.data(),speck.proto.size(),W,H,1,1,m,&t,&r)&&(r.protagonist&PSeen),"far out: the protagonist is kept");
  Tensors dot;dot.Anchor(11,320,220,14,28,.8f); // 392 = .15% (< KeepPersonArea), IoU .34 with 24x48
  Check(Decode(dot.det.data(),dot.det.size(),dot.proto.data(),dot.proto.size(),W,H,1,1,m,&t,&r)&&(r.protagonist&PSeen)&&People(m)>100,"below every size limit the locked protagonist still passes the filter");}
 // 9) Miss-run buckets.
 Check(MissRunBucket(1)==0&&MissRunBucket(2)==0&&MissRunBucket(3)==1&&MissRunBucket(6)==1&&MissRunBucket(7)==2&&MissRunBucket(18)==2&&MissRunBucket(19)==3,"miss-run buckets");
 std::printf("S27 PROTAGONIST DECODE CPU: %u checks, 0 failures; synthetic tensors, production decoder; no model, GPU or game\n",checks);return 0;
}catch(const std::exception& e){std::printf("S27 PROTAGONIST DECODE CPU FAILED: %s\n",e.what());return 1;}
