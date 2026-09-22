#pragma once
// S26: the per-frame YanYun passes take their shader-visible descriptor heaps
// from small pools instead of creating one per pass per frame.
// Why: the 2026-09-22 02:26 hang dump shows a game render thread looping
// forever in the ReShade host's unregister_descriptor_heap (its registry map
// was corrupt) while 033's lease retirement released the last reference to one
// of those per-frame heaps. Every heap made through the game device is a host
// proxy, and the host registry is not safe against heaps being created on one
// thread and destroyed on another; the partition made and dropped three per
// real frame (motion ring, blend, recognition capture).
// A pooled heap is created once, handed out again only after the lease that
// last used it has retired (the GPU is done with its descriptors), and never
// destroyed: a device or layout change parks the old set, and process exit
// releases nothing (raw references, like the lease slots).
// Include after resolve_leases.h and d3d12_identity.h (tests use stand-ins).
#include <d3d12.h>
#include <mutex>
#include <vector>
namespace yyheappool {
// Heaps in flight stay far below this (lease high_water is 6); a full pool
// skips that pass for one frame instead of growing without bound.
inline constexpr unsigned MaxHeaps=24;
struct Stats {unsigned long long created=0,reused=0,full=0,failed=0,parked=0;};
class Pool {
public:
 // A heap of exactly this layout for a pass whose lease does not exist yet.
 // nullptr: every heap is still in flight, or creation failed; skip the pass.
 ID3D12DescriptorHeap* Reserve(ID3D12Device* dev,const D3D12_DESCRIPTOR_HEAP_DESC& desc) noexcept {
  if(!dev)return nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  if(!device_||!identity033::Equal(device_,dev)||!Same(desc_,desc)){Park();dev->AddRef();device_=dev;desc_=desc;}
  for(auto& e:entries_)if(!e.reserved&&(!e.ticket.slot||resolveleases::Completed(e.ticket))){e.reserved=true;e.ticket={};++stats_.reused;return e.heap;}
  if(entries_.size()>=MaxHeaps){++stats_.full;return nullptr;}
  ID3D12DescriptorHeap* heap=nullptr;
  if(FAILED(dev->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)))||!heap){++stats_.failed;return nullptr;}
  entries_.push_back({heap,{},true});++stats_.created;return heap;
 }
 // The reserved heap is used by the GPU work this lease covers.
 void Bind(ID3D12DescriptorHeap* heap,resolveleases::Ticket ticket) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  for(auto& e:entries_)if(e.heap==heap&&e.reserved){e.reserved=false;e.ticket=ticket;return;}
 }
 // No command used the reservation: the heap is free again at once.
 void Unreserve(ID3D12DescriptorHeap* heap) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  for(auto& e:entries_)if(e.heap==heap&&e.reserved){e.reserved=false;e.ticket={};return;}
 }
 // Reserve and Bind for a pass whose lease already exists.
 ID3D12DescriptorHeap* Acquire(ID3D12Device* dev,const D3D12_DESCRIPTOR_HEAP_DESC& desc,resolveleases::Slot* lease) noexcept {
  const auto ticket=resolveleases::GetTicket(lease);
  if(!ticket.slot||!ticket.generation)return nullptr;
  auto* heap=Reserve(dev,desc);if(heap)Bind(heap,ticket);return heap;
 }
 Stats Snapshot() const noexcept {std::lock_guard<std::mutex> lock(mutex_);return stats_;}
 size_t Size() const noexcept {std::lock_guard<std::mutex> lock(mutex_);return entries_.size();}
private:
 struct Entry {ID3D12DescriptorHeap* heap=nullptr;resolveleases::Ticket ticket{};bool reserved=false;};
 static bool Same(const D3D12_DESCRIPTOR_HEAP_DESC& a,const D3D12_DESCRIPTOR_HEAP_DESC& b) noexcept {
  return a.Type==b.Type&&a.NumDescriptors==b.NumDescriptors&&a.Flags==b.Flags&&a.NodeMask==b.NodeMask;
 }
 // Lists in flight may still reference the old set: keep every heap alive.
 void Park(){stats_.parked+=entries_.size();parked_.insert(parked_.end(),entries_.begin(),entries_.end());entries_.clear();}
 mutable std::mutex mutex_;
 ID3D12Device* device_=nullptr; // one reference, never released
 D3D12_DESCRIPTOR_HEAP_DESC desc_{};
 std::vector<Entry> entries_,parked_;
 Stats stats_;
};
// A reservation that never gets its lease (early return, exception) is released.
struct Reservation {
 Pool* pool=nullptr;ID3D12DescriptorHeap* heap=nullptr;
 Reservation(Pool& p,ID3D12DescriptorHeap* h) noexcept:pool(&p),heap(h){}
 Reservation(const Reservation&)=delete;Reservation& operator=(const Reservation&)=delete;
 void Bind(resolveleases::Ticket ticket) noexcept {if(heap&&pool){pool->Bind(heap,ticket);pool=nullptr;}}
 ~Reservation(){if(heap&&pool)pool->Unreserve(heap);}
};
inline Pool motionHeaps,blendHeaps,captureHeaps;
// Next to the GPU vote line: pool sizes must stay small while reuse grows.
inline void LogPools(){
 const Pool* pools[]={&motionHeaps,&blendHeaps,&captureHeaps};
 size_t size[3];Stats s[3];
 for(unsigned i=0;i<3;++i){size[i]=pools[i]->Size();s[i]=pools[i]->Snapshot();}
 Log("[033 YY S26 heap pools] motion/blend/capture heaps=%zu/%zu/%zu created=%llu/%llu/%llu reused=%llu/%llu/%llu full=%llu/%llu/%llu failed=%llu/%llu/%llu parked=%llu/%llu/%llu; each heap is reused after its lease retired and never destroyed",
  size[0],size[1],size[2],s[0].created,s[1].created,s[2].created,s[0].reused,s[1].reused,s[2].reused,s[0].full,s[1].full,s[2].full,
  s[0].failed,s[1].failed,s[2].failed,s[0].parked,s[1].parked,s[2].parked);
}
}
