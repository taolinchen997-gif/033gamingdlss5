#include "../src/mfg/module_identity.hpp"
#include <cstdio>
int main(){
    auto plugin=LoadLibraryW(L"mfg_identity_fixture.dll");if(!plugin)return 1;
    auto adapter=GetModuleHandleW(nullptr);
    auto owner=reinterpret_cast<HMODULE(*)(HMODULE)>(GetProcAddress(plugin,"ImplementationOwner"));
    unsigned checks=0,failed=0;auto check=[&](bool good,const char* name){++checks;if(!good){++failed;printf("FAIL %s\n",name);}};
    check(owner && owner(adapter)==plugin,"combined implementation owner differs from registration adapter");
    check(!mfgunlock::identity::IsPlugin(plugin,owner(adapter)),"own implementation always excluded even with a plugin export");
    check(mfgunlock::identity::IsPlugin(plugin,adapter),"separate Streamline provider is eligible");
    check(!mfgunlock::identity::IsPlugin(adapter,plugin),"unrelated module without plugin export is excluded");
    check(!mfgunlock::identity::IsPlugin(nullptr,plugin),"null module cannot be scanned");
    FreeLibrary(plugin);printf("MFG identity: %u checks, %u failures; two real modules, no game/GPU\n",checks,failed);return failed?1:0;
}
