// CPU-only diagnostic values. They never authorize Map or change retirement.
#pragma once
#include <cstdint>
namespace yyreadbackdiag {
enum Reason : uint32_t {
 None=0,InvalidTicket=1u<<0,Unsubmitted=1u<<1,NoObservedQueue=1u<<2,
 PendingSubmission=1u<<3,Pinned=1u<<4,Recording=1u<<5,GpuWaiting=1u<<6,
 WaitingResetOrDiscard=1u<<7,FaultBlocked=1u<<8,GpuFault=1u<<9,
 MissingFence=1u<<10,AwaitCollection=1u<<11
};
struct Probe {
 uintptr_t ticketSlot=0;uint64_t ticketGeneration=0;
 bool valid=false,observed=false,retired=false,submitted=false,reset=false,discarded=false;
 bool recording=false,pinned=false,faultBlocked=false,gpuWaiting=false,gpuFault=false,missingFence=false;
 unsigned pending=0,observedQueues=0;
 uint32_t reasons=InvalidTicket;
 double lockWaitMs=0,snapshotMs=0,collectMs=0,totalMs=0;
 bool FencesReadyObserved()const{return observed&&observedQueues&&!gpuWaiting&&!gpuFault&&!missingFence;}
};
inline uint32_t Classify(const Probe& p){
 if(!p.valid)return InvalidTicket;
 if(p.retired)return None; // Sole admission is the existing post-Collect slot/generation proof.
 uint32_t r=0;
 if(!p.submitted)r|=Unsubmitted;
 if(!p.observedQueues)r|=NoObservedQueue;
 if(p.pending)r|=PendingSubmission;
 if(p.pinned)r|=Pinned;
 if(p.recording)r|=Recording;
 if(p.gpuWaiting)r|=GpuWaiting;
 if(!p.reset&&!p.discarded)r|=WaitingResetOrDiscard;
 if(p.faultBlocked)r|=FaultBlocked;
 if(p.gpuFault)r|=GpuFault;
 if(p.missingFence)r|=MissingFence;
 // Atomic allocator/fault/fence observations can change during collection.
 // Even an apparently eligible snapshot never authorizes early Map.
 return r?r:AwaitCollection;
}
struct ObservedAt {bool present=false;uint64_t atNs=0;
 void Once(bool value,uint64_t now){if(value&&!present){present=true;atNs=now;}}
};
struct FirstSeen {
 uintptr_t slot=0;uint64_t generation=0;
 ObservedAt submitted,fencesReady,reset,discarded,retired;
 bool Observe(const Probe& p,uint64_t observationNs){
  if(!p.valid)return false;
  if(!generation){slot=p.ticketSlot;generation=p.ticketGeneration;}
  if(slot!=p.ticketSlot||generation!=p.ticketGeneration)return false;
  // Retired/reused slots do not reconstruct unobserved historical events.
  submitted.Once(p.observed&&p.submitted,observationNs);
  fencesReady.Once(p.FencesReadyObserved(),observationNs);
  reset.Once(p.observed&&p.reset,observationNs);
  discarded.Once(p.observed&&p.discarded,observationNs);
  retired.Once(p.retired,observationNs);
  return true;
 }
};
}
