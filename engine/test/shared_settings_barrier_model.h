// CPU reference model for the proposed ordering, not a runtime/IPC provider.
// ClosedFixtureOnly is never a production coverage certificate.
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace installer033 {
enum class Coverage { Unknown, ClosedFixtureOnly };
enum class BarrierPhase { Open, Freezing, AwaitingLock, Locked, Adopting, AwaitingUnlock, Complete, Blocked };
struct Member {
    uint64_t id=0,incarnation=0;
    uint32_t required_groups=3; // grade + NR; any later persistent group must join.
};
struct GroupReceipt {
    uint32_t group=0;
    uint64_t queued=0,saved=0;
    int result=1; // existing mailbox: BYPASS before any save, OK after a save.
};
struct DrainReceipt {
    bool producers_frozen=false,unqueued_edits=true;
    uint32_t explicit_saves_in_flight=0,group_count=0;
    std::array<GroupReceipt,4> groups{};
};
class AllSessionBarrierModel {
    struct State {Member member;bool drained=false,adopted=false;DrainReceipt drain;};
    std::vector<State> members;
    uint64_t generation=0;
    std::string identity,defaults;
    BarrierPhase phase_=BarrierPhase::Open;
    static bool hex(const std::string& value,size_t length){
        if(value.size()!=length)return false;
        for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        return true;
    }
    static bool identity_valid(const std::string& value){
        return value.size()==25&&value[8]==':'&&hex(value.substr(0,8),8)&&hex(value.substr(9),16);
    }
    State* member(uint64_t id,uint64_t incarnation){
        for(auto& value:members)if(value.member.id==id&&value.member.incarnation==incarnation)return &value;
        return nullptr;
    }
    static bool clean(const State& state,const DrainReceipt& receipt){
        if(!receipt.producers_frozen||receipt.unqueued_edits||receipt.explicit_saves_in_flight||!receipt.group_count||receipt.group_count>4)return false;
        uint32_t observed=0;
        for(uint32_t i=0;i<receipt.group_count;++i){
            const auto& g=receipt.groups[i];if(g.group>=4||(observed&(1u<<g.group)))return false;
            observed|=1u<<g.group;
            if(g.queued!=g.saved||(g.saved?g.result!=0:(g.result!=0&&g.result!=1)))return false;
        }
        return observed==state.member.required_groups;
    }
    static bool same_counters(const DrainReceipt& newer,const DrainReceipt& before){
        if(newer.group_count!=before.group_count)return false;
        for(uint32_t i=0;i<before.group_count;++i){bool found=false;
            for(uint32_t j=0;j<newer.group_count;++j)if(newer.groups[j].group==before.groups[i].group){
                const auto& a=newer.groups[j];const auto& b=before.groups[i];
                if(a.queued!=b.queued||a.saved!=b.saved||a.result!=b.result)return false;found=true;break;
            }
            if(!found)return false;
        }
        return true;
    }
    bool all(bool adoption)const{for(const auto& s:members)if(adoption?!s.adopted:!s.drained)return false;return !members.empty();}
    bool blocked(){phase_=BarrierPhase::Blocked;return false;}
public:
    static constexpr bool production_available=false;
    explicit AllSessionBarrierModel(uint64_t previous_generation=0):generation(previous_generation){}
    BarrierPhase phase()const{return phase_;}
    uint64_t epoch()const{return generation;}
    bool admission_open()const{return phase_==BarrierPhase::Open||phase_==BarrierPhase::Complete;}
    bool may_write_defaults()const{return phase_==BarrierPhase::Locked;}
    bool begin(Coverage coverage,const std::vector<Member>& fixed_members,const std::string& lock_identity,const std::string& defaults_sha256){
        if(phase_!=BarrierPhase::Open)return false;
        if(coverage!=Coverage::ClosedFixtureOnly||fixed_members.empty()||fixed_members.size()>32||
           !identity_valid(lock_identity)||!hex(defaults_sha256,64)||generation==(std::numeric_limits<uint64_t>::max)())return blocked();
        for(size_t i=0;i<fixed_members.size();++i){const auto& m=fixed_members[i];
            if(!m.id||!m.incarnation||(m.required_groups&3u)!=3u||(m.required_groups&~15u))return blocked();
            for(size_t j=0;j<i;++j)if(fixed_members[j].id==m.id)return blocked();
        }
        for(const auto& m:fixed_members)members.push_back({m,false,false,{}});
        identity=lock_identity;defaults=defaults_sha256;++generation;phase_=BarrierPhase::Freezing;return true;
    }
    bool acknowledge_drained(uint64_t epoch,uint64_t id,uint64_t incarnation,const DrainReceipt& receipt){
        if(epoch!=generation)return false;
        auto* state=member(id,incarnation);if(!state)return false;
        if(phase_==BarrierPhase::Open||phase_==BarrierPhase::Complete||phase_==BarrierPhase::Blocked)return false;
        // A fully completed new save also invalidates the earlier freeze proof.
        // Bind by group identity, not receipt array position or monotonicity.
        if(state->drained&&(!clean(*state,receipt)||!same_counters(receipt,state->drain)))return blocked();
        if(phase_!=BarrierPhase::Freezing)return false;
        if(!clean(*state,receipt))return false;
        state->drain=receipt;state->drained=true;if(all(false))phase_=BarrierPhase::AwaitingLock;return true;
    }
    bool lock_acquired(uint64_t epoch,const std::string& lock_identity){
        if(phase_!=BarrierPhase::AwaitingLock||epoch!=generation)return false;
        if(lock_identity!=identity)return blocked();phase_=BarrierPhase::Locked;return true;
    }
    bool committed(uint64_t epoch,const std::string& lock_identity,const std::string& defaults_sha256){
        if(phase_!=BarrierPhase::Locked||epoch!=generation)return false;
        if(lock_identity!=identity||defaults_sha256!=defaults)return blocked();
        phase_=BarrierPhase::Adopting;return true;
    }
    bool acknowledge_adopted(uint64_t epoch,uint64_t id,uint64_t incarnation,const std::string& defaults_sha256,const DrainReceipt& receipt){
        if(epoch!=generation)return false;
        auto* state=member(id,incarnation);if(!state)return false;
        if(phase_==BarrierPhase::Open||phase_==BarrierPhase::Complete||phase_==BarrierPhase::Blocked)return false;
        if(state->drained&&(!clean(*state,receipt)||!same_counters(receipt,state->drain)))return blocked();
        const bool valid=state->drained&&defaults_sha256==defaults&&clean(*state,receipt)&&same_counters(receipt,state->drain);
        if(phase_!=BarrierPhase::Adopting){if(phase_==BarrierPhase::AwaitingUnlock&&!valid)return blocked();return false;}
        if(!valid){if(state->adopted)return blocked();return false;}
        state->adopted=true;if(all(true))phase_=BarrierPhase::AwaitingUnlock;return true;
    }
    bool unlocked(uint64_t epoch,const std::string& lock_identity){
        if(epoch!=generation)return false;
        if(phase_!=BarrierPhase::AwaitingUnlock){if(phase_!=BarrierPhase::Open&&phase_!=BarrierPhase::Complete)return blocked();return false;}
        if(lock_identity!=identity)return blocked();phase_=BarrierPhase::Complete;return true;
    }
    // A lost member or accepted new work invalidates the fixed cohort. Never
    // remove the member and reinterpret its absence/process exit as a drain.
    void member_lost_or_new_work(uint64_t id){
        if(phase_==BarrierPhase::Open||phase_==BarrierPhase::Complete)return;
        (void)id;phase_=BarrierPhase::Blocked; // unknown writers also invalidate coverage
    }
    void reset_failed(){if(phase_!=BarrierPhase::Open&&phase_!=BarrierPhase::Complete)phase_=BarrierPhase::Blocked;}
};
}
