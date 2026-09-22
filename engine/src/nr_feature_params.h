#pragma once
#include <cstddef>
namespace nrlayers {
// NR callers serialize creation/evaluation/release through their existing
// render ownership scope. No allocation, driver call or implicit destruction.
// Three active + three candidate features fit; retired banks gate new builds.
template<class Params,std::size_t Capacity=6> struct FeatureParams {
    struct Entry {void* feature=nullptr;Params* params=nullptr;};
    Entry entries[Capacity]{};
    Entry* Find(void* feature){if(feature)for(auto& e:entries)if(e.feature==feature)return &e;return nullptr;}
    Entry* Free(){for(auto& e:entries)if(!e.feature && !e.params)return &e;return nullptr;}
    bool Any()const{for(const auto& e:entries)if(e.feature || e.params)return true;return false;}
};
}
