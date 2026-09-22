// Include at namespace/global scope AFTER resolve_leases.h and Ticket/Completed.
// Diagnostic replacement for a worker's existing Completed(ticket) call.
// Never call both per poll. No logging, writes, waits, hooks or new GPU work.
#include <chrono>
#include "readback_lease_diagnostics.h"
namespace resolveleases {
static yyreadbackdiag::Probe ProbeReadback(Ticket ticket){
 using Clock=std::chrono::steady_clock;
 const auto ms=[](Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
 yyreadbackdiag::Probe out;
 out.ticketSlot=reinterpret_cast<uintptr_t>(ticket.slot);out.ticketGeneration=ticket.generation;
 if(!ticket.slot||!ticket.generation)return out; // Exactly Completed's invalid-ticket rule.
 out.valid=true;
 const auto begin=Clock::now();
 std::unique_lock<std::mutex> lock(mutex);
 const auto locked=Clock::now();out.lockWaitMs=ms(begin,locked);
 auto* s=ticket.slot;
 // Snapshot the SAME generation before CollectLocked can clear its fields.
 // These are first-observed states, not the time a driver/GPU event occurred.
 if(s->active&&s->generation==ticket.generation){
  out.observed=true;out.submitted=s->submitted;out.reset=s->reset;out.discarded=Discarded(*s);
  out.pending=s->pending;out.recording=s->recording;out.pinned=s->pinned;
  out.faultBlocked=nrfault033::Blocked();
  for(const auto& u:s->uses)if(u.queue){
   ++out.observedQueues;
   if(!u.fence){out.missingFence=true;continue;}
   const auto done=u.fence->GetCompletedValue();
   if(done==UINT64_MAX)out.gpuFault=true;
   else if(done<u.value)out.gpuWaiting=true;
  }
 }
 const auto snapshotted=Clock::now();out.snapshotMs=ms(locked,snapshotted);
 // Retain the exact original collector and complete-lifetime admission.
 CollectLocked();
 const auto collected=Clock::now();out.collectMs=ms(snapshotted,collected);
 out.retired=!ticket.slot->active||ticket.slot->generation!=ticket.generation;
 out.reasons=yyreadbackdiag::Classify(out);
 lock.unlock();out.totalMs=ms(begin,Clock::now());
 return out;
}
}
