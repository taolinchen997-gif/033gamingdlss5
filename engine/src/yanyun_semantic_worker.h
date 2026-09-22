#pragma once
#include "yanyun_dual_state.h"
#include "yanyun_dual_policy.h"
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "../../semantic/worker_protocol.h"
#include "../../semantic/bounded_readback_queue.h"
#include "../../semantic/third_party/DXL/SemanticMask.h"
#include <condition_variable>
#include <chrono>
#include "../../semantic/readback_pixels.h"
#include "../../semantic/resolveleases_readback_probe.inc"
#include "../../semantic/readback_diagnostic_trace.h"
#include "../../semantic/immutable_readback_gate.h"
#include "yanyun_once_capture.h"
#include "owned_copy.h"
#include "yanyun_late_ring.h"
#include "yanyun_heap_pool.h"
namespace yanyundual {
using Microsoft::WRL::ComPtr;
inline void Require(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(message);}
inline std::filesystem::path SemanticDirectory(){
 HMODULE self=nullptr;wchar_t path[32768]{};
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(&SemanticDirectory),&self)||!GetModuleFileNameW(self,path,32768))throw std::runtime_error("semantic module path");
 return std::filesystem::path(path).parent_path()/L"semantic";
}
// protagonist: S27 worker flags (yyworker::protagonist), 0 from an older worker.
struct Recognition {FrameIdentity source;DXL::SemanticMaskSnapshot mask;double inferenceMs=0;uint32_t protagonist=0;};
using ReadbackClock=std::chrono::steady_clock;
inline double ReadbackMs(ReadbackClock::time_point a,ReadbackClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct ReadbackTrace {
 yyreadbackdiag::FirstSeen seen;uint32_t reasonsSeen=0,lastReasons=0;uint64_t polls=0;
 double lockMs=0,collectMs=0,snapshotMs=0;
};
struct Readback {
 mutable ReadbackTrace trace;
 ReadbackClock::time_point enqueuedAt{};double captureBuildMs=0;
 ComPtr<ID3D12Resource> control,buffer;ComPtr<ID3D12Device> device;D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
 UINT64 bytes=0;FrameIdentity source;resolveleases::Ticket ticket;unsigned inputW=0,inputH=0;
 bool sealedSnapshot=false,gridRanObserved=false;yyonce::Layout sealedLayout{};
};
inline std::atomic<int> recognitionStatus{0}; // UI reads without creating a worker thread
class SemanticWorker {
 yyworker::BoundedReadbackQueue<Readback,2> jobs_;
 std::shared_ptr<const Recognition> result_;
 yyworker::Client client_;
 ComPtr<ID3D12Device> device_;
 std::unique_ptr<yycopied::OwnedCopy> copy_; // Background-only; retained for the pinned worker lifetime.
 yyworker::diagnostic::Totals cycleTotals_;
 yyworker::diagnostic::LogBudget diagnosticBudget_;
 uint64_t expired_=0,promoted_=0,probePolls_=0,resetOnlyPolls_=0,gpuWaitPolls_=0,unsubmittedPolls_=0,incomplete_=0;
 double lockTotalMs_=0,collectTotalMs_=0,probeSnapshotMs_=0,diagnosticLogMs_=0;
 static uint64_t Ns(ReadbackClock::time_point time){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count());}
 // S18: a capture is still worth inferring while its result could be shown at
 // full weight. S27b: that is now judged in frames on the composition side; a
 // slow recognition (~4/s in the S27 session) expired most captures at 250 ms
 // and so recognised even less often. One second covers the frame window at
 // the frame rates seen; the newest capture is still preferred.
 static constexpr uint64_t SourceLimitMs=1000;
 static bool Fresh(const Readback& job){const auto now=GetTickCount64();return now>=job.source.capturedMs&&now-job.source.capturedMs<=SourceLimitMs;}
 static yycopied::SourceStatus CopySourceReady(void* context,ID3D12Resource* source,ID3D12Device* device){
  const auto* job=static_cast<const Readback*>(context);
  if(!job||!job->gridRanObserved||!job->device||!job->control||source!=job->buffer.Get()||!identity033::Equal(job->device.Get(),device))return yycopied::SourceStatus::Invalid;
  if(!Fresh(*job))return yycopied::SourceStatus::NotReady;
  // This second exact-ticket gate belongs to Convert timing. The original
  // lease lock is released by ProbeReadback before OwnedCopy submits anything.
  const auto observed=resolveleases::ProbeReadback(job->ticket);
  const bool ready=yyworker::ImmutableReadbackReady(job->sealedSnapshot,reinterpret_cast<uintptr_t>(job->ticket.slot),job->ticket.generation,
   observed,SUCCEEDED(device->GetDeviceRemovedReason()),nrfault033::Blocked());
  return ready?yycopied::SourceStatus::Ready:yycopied::SourceStatus::NotReady;
 }
 bool Probe(const Readback& job,yyworker::diagnostic::Cycle& cycle){
  const auto value=resolveleases::ProbeReadback(job.ticket);
  const auto observed=ReadbackClock::now();auto& trace=job.trace;
  trace.seen.Observe(value,Ns(observed));trace.reasonsSeen|=value.reasons;trace.lastReasons=value.reasons;++trace.polls;
  trace.lockMs+=value.lockWaitMs;trace.collectMs+=value.collectMs;trace.snapshotMs+=value.snapshotMs;
  lockTotalMs_+=value.lockWaitMs;collectTotalMs_+=value.collectMs;probeSnapshotMs_+=value.snapshotMs;
  cycle.AddPreparePart(yyworker::diagnostic::PreparePart::Probe,value.totalMs);++probePolls_;
  if(value.gpuWaiting)++gpuWaitPolls_;if(!value.submitted&&value.observed)++unsubmittedPolls_;
  // Counts still-unretired observations, not blocked polls: a sealed payload
  // can already be readable here while its original full resource lease lives.
  if(value.FencesReadyObserved()&&!value.retired&&!value.reset&&!value.discarded&&value.submitted&&!value.pending&&!value.recording&&!value.pinned&&!value.faultBlocked)++resetOnlyPolls_;
  // Physical write-once payload may be read before Reset, never before an
  // actual observed fence. Full lease retirement still owns all GPU resources.
  return yyworker::ImmutableReadbackReady(job.sealedSnapshot,reinterpret_cast<uintptr_t>(job.ticket.slot),job.ticket.generation,
   value,job.device&&SUCCEEDED(job.device->GetDeviceRemovedReason()),nrfault033::Blocked());
 }
 void FinishCycle(std::shared_ptr<Readback>& job,yyworker::diagnostic::Cycle& cycle,ReadbackClock::time_point taken,const char* outcome){
  using namespace yyworker::diagnostic;
  const auto source=job->source;const auto enqueued=Ns(job->enqueuedAt);const auto trace=job->trace;
  const auto release=ReadbackClock::now();job.reset();const auto released=ReadbackClock::now();
  cycle.Add(Phase::Release,ReadbackMs(release,released));cycle.Finish(ReadbackMs(taken,released));cycleTotals_.Observe(cycle);
  if(!diagnosticBudget_.Take(cycleTotals_.cycles))return;
  const auto logBegin=ReadbackClock::now();
  auto when=[&](const yyreadbackdiag::ObservedAt& event){return event.present&&event.atNs>=enqueued?double(event.atNs-enqueued)/1e6:-1.;};
  auto phase=[&](Phase phase){return cycle.phase[unsigned(phase)];};
  Log("[033 YY S17 cycle] cycle=%llu outcome=%s final_source=%llu service_ms=%.3f idle_ms=%.3f prepare_all_ms=%.3f init_ms=%.3f convert_ms=%.3f ipc_ms=%.3f publish_ms=%.3f release_ms=%.3f other_ms=%.3f probe_nested_ms=%.3f promote_nested_ms=%.3f sleep_nested_ms=%.3f accounting_invalid=%llu; all active waits including promoted-away jobs, nested values overlap prepare",
   cycleTotals_.cycles,outcome,source.frame,cycle.serviceMs,phase(Phase::QueueWait),phase(Phase::Prepare),phase(Phase::Init),phase(Phase::Convert),phase(Phase::IPC),phase(Phase::Publish),phase(Phase::Release),phase(Phase::Other),cycle.preparePart[unsigned(PreparePart::Probe)],cycle.preparePart[unsigned(PreparePart::Promote)],cycle.preparePart[unsigned(PreparePart::Sleep)],cycle.invalid);
  if(diagnosticBudget_.emitted<LogBudget::Maximum){++diagnosticBudget_.emitted;
   Log("[033 YY S17 lease] source=%llu polls=%llu reasons_seen=0x%X last_reasons=0x%X submit_seen_ms=%.3f fences_ready_seen_ms=%.3f reset_seen_ms=%.3f discard_seen_ms=%.3f retired_seen_ms=%.3f lock_ms=%.3f collect_ms=%.3f snapshot_ms=%.3f; observed after enqueue, -1=not observed, not actual GPU event times",
    source.frame,trace.polls,trace.reasonsSeen,trace.lastReasons,when(trace.seen.submitted),when(trace.seen.fencesReady),when(trace.seen.reset),when(trace.seen.discarded),when(trace.seen.retired),trace.lockMs,trace.collectMs,trace.snapshotMs);
  }
  if(diagnosticBudget_.emitted<LogBudget::Maximum){++diagnosticBudget_.emitted;
   Log("[033 YY S17 totals] cycles=%llu inferred=%llu expired=%llu promotions=%llu probe_polls=%llu reset_only_observed_polls=%llu gpu_wait_polls=%llu unsubmitted_polls=%llu idle_ms=%.3f prepare_all_ms=%.3f init_ms=%.3f convert_ms=%.3f ipc_ms=%.3f publish_ms=%.3f release_ms=%.3f other_ms=%.3f lock_ms=%.3f collect_ms=%.3f snapshot_ms=%.3f previous_diagnostic_log_ms=%.3f accounting_invalid=%llu; polls may repeat a source; reset-only observations are not blocked sealed reads; promotion is not total discarded jobs; current diagnostic logging is counted at the next sample",
    cycleTotals_.cycles,recognitions.load(),expired_,promoted_,probePolls_,resetOnlyPolls_,gpuWaitPolls_,unsubmittedPolls_,cycleTotals_.phase[unsigned(Phase::QueueWait)],cycleTotals_.phase[unsigned(Phase::Prepare)],cycleTotals_.phase[unsigned(Phase::Init)],cycleTotals_.phase[unsigned(Phase::Convert)],cycleTotals_.phase[unsigned(Phase::IPC)],cycleTotals_.phase[unsigned(Phase::Publish)],cycleTotals_.phase[unsigned(Phase::Release)],cycleTotals_.phase[unsigned(Phase::Other)],lockTotalMs_,collectTotalMs_,probeSnapshotMs_,diagnosticLogMs_,cycleTotals_.invalid);
  }
  diagnosticLogMs_+=ReadbackMs(logBegin,ReadbackClock::now());
 }

 // S20: unfeathered person strength into the GPU-time latch ring. The GPU pushes
 // it forward with the motion of its own frame and feathers afterwards.
 // S21: the GPU votes over the three newest publications; here a recognition
 // whose person coverage collapsed (< DropoutRatio of the last published one)
 // is held back, at most MaxHeldRecognitions in a row (policy header).
 // S22: equal grids (the normal landscape case) skip DXL's resampling compose.
 std::vector<uint8_t> lateRgba_;uint64_t latePublished_=0,lateSkipped_=0;
 uint64_t lateRecognitions_=0,lateHeld_=0,lateRecovered_=0,lateGaveUp_=0,lateEmpty_=0,latePresent_=0,lateChange_[5]{},lateDirect_=0;
 // S25: how long each dropout lasted, in held recognitions: 1-2 (S21 already hid
 // these), 3-4 and 5-6 (hidden since S25), or still low after the hold (visible).
 uint64_t lateRuns_[4]{};
 // S27: protagonist flags seen (locked/seen/rescued/missing), recognitions held
 // for a missing protagonist, how long those holds lasted (1-2/3-6/7-12/13-18),
 // and holds that reached the limit with the protagonist still missing.
 uint64_t lateProtagonist_[4]{},lateProtagonistHeld_=0,lateProtagonistRuns_[4]{},lateProtagonistGaveUp_=0;
 bool lateRunIsProtagonist_=false;
 unsigned lateHeldInRow_=0;float lateReference_=0.f;uint64_t latePublishedFrame_=0;
 void LogLateTotals(){
  if(lateRecognitions_!=64&&lateRecognitions_%512)return;
  Log("[033 YY S27 mask vote] recognitions=%llu published=%llu held=%llu recovered_after_hold=%llu published_still_low=%llu empty=%llu person_present=%llu change fell>60%%=%llu fell30-60%%=%llu steady=%llu rose43-150%%=%llu rose>150%%=%llu skipped=%llu direct=%llu dropout_runs 1-2/3-4/5-6/longer=%llu/%llu/%llu/%llu hold_limit=%u; protagonist locked/seen/rescued/missing=%llu/%llu/%llu/%llu protagonist_held=%llu protagonist_runs 1-2/3-6/7-12/13-18=%llu/%llu/%llu/%llu protagonist_gave_up=%llu protagonist_hold_limit=%u; worker side, before the GPU three-vote",
   lateRecognitions_,latePublished_,lateHeld_,lateRecovered_,lateGaveUp_,lateEmpty_,latePresent_,lateChange_[0],lateChange_[1],lateChange_[2],lateChange_[3],lateChange_[4],lateSkipped_,lateDirect_,
   lateRuns_[0],lateRuns_[1],lateRuns_[2],lateRuns_[3],latemask::MaxHeldRecognitions,
   lateProtagonist_[0],lateProtagonist_[1],lateProtagonist_[2],lateProtagonist_[3],lateProtagonistHeld_,
   lateProtagonistRuns_[0],lateProtagonistRuns_[1],lateProtagonistRuns_[2],lateProtagonistRuns_[3],lateProtagonistGaveUp_,latemask::MaxProtagonistHeld);
 }
 void PublishLate(const Recognition& r){
  const unsigned mh=latemask::MaskHeight(r.source.width,r.source.height);
  if(!mh||mh>latemask::MaxMaskHeight||!late::ring.Ensure(device_.Get())){++lateSkipped_;return;}
  lateRgba_.resize(size_t(latemask::MaskWidth)*mh*4);
  const size_t texels=size_t(latemask::MaskWidth)*mh;uint64_t sum=0;
  if(r.mask.width==latemask::MaskWidth&&r.mask.height==mh&&r.mask.coverage.size()==texels&&r.mask.groupIds.size()==texels){
   sum=latemask::DirectPersonStrength(r.mask.coverage.data(),r.mask.groupIds.data(),texels,lateRgba_.data());++lateDirect_;
  }else{
   float strengths[DXL::SEM_GROUP_COUNT]{};strengths[0]=1;
   DXL::ComposeSemanticMask(lateRgba_.data(),latemask::MaskWidth*4,latemask::MaskWidth,mh,&r.mask,0,strengths,1,0.f);
   for(size_t p=0;p<lateRgba_.size();p+=4)sum+=lateRgba_[p];
  }
  // DXL's allocation-failure path writes zeros: never publish a detected person as "no person".
  bool expected=false;
  for(size_t p=0;p<r.mask.coverage.size();++p)if(r.mask.groupIds[p]==0&&r.mask.coverage[p]<128){expected=true;break;}
  if(expected&&!sum){++lateSkipped_;return;}
  ++lateRecognitions_;
  const float coverage=float(double(sum)/(255.0*latemask::MaskWidth*mh));
  if(!sum)++lateEmpty_;else if(coverage>=latemask::MinReferenceCoverage)++latePresent_;
  if(lateReference_>=latemask::MinReferenceCoverage)++lateChange_[latemask::CoverageChangeBucket(coverage,lateReference_)];
  // S27: the worker's protagonist flags; a locked protagonist without any
  // detection on its track is held up to MaxProtagonistHeld, other collapses
  // by the coverage rule (S21/S25).
  const uint32_t pf=r.protagonist;const bool protagonistMissing=yyworker::protagonist::MissingOnTrack(pf);
  if(pf&yyworker::protagonist::Locked){++lateProtagonist_[0];if(pf&yyworker::protagonist::Seen)++lateProtagonist_[1];
   if(pf&yyworker::protagonist::Rescued)++lateProtagonist_[2];if(protagonistMissing)++lateProtagonist_[3];}
  const uint64_t framesSincePublished=latePublishedFrame_&&r.source.frame>latePublishedFrame_?r.source.frame-latePublishedFrame_:0;
  const auto hold=latemask::HoldDecision(coverage,lateReference_,lateHeldInRow_,protagonistMissing,framesSincePublished);
  if(hold!=latemask::HoldKind::None){
   ++lateHeld_;++lateHeldInRow_;
   if(hold==latemask::HoldKind::Protagonist){++lateProtagonistHeld_;lateRunIsProtagonist_=true;}
   if(lateHeld_<=16)Log("[033 YY S27 hold] %s source=%llu coverage=%.4f reference=%.4f in_row=%u; this recognition is held back, the GPU keeps voting over the previous ones",
    hold==latemask::HoldKind::Protagonist?"protagonist":"dropout",r.source.frame,coverage,lateReference_,lateHeldInRow_);
   LogLateTotals();return;
  }
  const bool afterHold=lateHeldInRow_!=0;
  if(late::ring.Publish(lateRgba_.data(),latemask::MaskWidth*4,latemask::MaskWidth,mh,r.source)){
   ++latePublished_;
   if(afterHold){
    if(lateRunIsProtagonist_){++lateProtagonistRuns_[latemask::ProtagonistRunBucket(lateHeldInRow_)];if(protagonistMissing)++lateProtagonistGaveUp_;}
    else if(coverage<lateReference_*latemask::DropoutRatio){++lateGaveUp_;++lateRuns_[3];}
    else{++lateRecovered_;++lateRuns_[latemask::DropoutRunBucket(lateHeldInRow_)];}
   }
   lateReference_=coverage;lateHeldInRow_=0;lateRunIsProtagonist_=false;latePublishedFrame_=r.source.frame;
  }else ++lateSkipped_;
  if(latePublished_==1||(latePublished_<=8192&&(latePublished_&(latePublished_-1))==0))
   Log("[033 YY S22 late mask] published=%llu skipped=%llu held=%llu source=%llu mask=%ux%u coverage=%.4f; GPU votes over the %u newest finished masks at execution time",latePublished_,lateSkipped_,lateHeld_,r.source.frame,latemask::MaskWidth,mh,coverage,latemask::Votes);
  LogLateTotals();
 }
 void Initialize(ID3D12Device* dev){
  copy_=std::make_unique<yycopied::OwnedCopy>(dev);
  auto luid=dev->GetAdapterLuid();int64_t adapter=0;std::memcpy(&adapter,&luid,sizeof adapter);
  client_.Start(SemanticDirectory()/L"033-person-worker.exe",adapter);device_=dev;
 Log("[033 YY semantic S17] private GPU recognition started; adapter=%08lX:%08lX; isolated System32 loader; opset21 GPU graph / CPU fallback forbidden; sealed immutable readback after observed fence, original full lease lifetime retained; first-inference journal in semantic/033-person-worker-*.log; initialization pending",static_cast<unsigned long>(luid.HighPart),luid.LowPart);
 }
 void Loop(){for(;;){
  using namespace yyworker::diagnostic;
  const auto idleBegin=ReadbackClock::now();auto job=jobs_.WaitTake();const auto taken=ReadbackClock::now();
  Cycle cycle;cycle.Add(Phase::QueueWait,ReadbackMs(idleBegin,taken));bool prepareAccounted=false;const char* outcome="prepare_failed";
  if(recognitionStatus.load()<0){FinishCycle(job,cycle,taken,"stopped");continue;} // Drain queued items after permanent failure; no further inference.
  try{
   auto picked=GetTickCount64();auto pickedClock=ReadbackClock::now();
   bool ready=false;
   for(;;){
    // Lift only a newer input with a completed sealed capture fence. An unready newest input never
    // replaces a ready older one, so repeated captures cannot starve inference.
    const auto promoteBegin=ReadbackClock::now();
    const bool promoted=jobs_.PromoteNewestReady(job,[&](const Readback& candidate){
     const auto now=GetTickCount64();
     return now>=candidate.source.capturedMs&&now-candidate.source.capturedMs<=SourceLimitMs&&Probe(candidate,cycle);
    });
    cycle.AddPreparePart(PreparePart::Promote,ReadbackMs(promoteBegin,ReadbackClock::now()));
    if(promoted){++promoted_;picked=GetTickCount64();pickedClock=ReadbackClock::now();}
    const auto now=GetTickCount64();
    if(now<job->source.capturedMs||now-job->source.capturedMs>SourceLimitMs)break;
    if(Probe(*job,cycle)){ready=true;break;}
    const auto sleepBegin=ReadbackClock::now();Sleep(2);cycle.AddPreparePart(PreparePart::Sleep,ReadbackMs(sleepBegin,ReadbackClock::now())); // Same background-only sleep.
   }
   cycle.Add(Phase::Prepare,ReadbackMs(taken,ReadbackClock::now()));prepareAccounted=true;
   if(!ready){
    ++expired_;
    static unsigned skipped=0;if(skipped++<8)Log("[033 YY semantic] sealed readback expired before verified readiness; source=%llu; dropped without mapping or inference",job->source.frame);
    FinishCycle(job,cycle,taken,"expired");continue;
   }
   const auto readableAt=GetTickCount64();const auto readableClock=ReadbackClock::now();
   const auto initBegin=ReadbackClock::now();
   outcome="init_failed";
   if(!device_)Initialize(job->device.Get());
   if(!identity033::Equal(device_.Get(),job->device.Get()))throw std::runtime_error("semantic device changed");
   const auto started=GetTickCount64();const auto convertBegin=ReadbackClock::now();cycle.Add(Phase::Init,ReadbackMs(initBegin,convertBegin));
   outcome="convert_failed";
   yyworker::ReadbackPixels pixels;
   bool incomplete=false,copyExpired=false;double copyMs=0;
   {
    const auto copyBegin=ReadbackClock::now();
    // The exact game Execute fence was checked above. Map ONLY the stable
    // control word; claim/allowed and the DEFAULT payload are never CPU-read.
    if(nrfault033::Blocked()||FAILED(job->device->GetDeviceRemovedReason()))throw std::runtime_error("semantic capture device fault");
    if(!job->sealedSnapshot||!yyonce::ValidLayout(job->sealedLayout)||job->bytes!=job->sealedLayout.bytes||job->layout.Offset!=yyonce::PayloadOffset||job->bytes>yyonce::MaximumBytes||job->bytes<=yyonce::PayloadOffset)throw std::runtime_error("semantic sealed layout");
    if(!job->control)throw std::runtime_error("semantic stable control missing");
    {
     void* mapped=nullptr;D3D12_RANGE range{yyonce::DidRunOffset,yyonce::DidRunOffset+sizeof(uint32_t)};
     Require(job->control->Map(0,&range,&mapped),"semantic stable did-run Map");
     struct Unmap{ID3D12Resource* p;~Unmap(){D3D12_RANGE none{};p->Unmap(0,&none);}} unmap{job->control.Get()};
     job->gridRanObserved=yyonce::DidRun(mapped,yyonce::ControlBytes);incomplete=!job->gridRanObserved;
    }
    if(!incomplete){
     // An expired previous copy retains all its own GPU references until its
     // private fence completes. No allocator/resource is reset while in flight.
     auto status=copy_->Poll(0);
     while(status==yycopied::Status::Pending&&Fresh(*job)){status=copy_->Poll(2);}
     if(status==yycopied::Status::Failed)throw std::runtime_error(copy_->LastFailure().stage);
     copyExpired=!Fresh(*job);
     if(!copyExpired){
      yycopied::SourceCompletion proof;proof.revalidate=&CopySourceReady;proof.context=job.get();
      auto startedCopy=copy_->Start(job->buffer.Get(),yyonce::CompletionOffset,job->bytes,proof);
      while(startedCopy==yycopied::StartResult::SourceNotReady&&Fresh(*job)){
       Sleep(2);startedCopy=copy_->Start(job->buffer.Get(),yyonce::CompletionOffset,job->bytes,proof);
      }
      if(startedCopy==yycopied::StartResult::Failed)throw std::runtime_error(copy_->LastFailure().stage);
      if(startedCopy==yycopied::StartResult::Busy)throw std::runtime_error("semantic owned COPY unexpectedly busy");
      copyExpired=!Fresh(*job)||startedCopy!=yycopied::StartResult::Started;
      if(!copyExpired){
       status=copy_->Poll(0);
       while(status==yycopied::Status::Pending&&Fresh(*job)){status=copy_->Poll(2);}
       if(status==yycopied::Status::Failed)throw std::runtime_error(copy_->LastFailure().stage);
       copyExpired=!Fresh(*job);
       if(!copyExpired){
        if(status!=yycopied::Status::Ready)throw std::runtime_error("semantic owned COPY completion missing");
        if(nrfault033::Blocked()||FAILED(job->device->GetDeviceRemovedReason()))throw std::runtime_error("semantic owned COPY device fault");
        auto mapped=copy_->Take();
        if(mapped.CopiedOffset()!=yyonce::CompletionOffset||mapped.Bytes()!=job->bytes)throw std::runtime_error("semantic owned COPY range mismatch");
        incomplete=!yyonce::CompleteGroups(job->sealedLayout,mapped.Data(),mapped.Bytes());
        const bool half=job->layout.Footprint.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        copyMs=ReadbackMs(copyBegin,ReadbackClock::now());
        if(!incomplete)pixels=yyworker::ConvertReadbackPixels({mapped.Data(),mapped.Bytes(),size_t(job->layout.Offset),job->layout.Footprint.RowPitch,job->inputW,job->inputH,
         half?yyworker::ReadbackPixelFormat::Float16:yyworker::ReadbackPixelFormat::Float32});
       }
      }
     }
     if(copyExpired)copy_->DiscardWhenComplete();
    }
    if(copyMs==0)copyMs=ReadbackMs(copyBegin,ReadbackClock::now());
   }
   const auto converted=ReadbackClock::now();const auto iw=pixels.width,ih=pixels.height;cycle.Add(Phase::Convert,ReadbackMs(convertBegin,converted));
   if(copyExpired||!Fresh(*job)){
    ++expired_;FinishCycle(job,cycle,taken,"owned_copy_expired");continue;
   }
   if(incomplete){
    ++incomplete_;if(incomplete_<=8||(incomplete_<=8192&&(incomplete_&(incomplete_-1))==0))Log("[033 YY semantic S17] sealed capture incomplete; source=%llu discarded=%llu; no inference or publication",job->source.frame,incomplete_);
    FinishCycle(job,cycle,taken,"capture_incomplete");continue;
   }
   outcome="ipc_failed";
   const auto& decoded=client_.Run(pixels.rgba,iw,ih);const auto inferredAt=ReadbackClock::now();cycle.Add(Phase::IPC,ReadbackMs(converted,inferredAt));
   outcome="publish_failed";
   auto next=std::make_shared<Recognition>();next->source=job->source;next->inferenceMs=decoded.inferenceMs;next->protagonist=yyworker::protagonist::Flags(decoded.reserved);
   next->mask.width=iw;next->mask.height=ih;next->mask.version=job->source.frame;next->mask.publishedMs=job->source.capturedMs;
   next->mask.coverage.assign(decoded.coverage,decoded.coverage+size_t(iw)*ih);next->mask.groupIds.assign(decoded.groups,decoded.groups+size_t(iw)*ih);
   // Capture can fail while this IPC finishes. Failure remains terminal;
   // Snapshot suppresses any internally published result after that failure.
   std::atomic_store(&result_,std::shared_ptr<const Recognition>(next));int expectedStatus=0;recognitionStatus.compare_exchange_strong(expectedStatus,1);++recognitions;
   PublishLate(*next); // S20/S21: the GPU votes over the newest finished masks when the frame executes
   cycle.Add(Phase::Publish,ReadbackMs(inferredAt,ReadbackClock::now()));outcome="published";
   if(recognitions<=16 || (recognitions<=8192 && (recognitions.load()&(recognitions.load()-1))==0))Log("[033 YY semantic S17] inferred=%llu provider=%u source=%llu age_ms=%llu prepare_ms=%llu dispatch_ms=%llu readiness_wait_ms=%llu compute_ms=%.1f readback_bytes=%llu capture_build_ms=%.3f queue_ms=%.3f ready_ms=%.3f init_ms=%.3f convert_ms=%.3f copy_path_ms=%.3f ipc_ms=%.3f publish_ms=%.3f; copy_path nested in convert; DEFAULT payload via owned COPY; original lease may remain live; mask pixels not game-accepted",recognitions.load(),decoded.reserved&0xFFu,job->source.frame,GetTickCount64()-job->source.capturedMs,started-job->source.capturedMs,picked-job->source.capturedMs,readableAt-picked,next->inferenceMs,job->bytes,job->captureBuildMs,ReadbackMs(job->enqueuedAt,pickedClock),ReadbackMs(pickedClock,readableClock),ReadbackMs(readableClock,convertBegin),ReadbackMs(convertBegin,converted),copyMs,ReadbackMs(converted,inferredAt),ReadbackMs(inferredAt,ReadbackClock::now()));
  }catch(const std::exception& e){if(!prepareAccounted)cycle.Add(Phase::Prepare,ReadbackMs(taken,ReadbackClock::now()));recognitionStatus=-1;
   note="人物识别失败，分区未生效，详见日志";
   Log("[033 YY semantic] stopped: %s; S17 stage=%s; interrupted stage time is included in prepare_all_ms or other_ms according to stage",e.what(),outcome);}
  FinishCycle(job,cycle,taken,outcome);
 }}
public:
 SemanticWorker(){HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&SemanticDirectory),&pinned))throw std::runtime_error("semantic module pin failed");
  std::thread([this]{Loop();}).detach();}
 bool CanAccept(){return recognitionStatus.load()>=0&&jobs_.HasCapacity();}
 int Status()const{return recognitionStatus.load();}
 bool Submit(const std::shared_ptr<Readback>& job){return recognitionStatus.load()>=0&&jobs_.Submit(job);}
 std::shared_ptr<const Recognition> Snapshot(){const auto snapshot=std::atomic_load(&result_);return recognitionStatus.load()<0?std::shared_ptr<const Recognition>{}:snapshot;}
};
// Pinned module owns the worker until process exit. No UI, Python process or
// detached per-frame jobs; one active job, two pending sources, one published result.
inline SemanticWorker& Worker(){static auto* worker=new SemanticWorker;return *worker;}

}
namespace yanyundual {
inline yyonce::Pipeline sealedCapturePipeline;
inline void Capture(ID3D12Device* dev,ID3D12GraphicsCommandList* list,ID3D12Resource* input,resolveleases::Slot* lease,const FrameIdentity& frame) noexcept {
 try{
 if(!RecognitionRequested(enabled,previewMask.load())||!Worker().CanAccept())return;
 const auto captureBegin=ReadbackClock::now();
 if(!dev||!list||!input||!lease||nrfault033::Blocked()||FAILED(dev->GetDeviceRemovedReason()))return;
 const auto deviceIdentity=identity033::Canonical(dev);
 if(!deviceIdentity||!identity033::Child(deviceIdentity.Get(),list).equal||!identity033::Child(deviceIdentity.Get(),input).equal)throw std::runtime_error("capture input/list/device mismatch");
 const auto desc=input->GetDesc();if(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return;
 const auto grid=DetectorSize(UINT(desc.Width),desc.Height);if(!grid.w)return;
 if(sealedCapturePipeline.device&&!identity033::Equal(sealedCapturePipeline.device.Get(),dev))throw std::runtime_error("capture device changed");
 sealedCapturePipeline.Prepare(sealedCapturePipeline.device?sealedCapturePipeline.device.Get():dev);
 // All optional-interface and platform capability checks precede recording.
 const auto seal=sealedCapturePipeline.PrepareSeal(list);
 if(!identity033::Child(deviceIdentity.Get(),seal.list.Get()).equal)throw std::runtime_error("capture seal device mismatch");
 // S26: the view heap comes from a pool and is reused after this lease retires.
 auto* views=yyheappool::captureHeaps.Acquire(sealedCapturePipeline.device.Get(),yyonce::ViewHeapDesc(),lease);
 if(!views)return; // every pooled heap still in flight: no recognition for this frame
 const auto capture=sealedCapturePipeline.Create(input,views);
 if(!identity033::Child(deviceIdentity.Get(),capture.control.Get()).equal||!identity033::Child(deviceIdentity.Get(),capture.buffer.Get()).equal||!identity033::Child(deviceIdentity.Get(),capture.views.Get()).equal)throw std::runtime_error("capture resource device mismatch");
 auto read=std::make_shared<Readback>();read->device=dev;read->source=frame;read->inputW=grid.w;read->inputH=grid.h;
 const auto& layout=capture.layout;
 if(layout.width!=grid.w||layout.height!=grid.h)throw std::runtime_error("capture detector dimensions changed");
 read->control=capture.control;read->buffer=capture.buffer;read->bytes=layout.bytes;read->layout.Offset=yyonce::PayloadOffset;
 read->layout.Footprint={desc.Format,layout.width,layout.height,1,layout.rowPitch};
 read->sealedLayout=layout;read->sealedSnapshot=true;
 read->ticket=resolveleases::GetTicket(lease);
 if(!read->ticket.slot||!read->ticket.generation)return;
 for(auto* object:{static_cast<IUnknown*>(input),static_cast<IUnknown*>(capture.control.Get()),static_cast<IUnknown*>(capture.buffer.Get()),static_cast<IUnknown*>(capture.views.Get()),static_cast<IUnknown*>(sealedCapturePipeline.root.Get()),static_cast<IUnknown*>(sealedCapturePipeline.claim.Get()),static_cast<IUnknown*>(sealedCapturePipeline.grid.Get())})if(!resolveleases::Hold(lease,object))return;
 // There are no fallible allocations, interface queries or lease additions
 // after the first command. Input state and original full lease are unchanged.
 sealedCapturePipeline.RecordSealed(seal,capture);
 // Arm only this exact source and ticket. Submission rejection drops the CPU
 // job; all recorded GPU objects remain held by the original complete lease.
 read->enqueuedAt=ReadbackClock::now();read->captureBuildMs=ReadbackMs(captureBegin,read->enqueuedAt);
 Worker().Submit(read);
 }catch(const std::exception& e){note="人物识别输入准备失败";
  if(recognitionStatus.exchange(-1)>=0)Log("[033 YY semantic S17] capture stopped: %s; no retry, mask output disabled until process restart",e.what());}
 catch(...){note="人物识别输入异常";
  if(recognitionStatus.exchange(-1)>=0)Log("[033 YY semantic S17] capture stopped: unknown exception; no retry, mask output disabled until process restart");}
}
}

#pragma pop_macro("max")
#pragma pop_macro("min")
