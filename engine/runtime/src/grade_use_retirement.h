#pragma once
#include "beta2_grade_policy.h"
#include <mutex>
namespace k033 {
// Exact ownership/retirement implementation shared with the native Use.
// Completion means the original lease already proved GPU done AND no replay.
// No destroy/resize event is itself a completion certificate.
template<class Source,class Api,class Ticket> struct GradeUseRetirement {
    Source source;Api api;Ticket ticket;
    mutable std::mutex mutex;bool armed=false,returned=false,done=false,recorded=false;
    bool possibly_recorded()const noexcept {std::lock_guard<std::mutex> lock(mutex);return armed;}
    int retire()noexcept {
        std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return K033_BUSY;
        returned=true;if(done)return K033_OK;
        if(!beta2grade::may_retire(armed,ticket.slot!=nullptr,ticket.slot&&api.completed(ticket)))return K033_BUSY;
        source={};done=true;return K033_OK;
    }
    bool finished(){std::lock_guard<std::mutex> lock(mutex);return done;}
};
}
