#pragma once
// S29: where the next recognition runs (worker only; the core just reads the
// provider code). The GPU (DirectML) pass takes 5-25 ms while the GPU has room,
// but in the S27b and S28 sessions it went to 100-250 ms once the game filled
// the GPU (in the S28 session one pass in three at first, then most of them),
// and neither a high-priority queue (S27b) nor a high GPU scheduling class (S28)
// brought it back. The same model on the CPU (4 threads) takes ~45-60 ms
// whatever the game does with the GPU.
//  - Start on the GPU.
//  - Move to the CPU when, among the last Window GPU passes (at least
//    MinGpuRuns of them), SlowPassesToMove took SlowPassMs or more and their
//    mean is MarginMs worse than the CPU's (measured, else InitialCpuMs).
//  - On the CPU, send one recognition to the GPU every ProbeEveryMs; come back
//    after QuickProbesToReturn probes in a row under QuickProbeMs.
// Pure bookkeeping: no clocks, threads or graphics here.
#include <cmath>
#include <cstdint>
namespace yyroute {
enum class Where : uint8_t {Gpu=0,Cpu=1};
inline constexpr double Alpha=.25,InitialCpuMs=50,SlowPassMs=60,MarginMs=10,QuickProbeMs=25,MaxMs=60000;
inline constexpr unsigned Window=8,MinGpuRuns=4,SlowPassesToMove=3,QuickProbesToReturn=2;
inline constexpr uint64_t ProbeEveryMs=2000;
static_assert(SlowPassesToMove<=MinGpuRuns&&MinGpuRuns<=Window,"route window");
// Provider code in bits 0..7 of the worker reply: 2 = GPU (since S10), 1 = CPU.
inline constexpr uint32_t ProviderGpu=2,ProviderCpu=1;
inline uint32_t Provider(Where w){return w==Where::Cpu?ProviderCpu:ProviderGpu;}
// CPU pass histogram bucket: <25 / 25-50 / 50-100 / >=100 ms.
inline unsigned Bucket(double ms){return ms<25?0u:ms<50?1u:ms<100?2u:3u;}
struct Route {
 Where mode=Where::Gpu;
 double window[Window]{};unsigned filled=0,next=0;
 double cpuEma=InitialCpuMs,lastProbe=0;bool cpuKnown=false;
 unsigned quickProbes=0;
 uint64_t lastProbeMs=0,gpuRuns=0,cpuRuns=0,probes=0,toCpu=0,toGpu=0;
 double GpuMean()const{double s=0;for(unsigned i=0;i<filled;++i)s+=window[i];return filled?s/filled:0;}
 unsigned SlowPasses()const{unsigned n=0;for(unsigned i=0;i<filled;++i)n+=window[i]>=SlowPassMs;return n;}
 // Where the next recognition runs; in CPU mode a due probe goes to the GPU.
 Where Next(uint64_t nowMs,bool cpuAvailable)const{
  if(mode!=Where::Cpu||!cpuAvailable)return Where::Gpu;
  return nowMs-lastProbeMs>=ProbeEveryMs?Where::Gpu:Where::Cpu;
 }
 // Records one finished recognition; true when the mode changed.
 bool Done(Where ran,double ms,uint64_t nowMs,bool cpuAvailable){
  if(!std::isfinite(ms)||ms<0||ms>MaxMs)ms=MaxMs; // garbage counts as slow, never as quick
  if(ran==Where::Cpu){++cpuRuns;cpuEma=cpuKnown?cpuEma+(ms-cpuEma)*Alpha:ms;cpuKnown=true;return false;}
  ++gpuRuns;
  if(mode==Where::Cpu){ // a probe
   ++probes;lastProbeMs=nowMs;lastProbe=ms;quickProbes=ms<QuickProbeMs?quickProbes+1:0;
   if(!cpuAvailable||quickProbes>=QuickProbesToReturn){Enter(Where::Gpu,nowMs);++toGpu;return true;}
   return false;
  }
  window[next]=ms;next=(next+1)%Window;if(filled<Window)++filled;
  if(cpuAvailable&&filled>=MinGpuRuns&&SlowPasses()>=SlowPassesToMove&&GpuMean()>=cpuEma+MarginMs){Enter(Where::Cpu,nowMs);++toCpu;return true;}
  return false;
 }
 // The CPU session failed: GPU only from now on.
 void CpuLost(){if(mode==Where::Cpu){Enter(Where::Gpu,lastProbeMs);++toGpu;}}
private:
 void Enter(Where w,uint64_t nowMs){mode=w;quickProbes=0;if(w==Where::Gpu){filled=next=0;}else lastProbeMs=nowMs;}
};
}
