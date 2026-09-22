// 033 CPU readback-to-detector pixels. No GPU objects or inference dependency.
// The half LUT reproduces the prior std::ldexp + finite-check + clamp/round
// formula exactly, including rejecting every infinity/NaN before byte casting.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace yyworker {
enum class ReadbackPixelFormat { Float16, Float32 };
struct ReadbackPixelView {
 const void* data=nullptr;
 size_t bytes=0,offset=0,rowPitch=0;
 unsigned width=0,height=0;
 ReadbackPixelFormat format=ReadbackPixelFormat::Float16;
};
struct ReadbackPixels {
 unsigned width=0,height=0;
 std::vector<uint8_t> rgba;
};
namespace readback_detail {
inline float HalfForTable(uint16_t bits){
 const int e=(bits>>10)&31;const unsigned m=bits&1023;
 const float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),e-25);
 return bits&32768?-v:v;
}
inline uint8_t FiniteByte(float value){
 return uint8_t((std::clamp)(value,0.f,1.f)*255.f+.5f);
}
}
inline const std::array<int16_t,65536>& HalfReadbackByteTable(){
 // One thread-safe initialization; table is immutable thereafter. -1 is the
 // exact previous nonfinite rejection, never a replacement black pixel.
 static const auto values=[](){
  std::array<int16_t,65536> table{};
  for(unsigned bits=0;bits<table.size();++bits){
   const auto value=readback_detail::HalfForTable(uint16_t(bits));
   table[bits]=std::isfinite(value)?int16_t(readback_detail::FiniteByte(value)):int16_t(-1);
  }
  return table;
 }();
 return values;
}
inline ReadbackPixels ConvertReadbackPixels(const ReadbackPixelView& view){
 if(!view.data||!view.width||!view.height||view.width>8192||view.height>8192)
  throw std::runtime_error("invalid semantic readback dimensions");
 const bool half=view.format==ReadbackPixelFormat::Float16;
 if(!half&&view.format!=ReadbackPixelFormat::Float32)
  throw std::runtime_error("invalid semantic readback format");
 const size_t pixelBytes=half?8u:16u;
 const size_t rowBytes=size_t(view.width)*pixelBytes;
 if(view.rowPitch<rowBytes||view.offset>view.bytes)
  throw std::runtime_error("invalid semantic readback layout");
 const size_t precedingRows=size_t(view.height)-1;
 if(precedingRows&&view.rowPitch>((std::numeric_limits<size_t>::max)()-rowBytes)/precedingRows)
  throw std::runtime_error("semantic readback span overflow");
 const size_t required=precedingRows*view.rowPitch+rowBytes;
 if(required>view.bytes-view.offset)
  throw std::runtime_error("semantic readback span truncated");

 const double ratio=640.0/(std::max)(view.width,view.height);
 ReadbackPixels out;
 out.width=(std::max)(1u,unsigned(view.width*ratio+.5));
 out.height=(std::max)(1u,unsigned(view.height*ratio+.5));
 out.rgba.assign(size_t(out.width)*out.height*4,255);
 const auto* table=half?HalfReadbackByteTable().data():nullptr;
 for(unsigned y=0;y<out.height;++y){
  const unsigned sy=(std::min)(view.height-1,unsigned(uint64_t(y)*view.height/out.height));
  const auto* row=static_cast<const uint8_t*>(view.data)+view.offset+size_t(sy)*view.rowPitch;
  for(unsigned x=0;x<out.width;++x){
   const unsigned sx=(std::min)(view.width-1,unsigned(uint64_t(x)*view.width/out.width));
   for(unsigned c=0;c<3;++c){
    uint8_t value;
    if(half){
     uint16_t bits;std::memcpy(&bits,row+size_t(sx)*pixelBytes+c*2,sizeof(bits));
     const int16_t decoded=table[bits];
     if(decoded<0)throw std::runtime_error("nonfinite semantic input");
     value=uint8_t(decoded);
    }else{
     float decoded;std::memcpy(&decoded,row+size_t(sx)*pixelBytes+c*4,sizeof(decoded));
     if(!std::isfinite(decoded))throw std::runtime_error("nonfinite semantic input");
     value=readback_detail::FiniteByte(decoded);
    }
    out.rgba[(size_t(y)*out.width+x)*4+c]=value;
   }
  }
 }
 return out;
}
}
