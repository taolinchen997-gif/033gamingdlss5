// Only unchanged mailbox/codec headers and closed, inert file fixtures execute.
// No NativePreferences thread, Core/host/worker DLL, GPU or game is loaded.
#include "fixtures/shared-settings-runtime-v1/shared/preferences_mailbox.h"
#include "fixtures/shared-settings-runtime-v1/shared/settings_codec.h"
#include "shared_settings_barrier_model.h"
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace fs=std::filesystem;
using installer033::AllSessionBarrierModel;
using installer033::BarrierPhase;
using installer033::Coverage;
using installer033::DrainReceipt;
using Lock=configstore::detail::WriteLock<configstore::detail::Wide>;
enum Category { RuntimeHeader, DirtyBoundary, ProtocolModel };
const char* category_names[]={"unchanged-runtime-header-counterexample","source-derived-dirty-boundary-model","closed-fixture-protocol-model"};
struct Result {const char* name;Category category;unsigned checks,failures;bool passed;};
unsigned checks=0,failures=0;std::vector<Result> results;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line %u: %s\n",unsigned(__LINE__),#x);}}while(0)
template<class Fn>void scenario(Category category,const char* name,Fn body){
    unsigned before=failures,before_checks=checks;
    try{body();}catch(const std::exception& e){++checks;++failures;std::printf("FAIL exception: %s\n",e.what());}
    catch(...){++checks;++failures;std::printf("FAIL unknown C++ exception\n");}
    results.push_back({name,category,checks-before_checks,failures-before,before==failures});
    std::printf("[%s] %s %s (%u checks, %u failures)\n",category_names[category],before==failures?"PASS":"FAIL",name,checks-before_checks,failures-before);
}
void require(bool value){if(!value)throw std::runtime_error("Fixture setup or IO failed");}
void scope(){
    wchar_t module[32768]{};DWORD count=GetModuleFileNameW(nullptr,module,32768);require(count&&count<32768);
    const auto cwd=fs::canonical(fs::current_path());
    require(cwd==fs::canonical(fs::path(module).parent_path()));
    const auto allowed=fs::canonical(fs::path(__FILE__).parent_path().parent_path()/L"build"/L"parallel-InstallerBarrierCpu");
    const auto prefix=allowed.native()+L"\\";require(cwd.native().compare(0,prefix.size(),prefix)==0);
    for(auto path=cwd;!path.empty();path=path.parent_path()){
        DWORD attr=GetFileAttributesW(path.c_str());require(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_REPARSE_POINT));
        if(path==path.parent_path())break;
    }
}
std::string identity(HANDLE handle){
    BY_HANDLE_FILE_INFORMATION info{};require(GetFileInformationByHandle(handle,&info)!=FALSE);
    std::ostringstream value;value<<std::hex<<std::setfill('0')<<std::setw(8)<<info.dwVolumeSerialNumber<<':'
        <<std::setw(8)<<info.nFileIndexHigh<<std::setw(8)<<info.nFileIndexLow;return value.str();
}
std::string identity(const std::wstring& path){Lock lock(path);require(lock.owned);return identity(lock.handle);}
std::string read_bytes(const std::wstring& path){
    FILE* f=nullptr;require(_wfopen_s(&f,path.c_str(),L"rb")==0&&f);std::string bytes;char buffer[4096];size_t n=0;
    while((n=std::fread(buffer,1,sizeof(buffer),f))!=0)bytes.append(buffer,n);
    const bool ok=!std::ferror(f);std::fclose(f);require(ok);return bytes;
}
void evidence_file(const fs::path& path,const std::string& bytes){
    FILE* f=nullptr;require(_wfopen_s(&f,path.c_str(),L"wbx")==0&&f);
    bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();
    ok=std::fclose(f)==0&&ok;require(ok);
}
// Test-driver reset while its exact lock is held. This is not a new installer
// implementation and does not claim all-root rollback or power-loss recovery.
void reset_fixture(const std::wstring& target,const std::string& bytes){
    const auto stage=target+L".reset-fixture.tmp";FILE* f=nullptr;
    require(_wfopen_s(&f,stage.c_str(),L"wbx")==0&&f);
    bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();
    ok=std::fflush(f)==0&&ok;ok=_commit(_fileno(f))==0&&ok;ok=std::fclose(f)==0&&ok;require(ok);
    require(MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE);
}
struct Files {
    fs::path folder;std::wstring path;std::string defaults_bytes,lock_identity;
    explicit Files(const char* name){
        folder=fs::current_path()/name;require(fs::create_directory(folder));path=(folder/L"settings.ini").wstring();
        require(k033settings::write_settings(path.c_str(),k033::defaults(),k033::nr_defaults(false)));
        defaults_bytes=read_bytes(path);lock_identity=identity(path);
        evidence_file(folder/L"defaults.before.ini",defaults_bytes);
        evidence_file(folder/L"lock.before.txt",lock_identity+"\n");
    }
    void finish(){const auto after=identity(path);evidence_file(folder/L"lock.after.txt",after+"\n");CHECK(after==lock_identity);}
    void reset(){Lock lock(path);require(lock.owned);CHECK(identity(lock.handle)==lock_identity);reset_fixture(path,defaults_bytes);}
    K033_Settings grade(){K033_Settings s{};K033_NrSettings nr{};require(k033settings::read_settings(path.c_str(),s,nr));return s;}
    K033_NrSettings nr(){K033_Settings s{};K033_NrSettings n{};require(k033settings::read_settings(path.c_str(),s,n));return n;}
};
bool save_grade(k033settings::PreferencesMailbox& box,const std::wstring& path){
    K033_Settings value{};uint64_t ticket=0;if(!box.take(value,ticket))return false;
    const bool ok=k033settings::write_settings(path.c_str(),value);require(box.complete(ticket,ok?K033_OK:K033_IO_ERROR));return ok;
}
bool save_nr(k033settings::NrPreferencesMailbox& box,const std::wstring& path){
    K033_NrSettings value{};uint64_t ticket=0;if(!box.take(value,ticket))return false;
    const bool ok=k033settings::write_nr(path.c_str(),value);require(box.complete(ticket,ok?K033_OK:K033_IO_ERROR));return ok;
}
// This controlled fixture actor proposes the required producer boundary. The
// unchanged real NativePreferences class has NO freeze/adopt/resume operation.
struct Actor {
    k033settings::PreferencesMailbox grade;
    k033settings::NrPreferencesMailbox nr;
    K033_Settings local=k033::defaults();K033_NrSettings local_nr=k033::nr_defaults(false);
    bool frozen=false,dirty=false;
    bool offer(const K033_Settings& s){if(frozen)return false;local=s;return grade.offer(s)==K033_OK;}
    void freeze(){frozen=true;if(dirty){require(grade.offer(local)==K033_OK);dirty=false;}}
    DrainReceipt snapshot(){
        DrainReceipt value;value.producers_frozen=frozen;value.unqueued_edits=dirty;value.group_count=2;
        value.groups[0].group=0;value.groups[1].group=1;
        require(grade.status(value.groups[0].queued,value.groups[0].saved,value.groups[0].result)==K033_OK);
        require(nr.status(value.groups[1].queued,value.groups[1].saved,value.groups[1].result)==K033_OK);return value;
    }
    void adopt(const std::wstring& path){
        require(frozen&&!dirty);K033_Settings s{};K033_NrSettings n{};require(k033settings::read_settings(path.c_str(),s,n));
        require(grade.publish_external(s)&&grade.receive(local)==K033_OK);
        require(nr.publish_external(n)&&nr.receive(local_nr)==K033_OK);
    }
};
const std::string id="12345678:000000000000002a";
// Synthetic plan digest tokens exercise binding; file content is checked
// separately using the actual codec. They are not asserted to be file hashes.
const std::string digest(64,'a'),other_digest(64,'b');
DrainReceipt clean(){DrainReceipt r;r.producers_frozen=true;r.unqueued_edits=false;r.group_count=2;r.groups[0].group=0;r.groups[1].group=1;return r;}
AllSessionBarrierModel one(){AllSessionBarrierModel m;require(m.begin(Coverage::ClosedFixtureOnly,{{1,10,3}},id,digest));return m;}
AllSessionBarrierModel locked(){auto m=one();require(m.acknowledge_drained(m.epoch(),1,10,clean()));require(m.lock_acquired(m.epoch(),id));return m;}

int main(){
    try{scope();}catch(...){std::fprintf(stderr,"Refuse execution outside the exact wrapper fixture output\n");return 2;}
    scenario(RuntimeHeader,"taken snapshot retries after lock release and overwrites a naive reset",[]{
        Files f("taken-snapshot");k033settings::PreferencesMailbox box;auto old=k033::defaults();old.exposure=.75f;
        CHECK(box.offer(old)==K033_OK);K033_Settings taken{};uint64_t ticket=0;CHECK(box.take(taken,ticket));
        {Lock reset_lock(f.path);require(reset_lock.owned);
            CHECK(!k033settings::write_settings(f.path.c_str(),taken));CHECK(box.complete(ticket,K033_IO_ERROR));
            reset_fixture(f.path,f.defaults_bytes);CHECK(f.grade().exposure==0);
        }
        uint64_t queued=0,saved=0;int result=0;CHECK(box.status(queued,saved,result)==K033_OK&&queued==1&&saved==0);
        CHECK(save_grade(box,f.path));CHECK(f.grade().exposure==old.exposure);CHECK(identity(f.path)==f.lock_identity);f.finish();
    });
    scenario(RuntimeHeader,"sampled queued equals saved does not freeze later offers",[]{
        Files f("late-offer");k033settings::PreferencesMailbox box;uint64_t queued=99,saved=98;int result=0;
        CHECK(box.status(queued,saved,result)==K033_OK&&queued==saved);auto old=k033::defaults();old.exposure=-.5f;
        {Lock reset_lock(f.path);require(reset_lock.owned);reset_fixture(f.path,f.defaults_bytes);CHECK(box.offer(old)==K033_OK);}
        CHECK(save_grade(box,f.path));CHECK(f.grade().exposure==old.exposure);f.finish();
    });
    scenario(RuntimeHeader,"one mailbox receipt does not drain another writer or the NR group",[]{
        Files f("other-writer");k033settings::PreferencesMailbox first,second;uint64_t q=0,s=0;int r=0;
        CHECK(first.status(q,s,r)==K033_OK&&q==s);auto old=k033::defaults();old.exposure=.5f;CHECK(second.offer(old)==K033_OK);
        f.reset();CHECK(save_grade(second,f.path));CHECK(f.grade().exposure==old.exposure);
        k033settings::NrPreferencesMailbox nr;auto value=k033::nr_defaults(true);value.layers=2;CHECK(nr.offer(value)==K033_OK);
        CHECK(first.status(q,s,r)==K033_OK&&q==s);f.reset();CHECK(save_nr(nr,f.path));CHECK(f.nr().enabled==1&&f.nr().layers==2);f.finish();
    });
    scenario(DirtyBoundary,"local pre-mailbox work is invisible to mailbox-only status",[]{
        Files f("unqueued-work");Actor actor;actor.local.exposure=.625f;actor.dirty=true;
        uint64_t q=0,s=0;int r=0;CHECK(actor.grade.status(q,s,r)==K033_OK&&q==s);CHECK(actor.dirty);
        f.reset();actor.freeze();CHECK(save_grade(actor.grade,f.path));CHECK(f.grade().exposure==.625f);f.finish();
    });
    scenario(ProtocolModel,"model rejects unproven coverage empty duplicate and unbounded membership",[]{
        CHECK(!AllSessionBarrierModel::production_available);
        AllSessionBarrierModel a;CHECK(!a.begin(Coverage::Unknown,{{1,10,3}},id,digest));CHECK(!a.may_write_defaults());
        AllSessionBarrierModel b;CHECK(!b.begin(Coverage::ClosedFixtureOnly,{},id,digest));
        AllSessionBarrierModel c;CHECK(!c.begin(Coverage::ClosedFixtureOnly,{{1,10,3},{1,11,3}},id,digest));
        std::vector<installer033::Member> many;for(uint64_t i=1;i<=33;++i)many.push_back({i,i,3});
        AllSessionBarrierModel d;CHECK(!d.begin(Coverage::ClosedFixtureOnly,many,id,digest));
        AllSessionBarrierModel e(UINT64_MAX);CHECK(!e.begin(Coverage::ClosedFixtureOnly,{{1,10,3}},id,digest));
        AllSessionBarrierModel f;CHECK(!f.begin(Coverage::ClosedFixtureOnly,{{1,10,1}},id,digest));
    });
    scenario(ProtocolModel,"model requires exact generation incarnation and every persistent group",[]{
        auto m=one();auto r=clean();CHECK(!m.admission_open());CHECK(!m.lock_acquired(m.epoch(),id));
        CHECK(!m.acknowledge_drained(m.epoch()+1,1,10,r));CHECK(!m.acknowledge_drained(m.epoch(),1,11,r));
        r.group_count=1;CHECK(!m.acknowledge_drained(m.epoch(),1,10,r));r=clean();r.groups[1].queued=1;
        CHECK(!m.acknowledge_drained(m.epoch(),1,10,r));r=clean();r.unqueued_edits=true;CHECK(!m.acknowledge_drained(m.epoch(),1,10,r));
        r=clean();r.explicit_saves_in_flight=1;CHECK(!m.acknowledge_drained(m.epoch(),1,10,r));
        r=clean();r.producers_frozen=false;CHECK(!m.acknowledge_drained(m.epoch(),1,10,r));
        CHECK(m.acknowledge_drained(m.epoch(),1,10,clean()));CHECK(m.phase()==BarrierPhase::AwaitingLock);
        AllSessionBarrierModel future;CHECK(future.begin(Coverage::ClosedFixtureOnly,{{1,10,7}},id,digest));
        CHECK(!future.acknowledge_drained(future.epoch(),1,10,clean())); // future FG group cannot disappear
    });
    scenario(ProtocolModel,"model fails closed on lost members changed lock identity and new work",[]{
        auto m=one();m.member_lost_or_new_work(1);CHECK(m.phase()==BarrierPhase::Blocked&&!m.admission_open());
        auto n=one();CHECK(n.acknowledge_drained(n.epoch(),1,10,clean()));
        CHECK(!n.lock_acquired(n.epoch(),"12345678:000000000000002b"));CHECK(n.phase()==BarrierPhase::Blocked);
        auto p=locked();p.member_lost_or_new_work(99);CHECK(!p.may_write_defaults()&&!p.admission_open());
        auto q=locked();q.reset_failed();CHECK(!q.admission_open());
    });
    scenario(ProtocolModel,"new work cannot reuse an earlier clean acknowledgement",[]{
        AllSessionBarrierModel m;CHECK(m.begin(Coverage::ClosedFixtureOnly,{{1,10,3},{2,20,3}},id,digest));
        CHECK(m.acknowledge_drained(m.epoch(),1,10,clean()));auto dirty=clean();dirty.groups[0].queued=1;
        CHECK(!m.acknowledge_drained(m.epoch(),1,10,dirty));CHECK(m.phase()==BarrierPhase::Blocked);
        CHECK(!m.acknowledge_drained(m.epoch(),2,20,clean()));CHECK(!m.may_write_defaults());
        auto n=locked();CHECK(!n.acknowledge_drained(n.epoch(),1,10,dirty));CHECK(n.phase()==BarrierPhase::Blocked);
    });
    scenario(ProtocolModel,"completed new saves invalidate a captured drain at every active phase",[]{
        auto advanced=clean();advanced.groups[0].queued=advanced.groups[0].saved=1;advanced.groups[0].result=K033_OK;
        for(unsigned phase=0;phase<5;++phase){
            AllSessionBarrierModel m;require(m.begin(Coverage::ClosedFixtureOnly,phase==0?std::vector<installer033::Member>{{1,10,3},{2,20,3}}:std::vector<installer033::Member>{{1,10,3}},id,digest));
            require(m.acknowledge_drained(m.epoch(),1,10,clean()));
            if(phase>=2)require(m.lock_acquired(m.epoch(),id));
            if(phase>=3)require(m.committed(m.epoch(),id,digest));
            if(phase>=4)require(m.acknowledge_adopted(m.epoch(),1,10,digest,clean()));
            CHECK(!m.acknowledge_drained(m.epoch(),1,10,advanced));CHECK(m.phase()==BarrierPhase::Blocked);
            CHECK(!m.may_write_defaults()&&!m.admission_open());CHECK(!m.unlocked(m.epoch(),id));
        }
        auto adoption=locked();require(adoption.committed(adoption.epoch(),id,digest));
        CHECK(!adoption.acknowledge_adopted(adoption.epoch(),1,10,digest,advanced));CHECK(adoption.phase()==BarrierPhase::Blocked);
        auto late=locked();require(late.committed(late.epoch(),id,digest));require(late.acknowledge_adopted(late.epoch(),1,10,digest,clean()));
        CHECK(!late.acknowledge_adopted(late.epoch(),1,10,digest,advanced));CHECK(late.phase()==BarrierPhase::Blocked);
        auto changed_result=locked();auto r=clean();r.groups[0].result=K033_OK;
        CHECK(!changed_result.acknowledge_drained(changed_result.epoch(),1,10,r));CHECK(changed_result.phase()==BarrierPhase::Blocked);
    });
    scenario(ProtocolModel,"work accepted before the drain can finish and receipt groups may reorder",[]{
        auto m=one();auto pending=clean();pending.groups[0].queued=2;
        CHECK(!m.acknowledge_drained(m.epoch(),1,10,pending));CHECK(m.phase()==BarrierPhase::Freezing);
        auto completed=pending;completed.groups[0].saved=2;completed.groups[0].result=K033_OK;
        CHECK(m.acknowledge_drained(m.epoch(),1,10,completed));CHECK(m.lock_acquired(m.epoch(),id));
        CHECK(m.committed(m.epoch(),id,digest));std::swap(completed.groups[0],completed.groups[1]);
        CHECK(m.acknowledge_adopted(m.epoch(),1,10,digest,completed));CHECK(m.unlocked(m.epoch(),id));
        AllSessionBarrierModel n;require(n.begin(Coverage::ClosedFixtureOnly,{{1,10,3},{2,20,3}},id,digest));
        CHECK(n.acknowledge_drained(n.epoch(),1,10,completed));std::swap(completed.groups[0],completed.groups[1]);
        CHECK(n.acknowledge_drained(n.epoch(),1,10,completed));CHECK(n.phase()==BarrierPhase::Freezing);
    });
    scenario(ProtocolModel,"adoption must match committed defaults before unlock or admission",[]{
        auto bad=locked();CHECK(!bad.committed(bad.epoch(),id,other_digest));CHECK(!bad.admission_open());
        auto early=locked();CHECK(early.committed(early.epoch(),id,digest));CHECK(!early.unlocked(early.epoch(),id));CHECK(early.phase()==BarrierPhase::Blocked);
        auto m=locked();CHECK(m.committed(m.epoch(),id,digest));CHECK(!m.acknowledge_adopted(m.epoch(),1,10,other_digest,clean()));
        CHECK(!m.acknowledge_adopted(m.epoch()-1,1,10,digest,clean()));CHECK(!m.admission_open());
        CHECK(m.acknowledge_adopted(m.epoch(),1,10,digest,clean()));CHECK(!m.admission_open());
        auto dirty=clean();dirty.unqueued_edits=true;CHECK(!m.acknowledge_adopted(m.epoch(),1,10,digest,dirty));CHECK(m.phase()==BarrierPhase::Blocked);
    });
    scenario(ProtocolModel,"closed fixture drains real mailboxes adopts defaults then resumes",[]{
        Files f("positive-closed-fixture");Actor a,b;a.local.exposure=.8f;a.dirty=true;
        auto old=k033::defaults();old.exposure=-.8f;CHECK(b.offer(old));auto nr=k033::nr_defaults(true);nr.layers=3;CHECK(b.nr.offer(nr)==K033_OK);
        AllSessionBarrierModel m(7);CHECK(m.begin(Coverage::ClosedFixtureOnly,{{1,10,3},{2,20,3}},f.lock_identity,digest));CHECK(m.epoch()==8);
        a.freeze();b.freeze();CHECK(!a.offer(old));CHECK(!b.offer(old));
        CHECK(!m.acknowledge_drained(m.epoch(),1,10,a.snapshot()));CHECK(!m.acknowledge_drained(m.epoch(),2,20,b.snapshot()));
        CHECK(save_grade(a.grade,f.path));CHECK(save_grade(b.grade,f.path));CHECK(save_nr(b.nr,f.path));
        CHECK(m.acknowledge_drained(m.epoch(),1,10,a.snapshot()));CHECK(m.acknowledge_drained(m.epoch(),2,20,b.snapshot()));
        {Lock reset_lock(f.path);require(reset_lock.owned);CHECK(m.lock_acquired(m.epoch(),identity(reset_lock.handle)));CHECK(m.may_write_defaults());
            reset_fixture(f.path,f.defaults_bytes);CHECK(f.grade().exposure==0&&f.nr().enabled==0);
            CHECK(m.committed(m.epoch(),identity(reset_lock.handle),digest));a.adopt(f.path);b.adopt(f.path);
            CHECK(a.local.exposure==0&&b.local.exposure==0&&a.local_nr.enabled==0&&b.local_nr.enabled==0);
            CHECK(m.acknowledge_adopted(m.epoch(),1,10,digest,a.snapshot()));CHECK(!m.admission_open());
            CHECK(m.acknowledge_adopted(m.epoch(),2,20,digest,b.snapshot()));CHECK(m.phase()==BarrierPhase::AwaitingUnlock);
            CHECK(!a.offer(old)&&!b.offer(old));CHECK(identity(reset_lock.handle)==f.lock_identity);
        }
        CHECK(m.unlocked(m.epoch(),f.lock_identity));CHECK(m.admission_open());a.frozen=b.frozen=false;
        auto fresh=a.local;fresh.contrast=1.1f;CHECK(a.offer(fresh));CHECK(save_grade(a.grade,f.path));
        CHECK(f.grade().contrast==1.1f&&f.grade().exposure==0&&f.nr().enabled==0);CHECK(identity(f.path)==f.lock_identity);f.finish();
    });
    FILE* f=nullptr;if(fopen_s(&f,"barrier-results.json","wb")!=0||!f)return 3;
    std::fprintf(f,"{\"checks\":%u,\"failures\":%u,\"cases\":%u,\"productionAvailable\":false,\"installable\":false,\"safeToResetProduction\":false,\"nativeRuntimeExecuted\":false,\"categories\":[",checks,failures,unsigned(results.size()));
    for(unsigned category=0;category<3;++category){
        unsigned count=0,category_checks=0,category_failures=0;for(const auto& r:results)if(r.category==category){++count;category_checks+=r.checks;category_failures+=r.failures;}
        std::fprintf(f,"%s{\"name\":\"%s\",\"cases\":%u,\"checks\":%u,\"failures\":%u}",category?",":"",category_names[category],count,category_checks,category_failures);
        std::printf("CATEGORY %s: %u cases, %u checks, %u failures\n",category_names[category],count,category_checks,category_failures);
    }
    std::fprintf(f,"],\"results\":[");
    for(size_t i=0;i<results.size();++i)std::fprintf(f,"%s{\"name\":\"%s\",\"category\":\"%s\",\"checks\":%u,\"failures\":%u,\"passed\":%s}",i?",":"",results[i].name,category_names[results[i].category],results[i].checks,results[i].failures,results[i].passed?"true":"false");
    std::fprintf(f,"]}\n");if(std::fclose(f)!=0)return 3;
    std::printf("Shared settings barrier CPU: %u checks, %u failures, %u cases; categorized header counterexamples and fixture models; production unavailable\n",checks,failures,unsigned(results.size()));
    return failures?1:0;
}
