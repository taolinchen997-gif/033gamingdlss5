#pragma once
#include <cstdint>
namespace monitor033 {
struct GpuSample {bool memoryValid=false,loadValid=false,temperatureValid=false,boardMemoryValid=false;uint64_t used=0,budget=0;unsigned load=0;int temperature=0;uint64_t boardUsed=0,boardTotal=0;char name[128]{};};
extern "C" GpuSample K033_MonitorGpu(uint64_t adapterLuid) noexcept;
struct PresentationRate {
 uint64_t qpc=0;uint32_t count=0;double fps=0;bool valid=false;
 void Sample(bool available,uint32_t total,uint64_t stamp,uint64_t frequency){
  valid=false;if(!available||!frequency||!stamp){qpc=0;return;}
  if(qpc&&stamp>qpc&&total>=count){const double seconds=double(stamp-qpc)/frequency;
   if(seconds>=.05&&seconds<=10){const double next=double(total-count)/seconds;if(next>=0&&next<=2000){fps=next;valid=true;}}}
  qpc=stamp;count=total;
 }
};
struct Rate {
 uint64_t tick=0,count=0;double value=0;bool valid=false;
 void Sample(uint64_t now,uint64_t total){
  if(!tick||now<tick||total<count){tick=now;count=total;valid=false;return;}
  const auto span=now-tick;if(span<1000)return;
  value=double(total-count)*1000.0/double(span);valid=true;tick=now;count=total;
 }
};
}
