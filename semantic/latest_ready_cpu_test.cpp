#include "bounded_readback_queue.h"
#include "../engine/src/yanyun_dual_policy.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* what){++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);}}
struct Job{uint64_t frame=0,captured=0,retire=0;bool leaseHeld=true;};
static std::shared_ptr<Job> Make(uint64_t f,uint64_t c,uint64_t r){auto p=std::make_shared<Job>();p->frame=f;p->captured=c;p->retire=r;return p;}
template<class Q,class Ready>static std::shared_ptr<Job> Promote(Q& queue,Ready ready){std::shared_ptr<Job> active;queue.PromoteNewestReady(active,ready);return active;}
int main(){using Q=yyworker::BoundedReadbackQueue<Job,2>;
 {Q q;auto a=Make(1,0,200),b=Make(2,20,40),c=Make(3,40,80);q.Submit(b);q.Submit(c);uint64_t now=50;
  auto current=a;const bool changed=q.PromoteNewestReady(current,[&](const Job& j){return now>=j.captured&&now-j.captured<=100&&now>=j.retire;});auto chosen=current;
  Check(changed&&current==b,"sole active job replaced atomically before pending capacity is reopened");
  Check(chosen==b&&chosen->captured==20&&chosen->frame==2,"ready B bypasses unretired A without choosing unretired C");
  current=chosen;Check(a->leaseHeld&&c->leaseHeld,"dropping CPU selection never ends original GPU lease");
  Check(q.TryTake()==c&&!q.TryTake(),"newer unretired source preserved exactly for later retirement");
  Check(now+10-current->captured<=100&&110-a->captured>100,"counterexample: ready promotion valid while FIFO head times out");}
 {Q q;auto a=Make(1,0,20),b=Make(2,20,200),c=Make(3,40,220);q.Submit(b);q.Submit(c);
  auto chosen=Promote(q,[](const Job& j){return j.retire<=50;});
  Check(!chosen&&a->retire<=50,"unretired newer frames cannot starve ready active source");
  Check(q.TryTake()==b&&q.TryTake()==c,"no eligible promotion leaves FIFO unchanged");}
 {Q q;auto b=Make(2,20,40),c=Make(3,40,60);q.Submit(b);q.Submit(c);auto chosen=Promote(q,[](const Job& j){return j.retire<=70;});
  Check(chosen==c&&q.HasCapacity()&&!q.TryTake(),"choose newest ready and discard older prefix to prevent backward publication");}
 {Q q;auto b=Make(2,0,20);q.Submit(b);auto chosen=Promote(q,[](const Job& j){return uint64_t(101)-j.captured<=100&&j.retire<=101;});
  Check(!chosen&&q.TryTake()==b,"expired ready result is not promoted or falsely restamped");}
 {Q q;auto b=Make(2,20,40),c=Make(3,40,80);q.Submit(b);bool appended=false;
  auto chosen=Promote(q,[&](const Job& j){Check(q.HasCapacity(),"readiness evaluated outside queue mutex");if(!appended){appended=q.Submit(c);}return j.retire<=50;});
  Check(chosen==b&&appended&&q.TryTake()==c,"producer append during readiness survives prefix removal");}
 {Q q;auto b=Make(2,20,40),c=Make(3,40,60);q.Submit(b);q.Submit(c);unsigned evaluated=0;
  auto chosen=Promote(q,[&](const Job& j){++evaluated;return j.retire<=70;});
  Check(chosen==c&&evaluated==1,"newest-ready checks at most fixed pending capacity");}
 {Q q;Check(!Promote(q,[](const Job&){return true;}),"empty queue has no source");auto a=Make(1,0,0);Check(q.Submit(a)&&q.WaitTake()==a,"existing blocking handoff still works");}
 {using namespace yanyundual;FrameIdentity prior{1,4,60,50,640,449},current{1,5,70,90,640,449};
  Check(MaskStatus(prior,current,90)==MaskReason::Generation,"fresh queued old-generation mask still rejected after mode apply");
  prior.generation=5;prior.stream=2;Check(MaskStatus(prior,current,90)==MaskReason::Stream,"fresh other-stream source cannot be relabeled or accepted");
  prior.stream=1;prior.frame=71;Check(MaskStatus(prior,current,90)==MaskReason::FutureFrame,"future source remains rejected");
  prior.frame=60;Check(MaskStatus(prior,current,151)==MaskReason::Ready&&MaskAgeWeight(151-50)==1.f,"S18: a 101ms source is shown at full weight, not blanked");
  Check(MaskStatus(prior,current,50+MaskFadeEndMs)==MaskReason::Age,"same identity never bypasses the fade end at actual composition");}
 for(unsigned i=0;i<100;++i){Q q;auto a=Make(i*3+1,20,40),b=Make(i*3+2,40,80),c=Make(i*3+3,60,100);q.Submit(a);
  std::atomic<bool> readyEntered{false},appended{false};std::shared_ptr<Job> chosen;
  std::thread consumer([&]{chosen=Promote(q,[&](const Job& j){readyEntered=true;while(!appended.load())std::this_thread::yield();return j.retire<=50;});});
  while(!readyEntered.load())std::this_thread::yield();const bool inserted=q.Submit(b);appended=true;consumer.join();
  Check(inserted&&chosen==a,"concurrent producer not blocked behind retirement predicate");Check(q.TryTake()==b,"concurrent append retains correct frame identity");
  Check(q.Submit(b)&&q.Submit(c)&&!q.Submit(a),"queue never exceeds two pending jobs");}
 // Compare controlled admission cases with a 10ms transform/inference stage.
 // These are counterexamples, not inferred timings for unlogged real frames.
 struct Case{uint64_t now,oldCaptured,newCaptured,newRetired;};
 for(const auto x:std::vector<Case>{{95,0,60,80},{90,0,50,70},{80,0,40,65}}){
  Q q;auto old=Make(1,x.oldCaptured,10),newer=Make(2,x.newCaptured,x.newRetired);q.Submit(newer);
  const auto next=Promote(q,[&](const Job& j){return x.now-j.captured<=100&&x.now>=j.retire;});
  Check(next==newer&&x.now+10-next->captured<x.now+10-old->captured,"latest-ready strictly reduces source age without modifying its stamp");
 }
 std::printf("latest fully-retired queue CPU: %u checks, %u failures; 100 producer/consumer interleavings; no GPU/game\n",checks,failures);
 return failures?1:0;
}
