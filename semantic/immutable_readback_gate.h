// Only for the physical write-once, sealed payload. Never use for ordinary
// replayable readback storage; original lease retirement remains unchanged.
#pragma once
#include "readback_lease_diagnostics.h"
namespace yyworker {
inline bool ImmutableReadbackReady(bool sealed,uintptr_t slot,uint64_t generation,
 const yyreadbackdiag::Probe& p,bool deviceHealthy,bool globallyFaultBlocked){
 if(!sealed||!slot||!generation||!p.valid||p.ticketSlot!=slot||p.ticketGeneration!=generation||!deviceHealthy||globallyFaultBlocked)return false;
 if(p.faultBlocked||p.gpuFault||p.missingFence||p.gpuWaiting||p.pending||p.recording||p.pinned)return false;
 if(p.retired)return p.reasons==yyreadbackdiag::None;
 return p.observed&&p.submitted&&p.observedQueues>0&&p.FencesReadyObserved()&&
  !p.reset&&!p.discarded&&p.reasons==yyreadbackdiag::WaitingResetOrDiscard;
}
}
