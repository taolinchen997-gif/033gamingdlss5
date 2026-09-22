#include "../src/reset_entry_owner_policy.h"
#include <cstdio>
using namespace leasewait033;
using resetentryowner033::MayClaim;
static unsigned checks=0,failed=0;
static void Check(bool b){++checks;if(!b)++failed;}
int main(){
    // With NR enabled exactly one owner is refused the claim: the ReShade
    // presentation list. Every game-side owner keeps today's behaviour.
    for(unsigned o=0;o<Owners;++o){
        const auto owner=Owner(o);
        Check(MayClaim(owner,false));
        Check(MayClaim(owner,true)==(owner!=Owner::GradePresent));
    }
    Check(!MayClaim(Owner::GradePresent,true));
    Check(MayClaim(Owner::GradePresent,false));
    Check(MayClaim(Owner::NR,true)&&MayClaim(Owner::Grade,true)&&MayClaim(Owner::Other,true));

    // The policy only decides who may publish an install request. It must not
    // become an admission gate: a refused claimer whose list happens to match
    // the installed entry is still admitted, and a permitted claimer whose
    // list differs is still refused. Both come from Admission(), unchanged.
    Check(Admission(true,true,0xAB,0xAB)==Reason::Ready);
    Check(Admission(true,true,0xAB,0xCD)==Reason::ResetDifferent);
    Check(Admission(false,true,0xAB,0xAB)==Reason::SubmitMissing);
    Check(Admission(true,false,0xAB,0xAB)==Reason::ResetMissing);

    // Owner indices are stable: appending GradePresent must not renumber the
    // owners already written into saved evidence (Other 0, Grade 1, NR 2).
    Check(unsigned(Owner::Other)==0&&unsigned(Owner::Grade)==1&&unsigned(Owner::NR)==2);
    Check(unsigned(Owner::GradePresent)==3&&Owners==4);

    std::printf("reset entry owner policy: %u checks, %u failures; CPU only\n",checks,failed);
    return failed?1:0;
}
