#pragma once
#include "beta2_fg_settings.h"
#include "settings_codec.h"
#include "preferences_mailbox.h"
namespace k033settings {
inline constexpr const char* fg_keys[]={"033_fg_version","fg_route","fg_automatic","fg_native_multiplier",
    "fg_universal_multiplier","fg_universal_enabled","fg_native_enabled","fg_max_count","fg_temporal_fix",
    "fg_force_flip_meter_off","fg_raise_ceiling","fg_force_ota"};
inline std::array<uint32_t,12> fg_values(const K033_Beta2FgSettings& s){
    return {s.version,s.route,s.automatic,s.native_multiplier,s.universal_multiplier,s.universal_enabled,
        s.native_enabled,s.max_count,s.temporal_fix,s.force_flip_meter_off,s.raise_ceiling,s.force_ota};
}
inline K033_Beta2FgSettings fg_from_values(const std::array<uint32_t,12>& v){
    K033_Beta2FgSettings s;s.version=v[0];s.route=v[1];s.automatic=v[2];s.native_multiplier=v[3];
    s.universal_multiplier=v[4];s.universal_enabled=v[5];s.native_enabled=v[6];s.max_count=v[7];
    s.temporal_fix=v[8];s.force_flip_meter_off=v[9];s.raise_ceiling=v[10];s.force_ota=v[11];return s;
}
template<class Char>bool read_fg(const Char* path,K033_Beta2FgSettings& out){
    FILE* file=nullptr;if(!path_valid(path)||open_settings(&file,path)||!file)return false;
    auto values=fg_values(k033beta2::fg_defaults());bool seen[12]={},any=false,ok=true;char line[1024];
    while(ok&&std::fgets(line,sizeof(line),file)){
        auto bytes=_ftelli64(file);if(bytes<0||bytes>256*1024){ok=false;break;}
        if(!std::strchr(line,'\n')&&!std::feof(file)){ok=false;break;}
        char* equal=std::strchr(line,'=');if(!equal)continue;*equal++=0;
        unsigned index=12;for(unsigned i=0;i<12;++i)if(!std::strcmp(line,fg_keys[i])){index=i;break;}
        if(index==12){if(!std::strncmp(line,"fg_",3)||!std::strncmp(line,"033_fg_",7))ok=false;continue;}
        float value=0;if(seen[index]||!number(equal,value)||!integer(value,6)){ok=false;break;}
        seen[index]=true;any=true;values[index]=uint32_t(value);
    }
    const auto candidate=fg_from_values(values);
    ok=ok&&!std::ferror(file)&&(!any||seen[0])&&k033beta2::fg_valid(candidate);std::fclose(file);
    if(ok)out=candidate;return ok;
}
template<class Char>bool write_fg(const Char* path,const K033_Beta2FgSettings& s){
    if(!path_valid(path)||!k033beta2::fg_valid(s))return false;
    const auto values=fg_values(s);
    std::array<const char*,13> owned{};owned[0]="033_runtime_version";
    for(unsigned i=0;i<12;++i)owned[i+1]=fg_keys[i];
    return configstore::Update(path,owned.data(),owned.size(),[&](FILE* file){
        std::fputs("033_runtime_version=1\n",file);
        for(unsigned i=0;i<12;++i)std::fprintf(file,"%s=%u\n",fg_keys[i],values[i]);
    },256*1024);
}
struct FgPreferences {
    using Value=K033_Beta2FgSettings;static Value defaults(){return k033beta2::fg_defaults();}
    static bool valid(const Value& v){return k033beta2::fg_valid(v);}static Value normalize(const Value& v){return v;}
};
using FgPreferencesMailbox=PreferencesMailboxT<FgPreferences>;
}
