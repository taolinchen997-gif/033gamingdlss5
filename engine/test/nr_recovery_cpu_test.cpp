#include <cstdio>
#include <cstdint>
#include "../src/nr_recovery_policy.h"
static int checks=0,failed=0;
static void check(bool b,const char* label){++checks;if(!b){++failed;std::printf("FAIL %s\n",label);}}
struct Bank{void* feat=nullptr;void* extra_feat[2]={};};
int main(){
    nrrecovery033::Retry retry;
    check(retry.Due(0),"first allocation allowed");
    retry.Failed(100);check(!retry.Due(349)&&retry.Due(350),"failed allocation backs off");
    retry.Failed(350);check(!retry.Due(849)&&retry.Due(850),"second delay doubles");
    for(int i=0;i<200;++i)retry.Failed(1000);
    check(retry.next==5000 && retry.failures==32,"retry remains bounded and available after prolonged pressure");
    retry.Success();check(retry.Due(0)&&retry.failures==0,"successful resource preparation resets backoff");
    retry.Failed(UINT64_MAX-1);check(retry.next==UINT64_MAX,"clock arithmetic does not wrap");

    nrrecovery033::Evaluation e;
    check(!e.Failed(0,1)&&!e.Failed(0,2)&&e.Failed(0,3),"three same-layer failures request replacement");
    check(e.mask==1 && e.Pending(),"only failed first feature is requested");
    e.Success();check(e.streak==0&&e.Pending(),"ordinary success does not falsely claim requested replacement completed");
    e.Adopted(2,20);check(e.Pending(),"unrelated or borrowed feature adoption is not recovery");
    e.Adopted(1,20);check(!e.Pending()&&e.cooldown==5020,"completed actual replacement clears request");
    check(!e.Failed(0,21)&&!e.Failed(0,22)&&!e.Failed(0,23),"no rebuild storm in cooldown");
    check(e.Failed(0,5020),"continuing error retries after cooldown");
    nrrecovery033::Evaluation f;
    check(!f.Failed(0,1)&&!f.Failed(1,2)&&!f.Failed(0,3),"failures in different layers do not form false streak");
    f.Success();check(!f.Failed(0,4),"one successful frame separates error streaks");
    check(!f.Failed(99,4)&&f.mask==0,"invalid layer does not request unrelated model");
    int a,b,c,d;
    Bank old{&a,{&b,&c}},candidate=old;
    nrrecovery033::RequestFresh(candidate,2);
    check(old.extra_feat[0]==&b && candidate.extra_feat[0]==nullptr,"request keeps live feature owned and only clears candidate pointer");
    check(candidate.feat==&a&&candidate.extra_feat[1]==&c,"unaffected live features remain reusable");
    check(nrrecovery033::Replaced(old,candidate)==0,"empty candidate cannot report restored NR");
    candidate.extra_feat[0]=&d;
    check(nrrecovery033::Replaced(old,candidate)==2,"only initialized replacement is recognized");
    std::printf("NR recovery policy: %d checks, %d failed\n",checks,failed);return failed?1:0;
}
