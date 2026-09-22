#include "../src/native_reflex_policy.h"
#include <cstdio>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}}
int main(){
    using namespace nativereflex;
    Check(SkipCapWrite(true,0,0),"native no override does not reconfigure Reflex");
    Check(!SkipCapWrite(true,60,0),"explicit native cap is still applied");
    Check(!SkipCapWrite(true,0,16667),"disabling an owned cap must restore original settings");
    Check(!SkipCapWrite(false,0,0),"converted/fake-NVAPI path retains existing policy");
    Check(ReleaseCap(true,8333)==8333,"restore original game cap rather than silently clearing it");
    Check(ReleaseCap(true,0)==0,"uncapped native game stays uncapped");
    Check(ReleaseCap(false,8333)==0,"conversion path keeps previous zero-cap behavior");
    unsigned own=0,calls=0;unsigned applied=8333;
    for(int i=0;i<1000;++i)if(!SkipCapWrite(true,0,own)){++calls;applied=ReleaseCap(true,8333);}
    Check(calls==0 && applied==8333,"repeated zero requests preserve native configuration without driver calls");
    if(!SkipCapWrite(true,60,own)){++calls;own=16667;applied=own;}
    if(!SkipCapWrite(true,0,own)){++calls;own=0;applied=ReleaseCap(true,8333);}
    Check(calls==2 && own==0 && applied==8333,"explicit cap roundtrip writes exactly enable and restore");
    printf("NATIVE REFLEX CPU: %u checks, %u failures; no NVAPI calls\n",checks,failures);
    return failures?1:0;
}
