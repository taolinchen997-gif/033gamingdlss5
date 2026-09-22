#pragma once
#include "../src/backend.h"
#include "nr_settings.h"
#include <mutex>
#include <cstring>
namespace k033settings {
// One newest immutable snapshot, not an unbounded queue of slider events.
// Producer uses try_lock; writer drops the lock before filesystem operations.
struct GradePreferences {
    using Value=K033_Settings;static Value defaults(){return k033::defaults();}
    static bool valid(const Value& v){return k033::valid(v);}static Value normalize(const Value& v){return v;}
};
struct NrPreferences {
    using Value=K033_NrSettings;static Value defaults(){return k033::nr_defaults(false);}
    static bool valid(const Value& v){return k033::nr_settings(v);}static Value normalize(const Value& v){return k033::nr_persistent(v);}
};
template<class Traits>class PreferencesMailboxT {
    using Value=typename Traits::Value;
    std::mutex mutex;
    Value latest=Traits::defaults();
    uint64_t revision=0,saved=0,writing=0;
    int result=K033_BYPASS;
    Value external=Traits::defaults();bool seen_external=false,incoming=false;
public:
    int offer(const Value& value){
        if(!Traits::valid(value))return K033_INVALID;
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return K033_BUSY;
        if(revision==UINT64_MAX)return K033_UNSUPPORTED;
        latest=Traits::normalize(value);++revision;incoming=false;return K033_OK;
    }
    bool take(Value& value,uint64_t& ticket){
        std::lock_guard<std::mutex> lock(mutex);
        if(writing||saved==revision)return false;
        value=latest;ticket=writing=revision;return true;
    }
    bool complete(uint64_t ticket,int outcome){
        std::lock_guard<std::mutex> lock(mutex);
        if(!ticket||ticket!=writing)return false;
        if(outcome==K033_OK){saved=ticket;seen_external=false;incoming=false;}
        result=outcome;writing=0;return true;
    }
    int status(uint64_t& queued,uint64_t& persisted,int& last){
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return K033_BUSY;
        queued=revision;persisted=saved;last=result;return K033_OK;
    }
    // Writer publishes a shared-file change only when no local edit is pending.
    // An accepted local offer invalidates older unread external snapshots.
    bool publish_external(const Value& value){
        if(!Traits::valid(value))return false;
        std::lock_guard<std::mutex> lock(mutex);
        if(writing||revision!=saved)return false;
        auto normalized=Traits::normalize(value);
        if(seen_external&&!std::memcmp(&external,&normalized,sizeof(normalized)))return true;
        external=normalized;seen_external=true;incoming=true;return true;
    }
    int receive(Value& value){
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return K033_BUSY;
        if(writing||revision!=saved||!incoming)return K033_BYPASS;
        value=external;latest=value;incoming=false;return K033_OK;
    }
};
using PreferencesMailbox=PreferencesMailboxT<GradePreferences>;
using NrPreferencesMailbox=PreferencesMailboxT<NrPreferences>;
}
