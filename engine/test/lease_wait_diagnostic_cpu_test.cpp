#include "../src/lease_wait_diagnostic.h"
#include <cstdio>
#include <thread>
#include <vector>
using namespace leasewait033;
static unsigned checks=0,failed=0;
static void Check(bool b){++checks;if(!b)++failed;}
int main(){
    // All gate combinations preserve the original boolean admission and the
    // first rejection. In particular, first entry A cannot admit entry B.
    for(unsigned s=0;s<2;++s)for(unsigned r=0;r<2;++r)for(unsigned same=0;same<2;++same){
        const auto result=Admission(s,r,11,same?11:22);
        Check((result==Reason::Ready)==bool(s&&r&&same));
        Check(result==(!s?Reason::SubmitMissing:!r?Reason::ResetMissing:!same?Reason::ResetDifferent:Reason::Ready));
    }
    Recorder rec;
    rec.Record(Owner::Grade,Reason::ResetDifferent,100,200,300);
    rec.Record(Owner::NR,Reason::HeapFailed,101,201,301,0x8007000e);
    Check(rec.Count(Owner::NR,Reason::ResetDifferent)==0);
    Check(rec.Count(Owner::Grade,Reason::ResetDifferent)==1);
    Check(rec.samples[2][1].published.load()==2&&rec.samples[2][1].result==0x8007000e);
    // Concurrent callers cannot tear the first pointer/result tuple or lose
    // aggregate counts; ready is never reported as a refusal.
    std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([&,t]{for(unsigned i=0;i<10000;++i){
        rec.Record(Owner::NR,Reason::ResetDifferent,t,t+100,t+200,t+300);
        rec.Record(Owner::NR,Reason::Ready);
    }});
    for(auto& t:threads)t.join();
    Check(rec.Count(Owner::NR,Reason::ResetDifferent)==80000);
    Check(rec.Count(Owner::NR,Reason::Ready)==0);
    const auto& sample=rec.samples[2][0];
    Check(sample.published.load(std::memory_order_acquire)==2);
    Check(sample.target==sample.command+100&&sample.entry==sample.command+200&&sample.result==sample.command+300);
    Check(rec.samples[1][0].command==100&&rec.samples[1][0].target==200&&rec.samples[1][0].entry==300);
    Check(rec.samples[0][0].published.load()==0);
    // Y4: the appended presentation owner keeps its own counters and sample,
    // and does not disturb the three owners recorded by earlier evidence.
    Check(rec.Count(Owner::GradePresent,Reason::ResetDifferent)==0);
    Check(rec.samples[3][0].published.load()==0);
    rec.Record(Owner::GradePresent,Reason::ResetDifferent,400,500,600);
    rec.Record(Owner::GradePresent,Reason::PoolFull);
    Check(rec.Count(Owner::GradePresent,Reason::ResetDifferent)==1);
    Check(rec.Count(Owner::GradePresent,Reason::PoolFull)==1);
    Check(rec.samples[3][0].published.load()==2&&rec.samples[3][0].command==400&&
          rec.samples[3][0].target==500&&rec.samples[3][0].entry==600);
    Check(rec.Count(Owner::Grade,Reason::ResetDifferent)==1);
    Check(rec.Count(Owner::NR,Reason::ResetDifferent)==80000);
    Check(rec.samples[1][0].command==100&&rec.samples[2][1].result==0x8007000e);
    // Out-of-range owners and Ready are still discarded rather than written.
    rec.Record(Owner::Count,Reason::ResetDifferent,1,2,3);
    rec.Record(Owner::GradePresent,Reason::Count);
    Check(rec.Count(Owner::GradePresent,Reason::ResetDifferent)==1);
    std::printf("lease refusal diagnostic: %u checks, %u failures; CPU only\n",checks,failed);
    return failed?1:0;
}
