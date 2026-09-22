#pragma once
#include "../include/033_runtime.h"
// Persistent user intent. None of these values prove generation or display.
struct K033_Beta2FgSettings {
    uint32_t size=sizeof(K033_Beta2FgSettings),version=1;
    uint32_t route=0,automatic=1,native_multiplier=0,universal_multiplier=2,universal_enabled=0;
    uint32_t native_enabled=1,max_count=4,temporal_fix=1,force_flip_meter_off=0,raise_ceiling=0,force_ota=0;
};
namespace k033beta2 {
inline K033_Beta2FgSettings fg_defaults(){return {};}
inline bool fg_valid(const K033_Beta2FgSettings& s){
    return s.size==sizeof(s)&&s.version==1&&s.route<=1&&s.automatic<=1&&
        (s.native_multiplier==0||(s.native_multiplier>=2&&s.native_multiplier<=6))&&
        (s.universal_multiplier==2||s.universal_multiplier==3)&&s.universal_enabled<=1&&
        s.native_enabled<=1&&s.max_count>=2&&s.max_count<=5&&s.temporal_fix<=1&&
        s.force_flip_meter_off<=1&&s.raise_ceiling<=1&&s.force_ota<=1;
}
}
