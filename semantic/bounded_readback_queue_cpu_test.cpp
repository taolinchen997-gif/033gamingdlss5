#include "bounded_readback_queue.h"
#include <cstdio>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <chrono>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}
struct Item{uint64_t frame=0,captured=0,retire=0;};
struct Result{unsigned frames=0,accepted=0,captures=0,published=0,expired=0,highWater=0;};
template<size_t Pending>static Result Simulate(bool pipelined,uint64_t leaseUs,uint64_t inferenceUs,bool permanentStall=false){
 yyworker::BoundedReadbackQueue<Item,Pending> queue;
 std::shared_ptr<Item> active,published;
 const uint64_t period=27778,end=27778*1200;
 uint64_t nextFrame=0,now=0,inferDone=0;unsigned frame=0,pending=0;bool computing=false;
 Result stats;
 auto start=[&]{if(!active){active=queue.TryTake();if(active)--pending;}};
 auto settle=[&]{
  for(;;){start();if(!active)return;
   if(computing){if(now<inferDone)return;published=active;++stats.published;if(now-active->captured>100000)++stats.expired;active.reset();computing=false;continue;}
   if(now-active->captured>100000){++stats.expired;active.reset();continue;}
   if(now>=active->retire){computing=true;inferDone=now+inferenceUs;}return;
  }
 };
 while(now<=end){
  uint64_t event=nextFrame;
  if(active)event=std::min(event,computing?inferDone:std::min(active->retire,active->captured+100001));
  now=event;
  if(now>end)break;
  settle();
  if(now==nextFrame){
   // Startup is excluded from steady-state acceptance percentage.
   if(frame>=12){++stats.frames;if(published&&now-published->captured<=100000)++stats.accepted;}
   const bool room=pipelined?queue.HasCapacity():!active;
   if(room){auto item=std::make_shared<Item>();item->frame=frame+1;item->captured=now;item->retire=permanentStall?end+1000000:now+leaseUs;
    Check(queue.Submit(item),"single producer precheck guarantees pending slot");++stats.captures;++pending;start();}
   stats.highWater=std::max(stats.highWater,unsigned(bool(active))+pending);
   ++frame;nextFrame=frame*period;
  }
  settle();
 }
 return stats;
}
int main(){try{
 yyworker::BoundedReadbackQueue<Item,1> queue;auto a=std::make_shared<Item>();a->frame=1;a->captured=77;
 auto b=std::make_shared<Item>();b->frame=2;b->captured=99;
 Check(queue.HasCapacity()&&queue.Submit(a),"first pending accepted");Check(!queue.HasCapacity()&&!queue.Submit(b),"second pending rejected, never replaces source");
 auto active=queue.TryTake();Check(active==a&&active->captured==77,"exact source survives take");Check(queue.Submit(b),"one pending allowed alongside active");
 Check(queue.WaitTake()==b&&b->captured==99,"worker directly takes queued item without new frame Pump");
 Check(!queue.TryTake()&&queue.HasCapacity(),"empty queue remains bounded");
 {uint64_t now=0;unsigned polls=0;const auto r=yyworker::AwaitFreshReadback([&]{++polls;return now>=54;},[&]{return now;},[&](unsigned n){now+=n;},0);
  Check(r==yyworker::FreshReadback::Ready&&now==54&&polls==28,"full retirement required before Map boundary");}
 {uint64_t now=0;unsigned polls=0;const auto r=yyworker::AwaitFreshReadback([&]{++polls;return false;},[&]{return now;},[&](unsigned n){now+=n;},0);
  Check(r==yyworker::FreshReadback::Expired&&now==102&&polls==51,"unretired source expires without Map/inference");}
 {uint64_t now=100;Check(yyworker::AwaitFreshReadback([]{return true;},[&]{return now;},[](unsigned){},0)==yyworker::FreshReadback::Ready,"100ms boundary retained");
  now=101;unsigned touched=0;Check(yyworker::AwaitFreshReadback([&]{++touched;return true;},[&]{return now;},[](unsigned){},0)==yyworker::FreshReadback::Expired&&touched==0,"expired pending never mapped even if retired");
  now=99;Check(yyworker::AwaitFreshReadback([]{return true;},[&]{return now;},[](unsigned){},100)==yyworker::FreshReadback::ClockInvalid,"clock inversion rejected");}
 for(unsigned i=0;i<100;++i){yyworker::BoundedReadbackQueue<Item> q;std::shared_ptr<Item> delivered;std::thread consumer([&]{delivered=q.WaitTake();});Check(q.Submit(a),"threaded single slot submit");consumer.join();Check(delivered==a,"threaded handoff preserves source identity");}
 const auto old=Simulate<1>(false,53000,7000),next=Simulate<1>(true,53000,7000),three=Simulate<2>(true,53000,7000);
 std::printf("36FPS L60ms (lease53+compute7), legacy %u/%u accepted=%.3f%%; 2-slot %u/%u accepted=%.3f%%; 3-slot %u/%u accepted=%.3f%%\n",old.accepted,old.frames,100.0*old.accepted/old.frames,next.accepted,next.frames,100.0*next.accepted/next.frames,three.accepted,three.frames,100.0*three.accepted/three.frames);
 Check(old.highWater<=1&&next.highWater<=2,"active plus one pending is absolute maximum");Check(next.accepted>old.accepted&&next.accepted<next.frames,"two slots improve but cannot claim continuous L60ms coverage");
 Check(three.accepted==three.frames&&three.highWater<=3,"three slots continuously cover specified 60ms case");
 const auto fast=Simulate<2>(true,30000,7000);std::printf("36FPS lease30+compute7: %u/%u accepted=%.3f%%\n",fast.accepted,fast.frames,100.0*fast.accepted/fast.frames);
 Check(fast.accepted==fast.frames&&fast.highWater<=3,"short enough total latency gives continuous steady-state");
 const auto stalled=Simulate<2>(true,200000,7000,true);Check(stalled.published==0&&stalled.highWater<=3&&stalled.expired>0,"permanent no-retirement stays bounded and never publishes");
 const auto slow=Simulate<2>(true,53000,110000);Check(slow.published>0&&slow.accepted==0&&slow.highWater<=3,"late completed result may publish but renderer never accepts it as fresh");
 std::printf("bounded pipeline CPU: %u checks, %u failures; no GPU or game\n",checks,failures);return failures?1:0;
 }catch(const std::exception& e){std::printf("ERROR %s\n",e.what());return 2;}}
