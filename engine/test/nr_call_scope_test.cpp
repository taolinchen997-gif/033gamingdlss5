#include "../src/nr_call_scope.h"
#include <thread>
#include <chrono>
#include <cstdio>
int main(){int checks=0,failures=0;
    auto check=[&](bool v,const char*n){++checks;if(!v){++failures;std::printf("FAIL %s\n",n);}};
    {
        nrdispatch::DepthScope outer(123);
        check(nrdispatch::context.depth==1&&nrdispatch::context.caller==123,"outer caller");
        {nrdispatch::DepthScope inner(456);check(nrdispatch::context.depth==2&&nrdispatch::context.caller==123,"nested core preserves outer caller");}
        check(nrdispatch::context.depth==1,"nested depth restored");
        std::atomic<bool> separate{false};
        std::thread worker([&]{separate=(nrdispatch::context.depth==0&&!nrdispatch::context.after&&nrdispatch::context.caller==0);});worker.join();
        check(separate,"other render thread is independent");
        nrdispatch::AfterScope writer;
        check(writer.entered&&nrdispatch::context.after,"writer acquired");
        std::atomic<bool> skipped{false};
        std::thread contender([&]{nrdispatch::AfterScope second;skipped=!second.entered&&!nrdispatch::context.after;});contender.join();
        check(skipped,"concurrent writer skips without waiting");
    }
    check(nrdispatch::context.depth==0&&!nrdispatch::context.after&&nrdispatch::context.caller==0,"scope restores reentry state");
    check(nrdispatch::consume_gap()&&!nrdispatch::consume_gap(),"skipped callback invalidates next history once");
    {nrdispatch::AfterScope again;check(again.entered,"writer reusable after scope");}
    check(!nrdispatch::image_candidate(-1,false),"unknown frame-generation call without image is not a missing SR frame");
    check(!nrdispatch::image_candidate(11,true),"known frame-generation call cannot run NR even with stale output");
    check(nrdispatch::image_candidate(1,false)&&nrdispatch::image_candidate(13,false),"known SR/RR failure still invalidates its history");
    check(!nrdispatch::image_candidate(12,false),"DeepDVC (feature 12) is not an image upscaler for NR");
    check(!nrdispatch::image_candidate(-1,true),"unknown handles cannot borrow stale SR output to run NR");
    {nrdispatch::AfterScope busy;std::atomic<bool> ignored{false};std::thread fg([&]{
        if(nrdispatch::image_candidate(-1,false)){nrdispatch::AfterScope unexpected;}
        else ignored=true;
    });fg.join();check(ignored&&!nrdispatch::consume_gap(),"concurrent non-image FG callback does not mark NR history as dropped");}
    {nrdispatch::AfterScope busy;
        {nrdispatch::WriterAccess nested;check(nested.entered&&!nested.owned,"internal stage borrows its already-owned writer");}
        std::atomic<bool> withheld{false};std::thread maintenance([&]{nrdispatch::WriterAccess access;withheld=!access.entered;});maintenance.join();
        check(withheld&&!nrdispatch::consume_gap(),"busy maintenance cannot falsely reset a valid SR history");
        std::thread feeder([&]{nrdispatch::WriterAccess access(true);withheld=!access.entered;});feeder.join();
        check(withheld&&nrdispatch::consume_gap(),"a skipped standalone Feeder frame does invalidate history");
    }
    {nrdispatch::WriterAccess access;check(access.entered&&access.owned,"maintenance writer is reusable after outer frame");}
    // 2026-09-12 writer wait: present-thread maintenance holds the writer briefly; a frame arriving then waits for it.
    check(nrdispatch::frame_may_wait(nrdispatch::Holder::Maintenance)&&nrdispatch::frame_may_wait(nrdispatch::Holder::Frame)&&
          !nrdispatch::frame_may_wait(nrdispatch::Holder::Long),"frames wait for maintenance and for another frame, never for a model build");
    {const unsigned before=nrdispatch::skipped_frame.load();std::atomic<bool> entered{true};
        nrdispatch::AfterScope busy;
        std::thread frame([&]{nrdispatch::AfterScope scope;entered=scope.entered;});frame.join();
        check(!entered&&nrdispatch::skipped_frame.load()==before+1,"a frame that loses to another frame is counted as skip_frame");
        check(nrdispatch::consume_gap(),"that dropped frame still invalidates history once");
    }
    {const unsigned before=nrdispatch::skipped_reentrant.load();
        nrdispatch::AfterScope outer;
        {nrdispatch::AfterScope inner;check(!inner.entered,"a re-entrant frame callback does not take the writer twice");}
        check(nrdispatch::skipped_reentrant.load()==before+1,"re-entrant drops are counted apart from lock contention");
        nrdispatch::consume_gap();
    }
    {std::atomic<bool> started{false},entered{false};std::thread frame;
        {nrdispatch::WriterAccess maintenance;
            check(maintenance.owned&&nrdispatch::holder.load()==nrdispatch::Holder::Maintenance,"maintenance marks the writer holder");
            frame=std::thread([&]{started=true;nrdispatch::AfterScope scope;entered=scope.entered;});
            while(!started)std::this_thread::yield();
            const auto spin=std::chrono::steady_clock::now();
            while(std::chrono::steady_clock::now()-spin<std::chrono::microseconds(500))std::this_thread::yield();
        }
        frame.join();
        check(entered&&!nrdispatch::consume_gap(),"a frame waits out a short maintenance hold instead of dropping NR");
        check(nrdispatch::holder.load()==nrdispatch::Holder::None,"the frame released the writer holder");
    }
    {nrdispatch::WriterAccess maintenance;std::atomic<bool> entered{true};std::chrono::steady_clock::duration spent{};
        std::thread frame([&]{const auto begin=std::chrono::steady_clock::now();{nrdispatch::AfterScope scope;entered=scope.entered;}spent=std::chrono::steady_clock::now()-begin;});
        frame.join();
        check(!entered&&nrdispatch::consume_gap(),"maintenance longer than the grace still drops that frame and resets history once");
        check(spent>=nrdispatch::MaintenanceGrace,"the frame gives up only after the whole grace");
    }
    {nrdispatch::WriterAccess maintenance;std::atomic<bool> entered{true};
        nrdispatch::LongStep build;
        check(build.active&&nrdispatch::holder.load()==nrdispatch::Holder::Long,"a model build marks the writer long");
        std::thread frame([&]{nrdispatch::AfterScope scope;entered=scope.entered;});frame.join();
        check(!entered&&nrdispatch::consume_gap(),"frames skip a model build and reset history as before");
        build.end();
        check(nrdispatch::holder.load()==nrdispatch::Holder::Maintenance,"ending the build restores the maintenance holder");
    }
    {nrdispatch::AfterScope frame;
        {nrdispatch::LongStep nested;check(nrdispatch::holder.load()==nrdispatch::Holder::Long,"a long step inside a frame marks the writer long");}
        check(nrdispatch::holder.load()==nrdispatch::Holder::Frame,"the long step restores the frame holder");
    }
    {nrdispatch::LongStep outside;check(!outside.active&&nrdispatch::holder.load()==nrdispatch::Holder::None,"a long step without the writer changes nothing");}
    std::printf("frames served by waiting: %u\n",nrdispatch::waited.load());
    std::printf("NR callback scopes: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
