#pragma once
#include "backend.h"
namespace k033 {
inline bool inline_grade_same(const K033_Settings& a,const K033_Settings& b) {
    return a.enabled==b.enabled&&a.style==b.style&&a.exposure==b.exposure&&a.contrast==b.contrast&&
        a.saturation==b.saturation&&a.warmth==b.warmth&&a.tint==b.tint&&a.highlights==b.highlights&&a.style_strength==b.style_strength;
}
// A same-scene grade operation has no model/NR/depth/MV dependency. Resource
// ownership and the native command-list state envelope belong to the caller.
// All preparation and validation finish before the first command is recorded.
template<class IO> int inline_grade_record(IO& io,const K033_Settings& settings) {
    if(!valid(settings))return K033_INVALID;
    if(!needs_grade(grade(settings)))return K033_BYPASS;
    int ready=io.validate();if(ready!=K033_OK)return ready;
    try {
        if(!io.arm())return K033_BYPASS; // final epoch check before retention/copy
        io.capture();
        io.grade(settings);
        io.copyback();
        return K033_OK;
    } catch(...) {io.failed();return K033_BACKEND_ERROR;}
}
}
