// S16 audit candidate: CPU-only accounting. It cannot admit a frame or retire a lease.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace yyworker { namespace diagnostic {
enum class Phase : unsigned {QueueWait,Prepare,Init,Convert,IPC,Publish,Release,Other,Count};
enum class PreparePart : unsigned {Probe,Sleep,Promote,Count};
constexpr unsigned PhaseCount=unsigned(Phase::Count),PreparePartCount=unsigned(PreparePart::Count);
inline void Increment(uint64_t& value)noexcept {if(value!=UINT64_MAX)++value;}
inline bool Accumulate(double& total,double ms)noexcept {
 if(!std::isfinite(ms)||ms<0||total>(std::numeric_limits<double>::max)()-ms)return false;
 total+=ms;return true;
}
// Source age is NOT inferred here. QueueWait is idle wait before a job arrives;
// serviceMs starts after WaitTake and includes all canceled/promotion work.
// PreparePart values are nested observations, NEVER added to primary totals.
struct Cycle {
 std::array<double,PhaseCount> phase{};
 std::array<double,PreparePartCount> preparePart{};
 double serviceMs=0;
 uint64_t invalid=0;
 bool finished=false;
 bool Add(Phase which,double ms)noexcept {
  const unsigned i=unsigned(which);
  if(finished||i>=PhaseCount||which==Phase::Other||!Accumulate(phase[i],ms)){Increment(invalid);return false;}
  return true;
 }
 bool AddPreparePart(PreparePart which,double ms)noexcept {
  const unsigned i=unsigned(which);
  if(finished||i>=PreparePartCount||!Accumulate(preparePart[i],ms)){Increment(invalid);return false;}
  return true;
 }
 bool Finish(double elapsedAfterTakeMs)noexcept {
  if(finished){Increment(invalid);return false;}finished=true;
  if(!std::isfinite(elapsedAfterTakeMs)||elapsedAfterTakeMs<0){Increment(invalid);return false;}
  serviceMs=elapsedAfterTakeMs;
  double primary=0;
  for(unsigned i=unsigned(Phase::Prepare);i<unsigned(Phase::Other);++i)
   if(!Accumulate(primary,phase[i])){Increment(invalid);return false;}
  // Tolerance is floating arithmetic only; it never changes 100ms admission.
  const double tolerance=1e-9*(serviceMs>1?serviceMs:1);
  if(primary>serviceMs+tolerance){Increment(invalid);return false;}
  phase[unsigned(Phase::Other)]=serviceMs>primary?serviceMs-primary:0;
  return true;
 }
};
struct Totals {
 std::array<double,PhaseCount> phase{};
 std::array<double,PreparePartCount> preparePart{};
 uint64_t cycles=0,invalid=0;
 void Observe(const Cycle& cycle)noexcept {
  Increment(cycles);if(!cycle.finished||cycle.invalid)Increment(invalid);
  for(unsigned i=0;i<PhaseCount;++i)if(!Accumulate(phase[i],cycle.phase[i]))Increment(invalid);
  for(unsigned i=0;i<PreparePartCount;++i)if(!Accumulate(preparePart[i],cycle.preparePart[i]))Increment(invalid);
 }
};
inline bool Sampled(uint64_t ordinal)noexcept {
 return ordinal>0&&ordinal<=8192&&(ordinal<=16||(ordinal&(ordinal-1))==0);
}
// Additional S16 cycle/summary records share this worker-owned finite budget.
// Existing S15 inferred logging is separate and unchanged.
struct LogBudget {
 static constexpr unsigned Maximum=128;
 unsigned emitted=0;
 bool Take(uint64_t ordinal)noexcept {if(!Sampled(ordinal)||emitted>=Maximum)return false;++emitted;return true;}
};
}}
