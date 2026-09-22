#include <cstdio>
#include "../runtime/src/beta2_grade_policy.h"
#include "../src/render_core_policy.h"
static int checks=0,failed=0;
static void check(bool value,const char* name){++checks;if(!value){++failed;std::printf("FAIL %s\n",name);}}
struct IO {
    int failure=0,calls=0;bool valid=true,current=true,armed=false,poisoned=false;
    int validate(){return valid?K033_OK:K033_BYPASS;}
    bool arm(){if(!current)return false;armed=true;return true;}
    void op(int phase){++calls;if(failure==phase)throw phase;}
    void capture(){op(1);}void grade(const K033_Settings&){op(2);}void copyback(){op(3);}void failed(){poisoned=true;}
};
int main(){
    auto settings=k033::defaults();IO neutral;
    check(k033::inline_grade_record(neutral,settings)==K033_BYPASS,"neutral bypass");
    check(!neutral.armed&&neutral.calls==0,"neutral no recording");
    settings.exposure=.2f;const bool nr_enabled=false;IO grade_only;
    check(!nr_enabled&&k033::inline_grade_record(grade_only,settings)==K033_OK,"NR off grade records");
    check(grade_only.calls==3&&grade_only.armed&&!grade_only.poisoned,"same O capture grade copyback");
    for(int phase=1;phase<=3;++phase){IO io;io.failure=phase;beta2grade::FaultState state;state.feature(7);
        const int result=k033::inline_grade_record(io,settings);state.outcome(result,io.armed);
        check(result==K033_BACKEND_ERROR&&io.poisoned,"partial failure poisons resources");
        check(state.output_uncertain&&!state.admissible(),"partial failure poisons game O");
        // Completing/retiring a use does not call feature(new generation).
        io.armed=false;state.feature(7);check(!state.admissible(),"fence and same feature do not repair O");
        state.feature(8);check(state.admissible(),"new creation resets O uncertainty");
    }
    IO stale;stale.current=false;
    check(k033::inline_grade_record(stale,settings)==K033_BYPASS&&!stale.armed&&!stale.calls,"reentry before arm no commands");
    IO denied;denied.valid=false;
    check(k033::inline_grade_record(denied,settings)==K033_BYPASS&&!denied.calls,"missing state no commands");
    auto invalid=settings;invalid.exposure=99;IO rejected;
    check(k033::inline_grade_record(rejected,invalid)==K033_INVALID&&!rejected.armed,"invalid settings no commands");
    auto changed=settings;changed.tint=.1f;
    check(!k033::inline_grade_same(settings,changed),"grade change invalidates history key");
    k033core::Ledger ledger;k033core::Metadata metadata;metadata.feature=1;
    ledger.created(123,true,metadata);auto first=ledger.get(123);check(bool(first.epoch),"successful creation generation");
    {
        k033core::Scope outer(first,123,k033core::Dx12);k033core::context.completed=true;
        k033core::Frame f;f.stream=123;f.featureGeneration=first.epoch->generation;f.evaluateEpoch=k033core::context.evaluation;
        check(k033core::CurrentFrame(&f)!=0,"current successful evaluate");
        // Simulate foreign parameter Get or COM QI calling back into Evaluate.
        {k033core::Scope nested(ledger.get(123),123,k033core::Dx12);k033core::context.completed=true;
            check(!k033core::CurrentFrame(&f),"nested Get/QI evaluate denied");}
        check(!k033core::CurrentFrame(&f),"outer epoch stays stale after reentry");
    }
    {
        k033core::Scope outer(ledger.get(123),123,k033core::Dx12);k033core::context.completed=true;
        k033core::Frame f;f.stream=123;f.featureGeneration=first.epoch->generation;f.evaluateEpoch=k033core::context.evaluation;
        ledger.released(123,false);check(k033core::CurrentFrame(&f)!=0,"failed release keeps feature");
        ledger.released(123,true);check(!k033core::CurrentFrame(&f),"foreign release invalidates feature");
    }
    ledger.created(123,true,metadata);check(ledger.get(123).epoch->generation!=first.epoch->generation,"reused handle new generation");
    beta2grade::FaultState clean;clean.feature(9);clean.outcome(K033_BYPASS,false);check(clean.admissible(),"unarmed bypass does not poison");
    clean.outcome(K033_BACKEND_ERROR,true);
    check(clean.uncertain_for(9),"arrival-state admission bypass retains prior poison");
    check(clean.uncertain_for(9),"missing strict state admission retains prior poison");
    check(!clean.uncertain_for(10),"actual new feature generation has separate O");
    check(!beta2grade::recyclable(true,clean),"poisoned completed owner cannot be evicted");
    clean.feature(10);check(beta2grade::recyclable(true,clean),"clean idle owner recyclable after eight streams");
    check(!beta2grade::recyclable(false,clean),"pending owner cannot be evicted");
    check(!beta2grade::may_retire(false,true,false),"cancel refused retains unarmed use until ledger completion");
    check(beta2grade::may_retire(false,true,true),"cancel accepted proven inactive releases use");
    check(beta2grade::may_retire(false,false,false),"never reserved use can retire");
    check(!beta2grade::may_retire(true,false,false),"armed missing ticket never inferred complete");
    std::printf("beta2 S54 grade CPU checks=%d failed=%d\n",checks,failed);return failed?1:0;
}
