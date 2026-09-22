#pragma once
#include <cstdint>
namespace nrclarity {
// One optional output cache; old textures remain owned by immutable frame leases.
// Neither this cache nor a strength edit owns/rebuilds any NR model or history.
template<class Resource,class Device> struct Cache {
    Resource* resource=nullptr;Device* device=nullptr;
    unsigned width=0,height=0;int format=0;uint64_t retryAfter=0;
    template<class Create,class Release>
    Resource* Prepare(Device* d,unsigned w,unsigned h,int f,bool enabled,uint64_t now,Create create,Release release){
        const bool same=resource&&device==d&&width==w&&height==h&&format==f;
        if(!enabled){if(resource)release(resource);resource=nullptr;device=nullptr;retryAfter=0;return nullptr;}
        if(same)return resource;
        if(now<retryAfter)return nullptr;
        Resource* fresh=nullptr;
        if(!create(fresh)){if(fresh)release(fresh);retryAfter=now+1000;return nullptr;}
        if(resource)release(resource);
        resource=fresh;device=d;width=w;height=h;format=f;retryAfter=0;return resource;
    }
};
template<class R,class ToRead,class Dispatch,class ToWrite>
R Finish(R source,R scratch,ToRead read,Dispatch dispatch,ToWrite write){
    if(!scratch||scratch==source)return source;
    read(source);dispatch(source,scratch);write(source);return scratch;
}
}
