#include "render_core_policy.h"
#include <cstdio>
#include <thread>
int main(){unsigned count=0,failures=0;auto check=[&](bool b,const char*n){++count;if(!b){++failures;printf("FAIL %s\n",n);}};
    using namespace k033core;
    Ledger ledger;Metadata sr{1,19,1,5120,2160},fg{11,0,1,5120,2160};
    check(ledger.get(7).feature==-1,"unknown does not become feature zero");
    ledger.created(7,false,sr);check(ledger.get(7).feature==-1,"failed creation never accepted");
    ledger.created(7,true,sr);check(ledger.get(7).outputW==5120,"creation contract retained");
    ledger.released(7,false);check(Image(ledger.get(7).feature),"failed release retains contract");
    ledger.released(7,true);check(!Image(ledger.get(7).feature),"released handle cannot reuse stale metadata");
    ledger.created(7,true,fg);check(!Image(ledger.get(7).feature),"reused handle classified by new successful create");
    check(Image(1)&&Image(13)&&!Image(12)&&!Image(11)&&!Image(18)&&!Image(-1)&&!Image(0),"strict SR/RR allowlist (13 = RR, 12 = DeepDVC)");
    {Scope outer(sr,7,Dx12);check(!FirstOffer(),"successful API skip without rendering withheld");context.completed=true;
        {Scope inner(sr,7,Dx12);context.completed=true;check(!FirstOffer(),"nested wrapper withheld");}
        check(FirstOffer(),"outer SR reaches NR");check(!FirstOffer(),"second NR offer in same evaluation withheld");}
    {Scope scope(fg,8,Dx12);check(!FirstOffer(),"FG stale Output cannot reach NR");}
    check(context.depth==0,"scope restored");
    Ownership owners;check(!owners.claim(Unclaimed)&&!owners.claim(4),"invalid owner rejected");
    check(owners.claim(Host033)&&owners.claim(Host033),"owner may reuse own feature");
    check(!owners.claim(NativeVulkan)&&!owners.claim(CoreDx12),"no mid-session API takeover");
    Ownership racing;bool a=false,b=false;std::thread x([&]{a=racing.claim(Host033);}),y([&]{b=racing.claim(NativeVulkan);});x.join();y.join();
    check(a!=b,"exactly one concurrent owner");
    Frame f;f.command=(void*)1;f.parameters=(void*)2;f.stream=3;f.feature=1;check(Valid(&f),"valid borrowed frame");
    f.version++;check(!Valid(&f),"incompatible ABI rejected");f.version=Version;f.feature=11;check(!Valid(&f),"FG ABI request rejected");
    printf("integrated render contract: %u checks, %u failures\n",count,failures);return failures?1:0;
}
