#pragma once
#include <stdint.h>
namespace k033format {
// DXGI numeric wire identifiers. BGRX is required by common old-game buffers;
// keep it a distinct format, never relabel X8 memory as BGRA for CopyResource.
constexpr int Count=5;
inline int index(uint32_t f){return f==28?0:f==87?1:f==24?2:f==10?3:f==88?4:-1;}
inline bool supported(uint32_t f){return index(f)>=0;}
inline bool color(uint32_t c,uint32_t f){return c<=3&&supported(f)&&(c!=2||f==24)&&(c!=3||f==10);}
}
