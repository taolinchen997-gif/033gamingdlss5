// No D3D/COM headers or graphics imports. Uses the actual candidate probe.
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
namespace nrfault033 {static bool blocked=false;static bool Blocked(){return blocked;}}
namespace resolveleases {
struct Fence {uint64_t done=0;uint64_t GetCompletedValue()const{return done;}};
struct Use {void* queue=nullptr;Fence* fence=nullptr;uint64_t value=8;};
struct Slot {Use uses[4];uint64_t generation=0;unsigned pending=0;
 bool active=false,reset=false,pinned=false,recording=false,submitted=false,discarded=false;};
struct Ticket {Slot* slot=nullptr;uint64_t generation=0;};
static Slot slots[1];static std::mutex mutex;static unsigned collectCalls=0;
static bool Discarded(const Slot& s){return s.discarded;}
// Exact retained collector predicate, with mocked fences and no COM objects.
static void CollectLocked(){
 ++collectCalls;if(nrfault033::Blocked())return;
 for(auto& s:slots){
  if(!s.active||(!s.reset&&!Discarded(s))||s.recording||s.pending||s.pinned)continue;
  bool complete=true,observed=false;
  for(const auto& u:s.uses)if(u.queue){observed=true;const auto done=u.fence?u.fence->GetCompletedValue():0;
   if(!u.fence||done==UINT64_MAX||done<u.value)complete=false;}
  if((s.reset||Discarded(s))&&!s.recording&&!s.pending&&!s.pinned&&observed&&complete)s={};
 }
}
// Verbatim production Completed body for admission comparison.
static bool Completed(Ticket ticket){
 if(!ticket.slot||!ticket.generation)return false;
 std::lock_guard<std::mutex> lock(mutex);CollectLocked();
 return !ticket.slot->active||ticket.slot->generation!=ticket.generation;
}
}
#include "resolveleases_readback_probe.inc"
int main(){
 unsigned checks=0;const auto check=[&](bool condition){++checks;if(!condition)throw std::runtime_error("probe CPU check failed");};
 try{
  using namespace resolveleases;using namespace yyreadbackdiag;
  Fence ready{8},waiting{7},faulted{UINT64_MAX};
  for(unsigned mask=0;mask<256;++mask)for(unsigned fenceCase=0;fenceCase<5;++fenceCase){
   Slot original;original.generation=10;original.active=(mask&1)!=0;original.reset=(mask&2)!=0;original.discarded=(mask&4)!=0;
   original.pending=(mask&8)?2:0;original.recording=(mask&16)!=0;original.pinned=(mask&32)!=0;
   original.submitted=(mask&64)!=0;nrfault033::blocked=(mask&128)!=0;
   if(fenceCase)original.uses[0]={reinterpret_cast<void*>(1),fenceCase==1?nullptr:fenceCase==2?&waiting:fenceCase==3?&faulted:&ready,8};
   slots[0]=original;const Ticket ticket{&slots[0],10};const unsigned callsBefore=collectCalls;
   const bool legacy=Completed(ticket);check(collectCalls==callsBefore+1);
   slots[0]=original;const auto probe=ProbeReadback(ticket);check(collectCalls==callsBefore+2);
   check(probe.retired==legacy);check(probe.valid);check(probe.retired==(probe.reasons==None));
   if(original.active){check(probe.observed);check(probe.submitted==original.submitted);check(probe.reset==original.reset);check(probe.discarded==original.discarded);
    check(probe.FencesReadyObserved()==(fenceCase==4));
    if(!legacy&&(!original.reset&&!original.discarded))check((probe.reasons&WaitingResetOrDiscard)!=0);
    if(!legacy&&original.pinned)check((probe.reasons&Pinned)!=0);
    if(!legacy&&original.pending)check((probe.reasons&PendingSubmission)!=0);
    if(!legacy&&original.recording)check((probe.reasons&Recording)!=0);
    if(!legacy&&fenceCase==2)check((probe.reasons&GpuWaiting)!=0);
    if(!legacy&&fenceCase==3)check((probe.reasons&GpuFault)!=0);
    if(!legacy&&fenceCase==1)check((probe.reasons&MissingFence)!=0);
   }else check(!probe.observed);
  }
  const auto beforeInvalid=collectCalls;check(!ProbeReadback({nullptr,10}).retired);check(!ProbeReadback({&slots[0],0}).retired);check(collectCalls==beforeInvalid);
  nrfault033::blocked=false;slots[0]=Slot{};slots[0].generation=11;slots[0].active=true;
  const auto reused=ProbeReadback({&slots[0],10});check(reused.retired&&!reused.observed);
  FirstSeen unknown;check(unknown.Observe(reused,0));check(unknown.retired.present&&unknown.retired.atNs==0);
  check(!unknown.submitted.present&&!unknown.reset.present&&!unknown.discarded.present&&!unknown.fencesReady.present);
  Probe p;p.valid=true;p.observed=true;p.ticketSlot=1;p.ticketGeneration=20;
  FirstSeen seen;check(seen.Observe(p,10));check(!seen.submitted.present);
  p.submitted=true;p.observedQueues=1;p.gpuWaiting=true;check(seen.Observe(p,20));check(seen.submitted.atNs==20&&!seen.fencesReady.present);
  p.gpuWaiting=false;check(seen.Observe(p,30));check(seen.fencesReady.atNs==30&&!seen.reset.present);
  p.reset=true;p.retired=true;check(seen.Observe(p,40));check(seen.reset.atNs==40&&seen.retired.atNs==40);
  check(seen.Observe(p,50));check(seen.submitted.atNs==20&&seen.fencesReady.atNs==30&&seen.reset.atNs==40);
  ++p.ticketGeneration;check(!seen.Observe(p,60));check(seen.generation==20);
  // Snapshot eligibility is never enough: only the actual gate marks retired.
  Probe eligible;eligible.valid=true;eligible.observed=true;eligible.submitted=true;eligible.reset=true;eligible.observedQueues=1;
  check(Classify(eligible)==AwaitCollection);check(!eligible.retired);
  std::printf("readback lease probe CPU: %u checks, 0 failures; 1280 collector equivalence cases; no GPU/game\n",checks);
  return 0;
 }catch(const std::exception& e){std::printf("FAILED after %u: %s\n",checks,e.what());return 1;}
}
