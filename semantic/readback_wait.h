// Background delivery only. Readiness means the EXISTING full lease retired,
// never merely that its first Execute or one queue's fence finished.
#pragma once
#include <cstdint>
namespace yyworker {
enum class ReadbackWait {Ready,TimedOut};
template<class Complete,class Clock,class Pause>
ReadbackWait AwaitReadback(Complete complete,Clock clock,Pause pause,uint64_t timeoutMs=2000){
 const uint64_t begin=clock();
 for(;;){
  if(complete())return ReadbackWait::Ready;
  const uint64_t now=clock();
  if(now<begin||now-begin>=timeoutMs)return ReadbackWait::TimedOut;
  pause(2); // Only the recognition thread sleeps; render callbacks never wait.
 }
}
}
