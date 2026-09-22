#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace yyonce {
constexpr uint32_t ClaimOffset=0,AllowedOffset=4,CompletionOffset=256,PayloadOffset=32768;
constexpr uint32_t DidRunOffset=256,ControlBytes=512;
// The immutable marker occupies a different 256-byte region from replay writes.
// A true marker is only meaningful after the exact observed Execute fence.
inline bool DidRun(const void* control,size_t bytes) noexcept {
 if(!control||bytes<ControlBytes)return false;
 uint32_t value=0;std::memcpy(&value,static_cast<const uint8_t*>(control)+DidRunOffset,sizeof(value));
 return value==1;
}
constexpr uint32_t MaximumGroups=80u*80u,InvalidCompletedGroups=UINT32_MAX;
static_assert(CompletionOffset+MaximumGroups*sizeof(uint32_t)<=PayloadOffset,"completion flags must not overlap pixels");
constexpr uint64_t MaximumBytes=8ull*1024*1024;
enum class PixelFormat : uint32_t {Float16=8,Float32=16};
struct Layout {
 uint32_t sourceWidth=0,sourceHeight=0,width=0,height=0,rowPitch=0,pixelBytes=0,groupsX=0,groupsY=0;
 uint64_t bytes=0;
 uint32_t ExpectedGroups()const noexcept{return groupsX*groupsY;}
};
inline Layout MakeLayout(uint32_t sw,uint32_t sh,PixelFormat format) noexcept {
 Layout value{};const auto stride=uint32_t(format);
 if(!sw||!sh||sw>8192||sh>8192||(stride!=8&&stride!=16))return value;
 const double scale=640.0/(std::max)(sw,sh);
 value.sourceWidth=sw;value.sourceHeight=sh;
 value.width=(std::max)(1u,uint32_t(sw*scale+.5));value.height=(std::max)(1u,uint32_t(sh*scale+.5));
 value.pixelBytes=stride;value.rowPitch=(value.width*stride+255u)&~255u;
 value.groupsX=(value.width+7u)/8u;value.groupsY=(value.height+7u)/8u;
 if(value.ExpectedGroups()>MaximumGroups)return {};
 value.bytes=PayloadOffset+uint64_t(value.rowPitch)*value.height;
 if(value.bytes>MaximumBytes)return {};
 return value;
}
inline bool ValidLayout(const Layout& value) noexcept {
 const auto expected=MakeLayout(value.sourceWidth,value.sourceHeight,PixelFormat(value.pixelBytes));
 return expected.bytes&&value.width==expected.width&&value.height==expected.height&&value.rowPitch==expected.rowPitch&&
  value.groupsX==expected.groupsX&&value.groupsY==expected.groupsY&&value.bytes==expected.bytes;
}
// This only validates image completeness. It is NOT GPU/CPU synchronization.
inline uint32_t CountCompletedGroups(const Layout& value,const void* buffer,size_t bytes) noexcept {
 if(!ValidLayout(value)||!buffer||bytes<PayloadOffset||bytes<value.bytes)return InvalidCompletedGroups;
 const auto* flags=static_cast<const uint8_t*>(buffer)+CompletionOffset;
 uint32_t completed=0;
 for(uint32_t group=0;group<value.ExpectedGroups();++group){
  uint32_t flag=0;std::memcpy(&flag,flags+size_t(group)*sizeof(flag),sizeof(flag));
  if(flag>1)return InvalidCompletedGroups;
  completed+=flag;
 }
 return completed;
}
inline bool CompleteGroups(const Layout& value,const void* buffer,size_t bytes) noexcept {
 const auto completed=CountCompletedGroups(value,buffer,bytes);
 return completed!=InvalidCompletedGroups&&completed==value.ExpectedGroups();
}
// Harness compatibility only: completed must come from CountCompletedGroups,
// never from a single GPU word. Production uses CompleteGroups directly.
inline bool CompleteCount(const Layout& value,uint32_t completed) noexcept {
 return ValidLayout(value)&&completed!=InvalidCompletedGroups&&completed==value.ExpectedGroups();
}
}
