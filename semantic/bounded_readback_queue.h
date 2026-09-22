// One worker owns its active item; this queue owns a fixed ONE or TWO pending items.
// A pending source keeps its original stamp; only fully retired newer inputs may supersede older work.
#pragma once
#include <condition_variable>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
namespace yyworker {
template<class T,size_t PendingCapacity=2> class BoundedReadbackQueue {
 static_assert(PendingCapacity>=1&&PendingCapacity<=2,"one active plus at most two pending captures");
 mutable std::mutex mutex_;
 std::condition_variable available_;
 std::array<std::shared_ptr<T>,PendingCapacity> pending_{};
 size_t first_=0,count_=0;
 std::shared_ptr<T> Pop(){auto item=std::move(pending_[first_]);first_=(first_+1)%PendingCapacity;--count_;return item;}
public:
 bool HasCapacity()const {std::lock_guard<std::mutex> lock(mutex_);return count_<PendingCapacity;}
 bool Submit(std::shared_ptr<T> item){
  if(!item)return false;
  {std::lock_guard<std::mutex> lock(mutex_);if(count_==PendingCapacity)return false;pending_[(first_+count_)%PendingCapacity]=std::move(item);++count_;}
  available_.notify_one();return true;
 }
 std::shared_ptr<T> WaitTake(){
  std::unique_lock<std::mutex> lock(mutex_);available_.wait(lock,[&]{return count_!=0;});
  return Pop();
 }
 // Only the worker consumes. Readiness can inspect full GPU retirement, so
 // evaluate it OUTSIDE the queue mutex; the render producer must not wait for
 // fence polling/COM release while submitting a small CPU queue item.
 template<class Ready>bool PromoteNewestReady(std::shared_ptr<T>& active,Ready ready){
  std::array<std::shared_ptr<T>,PendingCapacity> seen{};size_t count=0;
  {std::lock_guard<std::mutex> lock(mutex_);count=count_;for(size_t i=0;i<count;++i)seen[i]=pending_[(first_+i)%PendingCapacity];}
  std::shared_ptr<T> chosen;
  for(size_t i=count;i>0;--i)if(ready(*seen[i-1])){chosen=seen[i-1];break;}
  if(!chosen)return false;
  std::shared_ptr<T> canceled;
  {std::lock_guard<std::mutex> lock(mutex_);
   // Producer may have appended an item since the snapshot; keep anything
   // newer than the exact chosen pointer. Never remap a stamp to another job.
   size_t index=0;while(index<count_&&pending_[(first_+index)%PendingCapacity]!=chosen)++index;
   if(index==count_)return false;
   // Change the sole active job before exposing newly free pending slots.
   // Canceled references are released after unlock; their GPU leases survive.
   canceled=std::move(active);active=chosen;
   for(size_t i=0;i<=index;++i)Pop();
  }
  // seen[] holds removed references through the unlock. Old GPU resources
  // remain held independently by their original full lease even when dropped.
  return true;
 }
 // Nonblocking seam also makes the scheduler's exact queue CPU-testable.
 std::shared_ptr<T> TryTake(){std::lock_guard<std::mutex> lock(mutex_);return count_?Pop():std::shared_ptr<T>{};}
};
enum class FreshReadback {Ready,Expired,ClockInvalid};
template<class Complete,class Clock,class Pause>
FreshReadback AwaitFreshReadback(Complete complete,Clock clock,Pause pause,uint64_t capturedMs){
 for(;;){
  const uint64_t now=clock();
  if(now<capturedMs)return FreshReadback::ClockInvalid;
  if(now-capturedMs>100)return FreshReadback::Expired;
  if(complete())return FreshReadback::Ready; // Existing FULL Completed(ticket) only.
  pause(2); // Recognition thread only; no render/GPU queue waits.
 }
}
}
