// Invoked by the CPU-only universal suite, using production route/config code.
#include <sstream>
#include "../src/fg_route.h"
#include "../src/upscale_notice_policy.h"
static void routeChecks(){
    using namespace fgroute033;
    srnotice033::Gate notice;
    check(notice.Notify(0),"first SR failure is visible");
    for(uint64_t now:{0ull,1ull,2500ull,5000ull,9999ull})check(!notice.Notify(now),"recreated feature/view does not stack duplicate failure toasts");
    check(notice.Notify(10000),"persistent SR failure remains visible after notification expires");
    auto parse=[](const char* text){std::istringstream input(text);return Parse(input);};
    check(parse("")==Universal,"new installation defaults to universal until FG input is observed");
    check(parse("imagefg=1\r\n")==Universal,"existing RE4 preparation preserved");
    check(parse("imagefg=1\nfgroute=0\n")==Native,"explicit native wins over legacy flag");
    check(parse("fgroute=1\nimagefg=0\n")==Universal,"explicit universal wins regardless of key order");
    check(parse("fgroute=12\nimagefg=0\n")==Native,"invalid route cannot activate universal");
    for(unsigned boot=Native;boot<=Universal;++boot){Selection route(boot);int writes=0;
        check(!route.Select(2,true,[&](unsigned){++writes;return true;})&&writes==0,"invalid selection is rejected before writes");
        check(!route.Select(1-boot,true,[&](unsigned){++writes;return false;}),"failed save does not apply selection");
        check(route.Selected()==boot&&!route.Pending(),"failed save preserves current and next route");
        check(route.Select(1-boot,true,[&](unsigned){++writes;return true;}),"opposite route can be selected");
        check(route.Boot()==boot&&route.Pending(),"selection never hot replaces live presentation");
        Selection restart(route.Selected());check(restart.Boot()==1-boot&&!restart.Pending(),"next process applies choice");
        check(route.Select(boot,true,[](unsigned){return true;})&&!route.Pending(),"pending change can be cancelled");
    }
    // Product rule uses real feature inputs, not an interposer/GPU label.
    for(unsigned cap=fgcap033::Unknown;cap<=fgcap033::Ready;++cap){
        fgcap033::Evidence evidence;evidence.Observe(cap);
        check(evidence.NativeAllowed()==(cap==fgcap033::Ready),"only compatible native input enables native selection");
        for(unsigned choice=0;choice<3;++choice){
            Selection route(Universal);int saves=0;
            const bool ok=route.Select(choice,evidence.NativeAllowed(),[&](unsigned){++saves;return true;});
            const bool expected=choice==Universal||(choice==Native&&cap==fgcap033::Ready);
            check(ok==expected && saves==int(expected),"unavailable native selection rejected before disk write");
            check(route.Boot()==Universal,"native selection never replaces a live universal surface");
        }
        evidence.Observe(fgcap033::Ready);evidence.Observe(fgcap033::OldInterface);
        check(evidence.NativeAllowed(),"SR-only and old companion interfaces cannot erase confirmed FG input");
    }
    for(bool valid:{false,true})for(bool requested:{false,true})for(bool compatible:{false,true})for(bool init:{false,true}){
        check((fgcap033::Streamline(valid,requested,compatible,init)==fgcap033::Ready)==(valid&&requested&&compatible&&init),
              "native input requires an actual FG request and compatible successful initialization");
    }
    unsigned srOnly[]={0,2,3},withFG[]={0,1000,3};
    check(!fgcap033::Requests(srOnly,3,1000u),"DLSS SR and Reflex are not frame generation");
    check(fgcap033::Requests(withFG,3,1000u),"unfiltered original game feature list identifies FG even in universal mode");
    check(!fgcap033::Requests<unsigned>(nullptr,1,1000u),"invalid feature pointer is not capability evidence");
    check(!fgcap033::Requests(withFG,65,1000u),"invalid feature count is not read");
    for(bool ready:{false,true})for(unsigned initial:{Native,Universal}){
        Selection automatic(initial,true);int saves=0;
        check(automatic.Resolve(ready,[&](unsigned v){++saves;return v==fgcap033::DefaultRoute(ready);}),"automatic default saved");
        check(automatic.Boot()==initial && automatic.Selected()==fgcap033::DefaultRoute(ready),"detection schedules route without hot switching");
        const int saved=saves;
        check(automatic.Resolve(ready,[&](unsigned){++saves;return true;})&&saves==saved,"unchanged default does not rewrite configuration per UI frame");
    }
    Selection explicitUniversal(Universal);
    check(explicitUniversal.Resolve(true,[](unsigned){return false;}) && explicitUniversal.Selected()==Universal,
          "a user can retain universal despite available native FG");
    Selection explicitNative(Native);
    check(!explicitNative.Resolve(false,[](unsigned){return false;}) && explicitNative.Selected()==Native,"failed fallback save keeps prior choice");
    check(explicitNative.Resolve(false,[](unsigned v){return v==Universal;}) && explicitNative.Pending(),"unavailable native route defaults to universal next launch");
    {std::istringstream empty("");check(ParsePreferences(empty).automatic,"fresh default uses detection");}
    {std::istringstream old("fgroute=1\n");check(!ParsePreferences(old).automatic,"legacy user-selected universal remains explicit");}
    {std::istringstream automatic("fgroute=1\nfgrouteauto=1\n");check(ParsePreferences(automatic).automatic,"detected choice remains automatic on next launch");}
    const char* path="route-test-temporary.cfg";
    const std::string kept="hotkey=122\r\nmfgmultiplier=6\r\nimagefgmultiplier=3\r\nstyle=2\r\nunknown=keep\r\n";
    {std::ofstream file(path,std::ios::binary);file<<kept<<"imagefg=0\r\n";}
    for(unsigned choice: {1u,0u,1u,0u}) {
        check(SaveChoice(path,choice),"route transaction persisted");
        std::ifstream file(path,std::ios::binary);std::string text((std::istreambuf_iterator<char>(file)),{});
        check(text.substr(0,kept.size())==kept,"both multiplier settings, hotkey and foreign bytes preserved");
        std::istringstream input(text);check(Parse(input)==choice,"persisted choice round trips");
        check(text.find("fgroute=",text.find("fgroute=")+1)==std::string::npos,"only one route key survives");
    }
    HANDLE held=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"hold original config for failed atomic replace");
    check(!SaveChoice(path,1),"locked config refuses replacement");
    CloseHandle(held);{std::ifstream file(path);check(Parse(file)==Native,"failed replacement preserves disk route");}
    check(DeleteFileA(path)!=0,"temporary config removed");
    check(Reason(false,false,false)==None,"game without native components can use universal");
    check(Reason(false,true,false)==StreamlineNotExcluded,"loaded interposer alone is insufficient");
    check(Reason(false,true,true)==None,"excluded feature with hook ownership allows universal");
    check(Reason(true,true,true)==NativeComponent,"native library overrides even an earlier exclusion");
    unsigned features[]={0,1000,2,1000,7};auto filtered=WithoutFramegen(features,5,1000u);
    check(filtered==std::vector<unsigned>({0,2,7}),"SR NR Reflex and caller order preserved");
    check(features[1]==1000&&features[3]==1000,"original preferences remain untouched");
    check(WithoutFramegen<unsigned>(nullptr,0,1000).empty(),"empty feature list is valid");
    unsigned legacy[]={0,1,2,3,~0u};
    check(LegacyPreferences(legacy,5,false),"SL1 known features can establish non-FG ownership");
    check(!LegacyPreferences(legacy,5,true),"SL1 unknown extension cannot establish ownership");
    check(!LegacyPreferences<unsigned>(nullptr,1,false),"SL1 missing feature list rejected");
    check(!LegacyPreferences(legacy,65,false),"SL1 invalid count rejected before reading");
    unsigned unknown[]={0,1000};check(!LegacyPreferences(unknown,2,false),"SL1 future/FG feature remains blocked");
    for(bool known:{false,true})for(bool initialized:{false,true})for(bool hooks:{false,true}){
        const bool allowed=LegacyExcluded(known,initialized,hooks);
        check(allowed==(known&&initialized&&hooks),"SL1 init acknowledgement needs all three conditions");
        check(Reason(true,true,allowed)==NativeComponent,"late native DLL always blocks SL1 universal");
    }
    for(bool universal:{false,true})for(unsigned feature:{0u,1u,2u,3u,~0u,4u,1000u,1001u})for(bool value:{false,true}){
        int calls=0;bool result=LegacyCall(universal,feature,[&]{++calls;return value;});
        const bool denied=universal&&!LegacyFeature(feature);
        check(calls==(denied?0:1),"SL1 late feature gates prevent unknown ownership without suppressing SR/Reflex");
        check(result==(!denied&&value),"SL1 allowed calls retain original true/false result");
    }
    for(bool universal:{false,true})for(bool fg:{false,true}){
        int calls=0;auto result=FeatureCall(universal,fg,-7,[&]{++calls;return 5;});
        check(calls==(universal&&fg?0:1),"only universal native-FG calls are suppressed");
        check(result==(universal&&fg?-7:5),"native and non-FG calls retain exact results");
    }
}
