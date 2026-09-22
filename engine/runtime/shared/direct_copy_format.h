#pragma once
#include <stdint.h>
namespace k033 {
// Storage and typed view stay separate through BOTH parent and worker copies.
inline bool direct_copy_storage(unsigned plane,uint32_t storage,uint32_t view){
    if(plane==1)return view==41&&(storage==39||storage==41);
    if(plane==2)return (view==16||view==34)&&storage==view;
    if(plane==3)return view==41&&storage==41;
    return (plane==0||plane==4)&&view==10&&storage==10;
}
}
