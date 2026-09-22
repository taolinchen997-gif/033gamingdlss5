#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <initializer_list>
#include "nr_feature_policy.h"

// Persisted model requests, independent of the UI's currently selected layer.
namespace nrlayers {
struct Model {
    float intensity=1,structure=1,tone=0,skin=1,globalTone=0;
    int style=0,preset=0,autoMask=1,uiCorrect=1;
};
inline Model Neutral(int pass){Model m;m.intensity=m.structure=m.skin=1.f/(pass+1.f);return m;}
template<class C> Model Get(const C& c,int pass){
    if(pass>0 && pass<nrfeatures::MaxPasses)return c.extra[pass-1];
    return {c.intensity,c.local_structure,c.local_tone,c.skin_structure,c.global_tone,c.style,c.preset,c.auto_mask,c.ui_correct};
}
inline bool Equal(const Model& a,const Model& b){
    return a.intensity==b.intensity && a.structure==b.structure && a.tone==b.tone && a.skin==b.skin &&
        a.globalTone==b.globalTone && a.style==b.style && a.preset==b.preset && a.autoMask==b.autoMask && a.uiCorrect==b.uiCorrect;
}
inline uint32_t Signature(const Model& m){
    uint32_t h=2166136261u;auto mix=[&](uint32_t v){h=(h^v)*16777619u;};
    // Preserve every float bit: tiny edits must not be marked applied without a build.
    for(float v:{m.intensity,m.structure,m.tone,m.skin,m.globalTone}){uint32_t bits;std::memcpy(&bits,&v,4);mix(bits);}
    for(int v:{m.style,m.preset,m.autoMask,m.uiCorrect})mix(uint32_t(v));return h;
}
template<class C> uint32_t Signature(const C& c,bool all=false){
    uint32_t h=2166136261u;const int count=all?nrfeatures::MaxPasses:nrfeatures::ClampPasses(c.passes);
    for(int i=0;i<count;++i)h=(h^Signature(Get(c,i)))*16777619u;return h;
}
template<class C> bool Matches(const C& built,const C& requested,int count){
    for(int i=0;i<count;++i)if(!Equal(Get(built,i),Get(requested,i)))return false;return true;
}
// Missing legacy keys are migrated after the entire file has been read. The
// old auto-mask/UI choices applied to every layer; copy them ONCE, not on edit.
struct Reader {
    Model extra[2]={Neutral(1),Neutral(2)};uint32_t seen[2]={};
    bool Read(const char* key,const char* value){
        if(std::strlen(key)<=9 || std::strncmp(key,"nrlayer",7) || (key[7]!='2' && key[7]!='3') || key[8]!='.')return false;
        const int layer=key[7]-'0';const char* field=key+9;int index=-1;
        const char* names[]={"intensity","structure","tone","skin","globaltone","style","preset","automask","uicorrect"};
        for(int i=0;i<9;++i)if(!std::strcmp(field,names[i]))index=i;
        if(index<0)return false;
        char* end=nullptr;const float v=std::strtof(value,&end);
        while(end && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t'))++end;
        if(end==value || !end || *end || !std::isfinite(v))return true;
        const float lo=index==3?-1.f:0.f,hi=index<5?2.f:(index<7?3.f:1.f);
        if(v<lo || v>hi || (index>=5 && v!=std::floor(v)))return true;
        auto& m=extra[layer-2];
        switch(index){case 0:m.intensity=v;break;case 1:m.structure=v;break;case 2:m.tone=v;break;case 3:m.skin=v;break;
            case 4:m.globalTone=v;break;case 5:m.style=int(v);break;case 6:m.preset=int(v);break;case 7:m.autoMask=int(v);break;case 8:m.uiCorrect=int(v);break;}
        seen[layer-2]|=1u<<index;return true;
    }
    template<class C> void Finish(C& c)const{
        for(int i=0;i<2;++i){c.extra[i]=extra[i];if(!(seen[i]&(1u<<7)))c.extra[i].autoMask=c.auto_mask;
            if(!(seen[i]&(1u<<8)))c.extra[i].uiCorrect=c.ui_correct;}
    }
};
template<class C> void Save(FILE* f,const C& c){
    std::fprintf(f,"nrlayersversion=1\n");
    for(int pass=1;pass<nrfeatures::MaxPasses;++pass){const auto m=Get(c,pass);const int l=pass+1;
        std::fprintf(f,"nrlayer%d.intensity=%.9g\nnrlayer%d.structure=%.9g\nnrlayer%d.tone=%.9g\nnrlayer%d.skin=%.9g\nnrlayer%d.globaltone=%.9g\n",l,m.intensity,l,m.structure,l,m.tone,l,m.skin,l,m.globalTone);
        std::fprintf(f,"nrlayer%d.style=%d\nnrlayer%d.preset=%d\nnrlayer%d.automask=%d\nnrlayer%d.uicorrect=%d\n",l,m.style,l,m.preset,l,m.autoMask,l,m.uiCorrect);
    }
}
// Retiring a candidate or old bank must never destroy a borrowed live object.
template<class T> void ExcludeShared(T*& retired,T* live){if(retired==live)retired=nullptr;}
}
