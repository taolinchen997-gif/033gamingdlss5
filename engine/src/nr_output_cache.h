#pragma once
#include <utility>

// Output colour format is not the NR model format. Keep at most two output
// pairs per bank so HDR/SDR toggles need neither another feature nor retirement.
// Ownership stays with the bank; submitted command-list leases stay untouched.
namespace nroutput {
struct Geometry {
    const void* device; unsigned width,height,guideWidth,guideHeight;
    int work,full,modelFormat;
};
inline bool Compatible(const Geometry& a,const Geometry& b) {
    return a.device==b.device && a.width==b.width && a.height==b.height &&
        a.guideWidth==b.guideWidth && a.guideHeight==b.guideHeight &&
        a.work==b.work && a.full==b.full && a.modelFormat==b.modelFormat;
}
enum class Result { Unchanged, Allocated, Cached, Capacity, Failed };
template<class Resource,class Format,class Create,class Release>
Result Switch(Resource*& full,Resource*& result,Format& format,
              Resource*& spareFull,Resource*& spareResult,Format& spareFormat,
              Format wanted,Create create,Release release) {
    if(format==wanted)return Result::Unchanged;
    if(spareFull || spareResult) {
        if(!spareFull || !spareResult || spareFormat!=wanted)return Result::Capacity;
        std::swap(full,spareFull);std::swap(result,spareResult);std::swap(format,spareFormat);
        return Result::Cached;
    }
    Resource *nextFull=nullptr,*nextResult=nullptr;
    if(!create(nextFull,nextResult,wanted) || !nextFull || !nextResult) {
        // Only these never-submitted allocations can be released immediately.
        if(nextFull)release(nextFull);if(nextResult)release(nextResult);
        return Result::Failed;
    }
    spareFull=full;spareResult=result;spareFormat=format;
    full=nextFull;result=nextResult;format=wanted;
    return Result::Allocated;
}
}
