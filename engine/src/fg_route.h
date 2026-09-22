// One presentation route per process. Selection changes only the next launch.
#pragma once
#include <Windows.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "config_store.h"
#include "fg_capability_policy.h"
#ifdef K033_BETA2_RESHADE_HOST
#include "beta2_fg_bridge.h"
#endif
namespace fgroute033 {
enum Route : unsigned { Native=0, Universal=1 };
enum Block : unsigned { None=0, NativeComponent=1, StreamlineNotExcluded=2 };
struct Preferences {unsigned route=Universal;bool automatic=true;};
inline Preferences ParsePreferences(std::istream& input) {
    std::string line;unsigned legacy=Universal;int route=-1,automatic=-1;bool legacySet=false;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line=="imagefg=1"){legacy=Universal;legacySet=true;}
        else if(line=="imagefg=0"){legacy=Native;legacySet=true;}
        else if(line=="fgroute=0")route=Native;
        else if(line=="fgroute=1")route=Universal;
        else if(line=="fgrouteauto=0")automatic=0;
        else if(line=="fgrouteauto=1")automatic=1;
    }
    return {route<0?legacy:unsigned(route),automatic<0?(route<0&&!legacySet):automatic!=0};
}
inline unsigned Parse(std::istream& input){return ParsePreferences(input).route;}
// ★通用补帧总闸★ 2026-09-12 业主：「这个通用是根本没什么用」—— 前后帧算中间帧那套整段下线。
//   帧数计数器涨、观感不跟着涨；而它带来的故障是实打实的：全局设置里留下一个 fg_route=1，
//   下次启动 033 就去接管交换链，第一帧黑屏然后闪退（评论区多例）；生化危机4 那条路线出厂
//   还默认开着 3×，表现就是一直闪屏。帧生成统一交给游戏自己的 DLSS-G（033 解锁到 6×）
//   或随包转接件。
//   关法：这个开关一关，Boot() 永远是原生，所有执行入口（DxgiFactory_Hooks / FG_Hooks /
//   Streamline_Hooks / universal_fg_service::Prepared）本来就靠 UniversalBoot() 把门，一起关死。
// Retired provider is an explicit experimental build option. The shipping
// build cannot be reactivated by old settings or a mutable global switch.
#ifdef K033_ENABLE_RETIRED_UNIVERSAL_FG
inline std::atomic<bool> universalOffered{false};
inline bool UniversalOffered(){return universalOffered.load();}
#else
inline constexpr bool UniversalOffered(){return false;}
#endif
class Selection {
    const unsigned boot;
    std::atomic<unsigned> selected;
    std::atomic<bool> automatic;
public:
    // 总闸关着时 boot 一律原生：老配置里写着 imagefg=1 / fgroute=1 的机器也回不到通用路线。
    // 这就是「第一帧黑屏闪退」的解药 —— 玩家不用去动任何文件，装上就好了。
    explicit Selection(unsigned value,bool useDefault=false)
        :boot(value==Universal&&UniversalOffered()?Universal:Native),selected(boot),automatic(useDefault){}
    explicit Selection(Preferences value):Selection(value.route,value.automatic){}
    unsigned Boot()const{return boot;}
    unsigned Selected()const{return selected.load();}
    bool Pending()const{return Boot()!=Selected();}
    bool Automatic()const{return automatic.load();}
    template<class Save> bool Select(unsigned value,bool nativeAllowed,Save save) {
        if(value==Universal&&!UniversalOffered())return false;   // 总闸关着，面板选不动
        if(!fgcap033::Selectable(value,nativeAllowed))return false;
        if(!save(value))return false;
        selected=value;automatic=false;return true;
    }
    template<class Save> bool Resolve(bool nativeAllowed,Save save){
        // Never substitute a live swapchain. Only choose the next launch.
        if(!Automatic() && (Selected()==Universal||nativeAllowed))return true;
        const auto target=fgcap033::DefaultRoute(nativeAllowed);
        // 2026-09-12「这个版本我的 dlss 帧生成没了？？？」：DefaultRoute 改成一律返回原生之后，本来正在用
        // 通用补帧、automatic 还是 1 的玩家，只要打开一次面板就被写回原生，下次启动帧生成整个消失 ——
        // 而这个游戏根本没有原生帧生成输入，改过去等于什么都没有。原生不可用时不许降级已经跑起来的通用路线。
        if(target==Native&&!nativeAllowed&&Boot()==Universal)return true;
        if(Selected()==target)return true;
        if(!save(target))return false;
        selected=target;automatic=true;return true;
    }
};
inline std::string ConfigPath(){char file[32768]{};GetModuleFileNameA(nullptr,file,32768);
    std::string path=file;path.resize(path.find_last_of("\\/")+1);return path+"dlss5-033.cfg";}
inline Selection& State(){static Selection state([]{
#ifdef K033_BETA2_RESHADE_HOST
    K033_Beta2FgSettings s;if(!k033beta2::ReadFg(s))return Preferences{Native,true};
    return Preferences{s.route,s.automatic!=0};
#else
    std::ifstream input(ConfigPath());return ParsePreferences(input);
#endif
}());return state;}
inline fgcap033::Evidence nativeCapability;
inline bool UniversalBoot(){return UniversalOffered() && State().Boot()==Universal;}
inline std::mutex settingsMutex;
inline bool SaveChoice(const std::string& path,unsigned route,bool automatic=false){if(route>Universal)return false;
#ifdef K033_BETA2_RESHADE_HOST
    (void)path;return k033beta2::SetFgRoute(route,automatic);
#else
    const char* keys[]={"fgroute","imagefg","fgrouteauto"};
    return configstore::Update(path,keys,3,[&](FILE* file){std::fprintf(file,"fgroute=%u\nimagefg=%u\nfgrouteauto=%u\n",route,route,automatic?1:0);});
#endif
}
inline int Select(int route){if(route<0||route>int(Universal))return 0;
    auto& state=State();std::lock_guard<std::mutex> lock(settingsMutex);
    return state.Select(unsigned(route),nativeCapability.NativeAllowed(),[](unsigned value){return SaveChoice(ConfigPath(),value);})?1:0;}
// Called from the settings UI, not from a feature hook or GPU submission.
// No disk work is added to feature/submit hooks. Save failure leaves choice intact.
inline bool ResolveDefault(){std::lock_guard<std::mutex> lock(settingsMutex);
    static unsigned failedAttempt=~0u;
    const unsigned attempt=nativeCapability.Read()*8+State().Selected()*2+unsigned(State().Automatic());
    if(failedAttempt==attempt)return false; // retry after capability/selection changes, not each frame
    const bool saved=State().Resolve(nativeCapability.NativeAllowed(),[](unsigned value){return SaveChoice(ConfigPath(),value,true);});
    failedAttempt=saved?~0u:attempt;return saved;}
// Streamline 2 must pass through the filtered slInit, with all late-load hooks
// installed. Merely seeing a DLL or an 'off' option is not ownership evidence.
inline std::atomic<bool> streamlineHooksReady{false},streamlineExcluded{false};
// SL1.0 has DLSS SR, NRD, NIS, Reflex and the common plugin; no DLSS-G.
// Still require a successful observed init AND guards on late feature calls.
// An unfamiliar feature/extension never establishes universal ownership.
inline bool LegacyFeature(unsigned feature){return feature<=3u || feature==~0u;}
template<class Feature> bool LegacyPreferences(const Feature* features,unsigned count,bool extension){
    if(extension || count>64 || (count && !features))return false;
    for(unsigned i=0;i<count;++i)if(!LegacyFeature(unsigned(features[i])))return false;
    return true;
}
inline bool LegacyExcluded(bool preferences,bool initialized,bool hooks){return preferences&&initialized&&hooks;}
template<class Invoke> bool LegacyCall(bool universal,unsigned feature,Invoke invoke){
    return universal&&!LegacyFeature(feature)?false:invoke();
}
inline unsigned Reason(bool nativeLoaded,bool streamlineLoaded,bool excluded){
    if(nativeLoaded)return NativeComponent;
    return streamlineLoaded&&!excluded?StreamlineNotExcluded:None;
}
inline unsigned Blocked(){if(!UniversalBoot())return None;
    return Reason(GetModuleHandleW(L"nvngx_dlssg.dll")||GetModuleHandleW(L"sl.dlss_g.dll"),
                  GetModuleHandleW(L"sl.interposer.dll")!=nullptr,streamlineExcluded.load());}
inline bool CanGenerate(){return UniversalBoot()&&Blocked()==None;}
// Used by actual slInit and CPU checks; leaves every non-FG feature in order.
template<class Feature> std::vector<Feature> WithoutFramegen(const Feature* input,unsigned count,Feature fg){
    std::vector<Feature> output;output.reserve(count);
    for(unsigned i=0;i<count;++i)if(input[i]!=fg)output.push_back(input[i]);return output;
}
template<class Result,class Invoke> Result FeatureCall(bool universal,bool isFramegen,Result blocked,Invoke invoke){
    return universal&&isFramegen?blocked:invoke();
}
}
