#include "nr_game_discovery.h"
#include "nr_game_input_abi.h"
#include "nr_input_policy.h"
#include <cstdio>
#include <cwchar>
static unsigned checks=0,failed=0;
static void Check(bool ok,const char* name){++checks;if(!ok){++failed;printf("FAIL %s\n",name);}}
int main(){
    using namespace nrgame033;
    for(auto mount:ModuleNames){unsigned probes=0;
        auto found=UniqueExport([&](const wchar_t* n)->ExportMatch{++probes;return !std::wcscmp(n,mount)?ExportMatch{12,34}:ExportMatch{};});
        Check(found.module==12&&found.address==34&&!found.ambiguous,"role found at each supported mount");
        Check(probes==9,"bounded inventory, no process-wide scan");
    }
    Check(!UniqueExport([](const wchar_t*){return ExportMatch{};}).address,"missing exports do not guess a role");
    Check(!UniqueExport([](const wchar_t*){return ExportMatch{9,0};}).address,"module alone insufficient");
    auto alias=UniqueExport([](const wchar_t*){return ExportMatch{12,34};});
    Check(alias.module==12&&!alias.ambiguous,"one module under multiple aliases is one owner");
    unsigned index=0;auto duplicate=UniqueExport([&](const wchar_t*){return ExportMatch{++index,34};});
    Check(duplicate.ambiguous&&!duplicate.address,"different modules never arbitrarily selected");
    Adapter a;a.id=Re4Tdb71;a.source=ReEngineScene;a.sceneSchema=71;a.compatible=1;
    Check(Supported(a),"approved semantic schema recognized without a game filename");
    for(unsigned i=0;i<8;++i){auto b=a;switch(i){case 0:b.size--;break;case 1:b.version++;break;
        case 2:b.frameVersion++;break;case 3:b.id++;break;case 4:b.source=ImageEstimated;break;
        case 5:b.sceneSchema=74;break;case 6:b.compatible=0;break;case 7:b.reserved=1;break;}
        Check(!Supported(b),"unsupported or not-ready descriptor cannot select a route");}
    using namespace nrinput033;
    Check(PreferUpscale(true,false)&&!PreferUpscale(false,false),"unknown adapters preserve legacy route");
    Check(!PreferUpscale(true,true),"approved adapter removes manual inject configuration");
    Check(CanPresent(true,false,true,PreferUpscale(true,true),0,false,true,Route::None),"known scene can automatically choose presentation");
    Check(!CanPresent(true,false,true,false,1,false,true,Route::None),"real offered upscale input wins");
    Check(!CanPresent(true,false,true,false,0,true,true,Route::None),"native FG cannot double-activate presentation");
    Check(!CanPresent(true,false,true,false,0,false,true,Route::Upscale),"existing upscale owner cannot be replaced");
    Ownership owner;Check(owner.Claim(Route::Upscale)&&!owner.Claim(Route::Presentation),"ownership stays exclusive for entire session");
    Check(!CanPresent(false,false,true,false,0,false,true,Route::None),"F11 off is preserved");
    Check(!CanPresent(true,true,true,false,0,false,true,Route::None),"safe-mode pause is preserved");
    printf("Automatic input CPU checks=%u failures=%u; no modules loaded or GPU created\n",checks,failed);
    return failed?1:0;
}
