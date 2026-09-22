#pragma once
#include <atomic>
#include <cstdint>
namespace srnotice033 {
// One visible message across viewports/feature recreation during a resize.
// This changes notification frequency only, never the API failure or log.
class Gate {
    std::atomic<uint64_t> next{0};
public:
    bool Notify(uint64_t now){
        auto due=next.load();
        return now>=due && next.compare_exchange_strong(due,now+10000u);
    }
};
}
