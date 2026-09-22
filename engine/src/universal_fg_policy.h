#pragma once
#include <cstdint>
namespace ufgpolicy033 {
inline constexpr unsigned MaxGenerated=2;
inline unsigned Multiplier(unsigned m){return m==3?3:2;}
inline unsigned Count(unsigned requested,unsigned capacity){auto n=Multiplier(requested)-1;return capacity<n?0:n;}
inline float Phase(unsigned index,unsigned count){return float(index+1)/float(count+1);}
inline unsigned OutputBase(uint64_t frame){return unsigned(frame%2)*MaxGenerated;}
inline double PresentInterval(double realInterval,unsigned presentCount){return realInterval/double(presentCount?presentCount:1);}
}
