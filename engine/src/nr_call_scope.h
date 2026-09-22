#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
namespace nrdispatch {
struct Context { int depth=0; bool after=false; uintptr_t caller=0; };
inline thread_local Context context;
inline std::atomic_flag writer=ATOMIC_FLAG_INIT;
inline std::atomic<bool> gap{false};
inline std::atomic<unsigned> skipped{0};
inline std::atomic<unsigned> non_image_calls{0};
inline std::atomic<unsigned> unknown_calls{0};
inline std::atomic<unsigned> framegen_calls{0};
// Output can be left over in a shared NGX parameter block after SR. It cannot
// identify a feature: only a successful CreateFeature ledger entry can do that.
// Unknown/FG evaluations never acquire the writer or invalidate SR history.
inline bool image_candidate(int feature, bool /*hasOutput*/) {
    return feature == 1 || feature == 13; // 1 = DLSS SR, 13 = ray reconstruction; 12 is DeepDVC
}
struct DepthScope {
    uintptr_t saved;
    explicit DepthScope(uintptr_t caller):saved(context.caller){
        if(context.depth++==0)context.caller=caller;
    }
    ~DepthScope(){--context.depth;context.caller=saved;}
};
// One NR instance owns shared working textures. Never block the game's render
// thread behind another frame callback, and never record two writes concurrently.
// 2026-09-12 (Witcher 3 DX12): maintenance on the present thread (control/park/
// build pumps, panel reads, the 5 s lifetime log) holds the writer for
// microseconds to a few milliseconds. A frame that lost to it skipped NR (raw
// DLSS shown) and the next frame reset history: a slight flash. A frame now waits
// a bounded moment, and only for maintenance. Frames never wait for another
// frame, and a model build marks itself Long so frames still skip it at once.
enum class Holder : unsigned char { None, Frame, Maintenance, Long };
inline std::atomic<Holder> holder{Holder::None};
inline std::atomic<unsigned> waited{0};
// Why a frame lost the writer, so one session names the contender.
inline std::atomic<unsigned> waited_frame{0},skipped_reentrant{0},skipped_frame{0},skipped_build{0};
inline constexpr std::chrono::milliseconds MaintenanceGrace{12};
// None is also the instant between another thread's acquire and its holder store.
// 2026-09-12 (Dragon's Dogma 2, Onimusha): with the game's frame generation on, the
// writer is lost to another frame holder, not to maintenance. An NR stage costs
// 0.5-1.5 ms of CPU, so a frame now waits the same bounded moment for another
// frame as for maintenance. Only a model build is skipped at once.
inline bool frame_may_wait(Holder current){return current!=Holder::Long;}
inline bool acquire(Holder kind){
    if(writer.test_and_set(std::memory_order_acquire))return false;
    holder.store(kind,std::memory_order_release);return true;
}
inline bool acquire_frame(){
    if(acquire(Holder::Frame))return true;
    Holder seen=holder.load(std::memory_order_acquire);
    const auto deadline=std::chrono::steady_clock::now()+MaintenanceGrace;
    while(frame_may_wait(seen)){
        std::this_thread::yield();
        if(acquire(Holder::Frame)){if(seen==Holder::Frame)++waited_frame;else ++waited;return true;}
        if(std::chrono::steady_clock::now()>=deadline)break;
        seen=holder.load(std::memory_order_acquire);
    }
    if(seen==Holder::Long)++skipped_build;else ++skipped_frame;
    return false;
}
inline void release(){holder.store(Holder::None,std::memory_order_release);writer.clear(std::memory_order_release);}
struct AfterScope {
    bool entered=false;
    AfterScope(){
        if(context.after){gap.store(true);++skipped;++skipped_reentrant;return;}
        if(acquire_frame()){entered=true;context.after=true;return;}
        gap.store(true);++skipped;
    }
    ~AfterScope(){if(entered){context.after=false;release();}}
};
// Internal NR stages can borrow their caller's writer. Maintenance competes for
// the same state without pretending a missed maintenance tick is a dropped SR
// frame. A standalone Feeder stage requests real-frame gap accounting instead.
struct WriterAccess {
    bool entered=false,owned=false;
    explicit WriterAccess(bool frame=false){
        if(context.after){entered=true;return;}
        if(frame?acquire_frame():acquire(Holder::Maintenance)){
            entered=true;owned=true;context.after=true;
        }else if(frame){gap.store(true);++skipped;}
    }
    ~WriterAccess(){if(owned){context.after=false;release();}}
};
// A long step (model build) under the writer this thread holds: frames skip it at
// once instead of waiting out the grace. end() restores the previous holder.
struct LongStep {
    bool active=false;Holder saved=Holder::None;
    LongStep(){if(context.after){saved=holder.exchange(Holder::Long,std::memory_order_acq_rel);active=true;}}
    LongStep(const LongStep&)=delete;LongStep& operator=(const LongStep&)=delete;
    void end(){if(active){holder.store(saved,std::memory_order_release);active=false;}}
    ~LongStep(){end();}
};
inline bool consume_gap(){return gap.exchange(false);}
}
