#pragma once
// S20 late-latch ring: finished person masks for the GPU to pick at execution time.
#include "yanyun_dual_policy.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
#include <immintrin.h>
#include <d3d12.h>
#include <wrl/client.h>
namespace yanyundual::late {
// Persistently mapped UPLOAD buffer. Only the recognition worker writes it; the
// GPU reads the index word when the frame EXECUTES and then that slot. A slot
// is rewritten only after Slots-1 newer publications (~300 ms at ~22 per s),
// while a frame reads its latched slot within the same few dispatches, so a
// slot never changes under a reader. The index word is one aligned 32-bit
// store made after an sfence, so the GPU sees either the old or the new slot
// and the header seq must match it. The buffer is never released: command lists
// in flight may reference it until the process ends.
class Ring {
 std::mutex mutex_;
 Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
 uint8_t* mapped_=nullptr;
 std::atomic<ID3D12Resource*> ready_{nullptr};
 uint32_t seq_=0;unsigned last_=latemask::Slots-1;
 std::vector<uint8_t> line_;
public:
 ID3D12Resource* Buffer() const {return ready_.load(std::memory_order_acquire);}
 bool Ensure(ID3D12Device* dev) noexcept {
  if(ready_.load(std::memory_order_acquire))return true;
  std::lock_guard<std::mutex> lock(mutex_);
  if(buffer_)return true;
  if(!dev)return false;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=latemask::RingBytes;desc.Height=1;
  desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
  if(FAILED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&buffer))))return false;
  void* mapped=nullptr;D3D12_RANGE none{};
  if(FAILED(buffer->Map(0,&none,&mapped))||!mapped)return false;
  std::memset(mapped,0,latemask::ControlBytes); // index 0 = nothing published
  _mm_sfence();
  buffer_=buffer;mapped_=static_cast<uint8_t*>(mapped);line_.resize(latemask::Pitch);
  ready_.store(buffer_.Get(),std::memory_order_release);
  return true;
 }
 // Worker thread only. rgba: R = person strength (unfeathered), rgbaPitch bytes per row.
 bool Publish(const uint8_t* rgba,unsigned rgbaPitch,unsigned maskW,unsigned maskH,const FrameIdentity& source) noexcept {
  if(!mapped_||!rgba||maskW!=latemask::MaskWidth||!maskH||maskH>latemask::MaxMaskHeight||rgbaPitch<maskW*4)return false;
  const unsigned slot=(last_+1)%latemask::Slots;
  if(++seq_>=(1u<<29))seq_=1;
  uint8_t* base=mapped_+latemask::ControlBytes+size_t(slot)*latemask::SlotStride;
  const latemask::SlotHeader header{latemask::Magic,seq_,uint32_t(source.frame),uint32_t(source.capturedMs),uint32_t(source.capturedMs>>32),
   uint32_t(source.stream),uint32_t(source.stream>>32),uint32_t(source.generation),uint32_t(source.generation>>32),
   source.width,source.height,maskW,maskH,latemask::Pitch,seq_,0};
  std::memcpy(base,&header,sizeof header);
  for(unsigned y=0;y<maskH;++y){
   const uint8_t* row=rgba+size_t(y)*rgbaPitch;
   for(unsigned x=0;x<maskW;++x)line_[x]=row[x*4];
   std::memcpy(base+latemask::HeaderBytes+size_t(y)*latemask::Pitch,line_.data(),latemask::Pitch);
  }
  _mm_sfence(); // every slot byte is globally visible before the index flips
  *reinterpret_cast<volatile uint32_t*>(mapped_)=latemask::EncodeIndex(seq_,slot);
  _mm_sfence();
  last_=slot;return true;
 }
};
inline Ring ring;
}
