#pragma once
#include "config_store.h"
#include "nr_settings.h"
#include "../src/backend.h"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <locale.h>
// One schema/path, separately owned grade/NR groups, one file transaction lock.
// Runtime appearance epochs never persist or travel between game processes.
namespace k033settings {
// Explicit locale object, never setlocale() in the game process.
inline _locale_t numeric_locale(){static auto value=_create_locale(LC_NUMERIC,"C");return value;}
inline const char* keys[]={"033_runtime_version","enabled","style","exposure","contrast","saturation","warmth","tint","highlights","style_strength"};
inline const std::array<std::string,30>& nr_keys(){
    static const auto names=[] {std::array<std::string,30> a{};a[0]="033_nr_version";a[1]="nr_enabled";a[2]="nr_layers";
        const char* fields[]={"style","preset","auto_mask","ui_correction","intensity","local_structure","local_tone","global_tone"};
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<8;++j)a[3+i*8+j]="nr_layer"+std::to_string(i+1)+"_"+fields[j];
        // Legacy v1 readers ignore extension keys outside their nr_ namespace.
        for(unsigned i=0;i<3;++i)a[27+i]="033_nr_sr_work"+std::to_string(i+1);return a;}();return names;
}
template<class Char>bool path_valid(const Char* path){return path&&*path;}
inline int open_settings(FILE** f,const char* p){return fopen_s(f,p,"rb");}
inline int open_settings(FILE** f,const wchar_t* p){return _wfopen_s(f,p,L"rb");}
inline bool number(const char* text,float& value){
    if(!numeric_locale())return false;errno=0;char* end=nullptr;value=_strtof_l(text,&end,numeric_locale());
    while(end&&(*end==' '||*end=='\r'||*end=='\n'||*end=='\t'))++end;
    return end!=text&&end&&!*end&&!errno&&std::isfinite(value);
}
inline bool integer(float v,unsigned max){return v>=0&&v<=max&&std::floor(v)==v;}
template<class Char>bool read_settings(const Char* path,K033_Settings& out,K033_NrSettings& nr_out){
    const auto& names=nr_keys(); // allocate schema strings before owning FILE
    if(!numeric_locale())return false;
    FILE* f=nullptr;if(!path_valid(path)||open_settings(&f,path)||!f)return false;
    char line[1024];bool seen[10]={},seen_nr[30]={},ok=true,has_nr=false;auto s=k033::defaults();auto nr=k033::nr_defaults(false);
    while(ok&&std::fgets(line,sizeof(line),f)){
        auto bytes=_ftelli64(f);if(bytes<0||bytes>256*1024){ok=false;break;}
        if(!std::strchr(line,'\n')&&!std::feof(f)){ok=false;break;}
        char* eq=std::strchr(line,'=');if(!eq)continue;*eq++=0;
        unsigned index=10,nindex=30;for(unsigned i=0;i<10;++i)if(!std::strcmp(line,keys[i])){index=i;break;}
        if(index==10)for(unsigned i=0;i<30;++i)if(names[i]==line){nindex=i;break;}
        if(index==10&&nindex==30){if(!std::strncmp(line,"nr_",3))ok=false;continue;}
        float v=0;if(!number(eq,v)){ok=false;break;}
        if(index<10){if(seen[index]){ok=false;break;}seen[index]=true;
            if(index<3&&!integer(v,3)){ok=false;break;}
            switch(index){case 0:ok=v==1;break;case 1:s.enabled=uint32_t(v);break;case 2:s.style=uint32_t(v);break;
                case 3:s.exposure=v;break;case 4:s.contrast=v;break;case 5:s.saturation=v;break;case 6:s.warmth=v;break;
                case 7:s.tint=v;break;case 8:s.highlights=v;break;case 9:s.style_strength=v;break;}
        }else{has_nr=true;if(seen_nr[nindex]){ok=false;break;}seen_nr[nindex]=true;
            if(nindex>=27){if(!integer(v,200)||!k033::nr_sr_work(nindex-27,uint32_t(v))){ok=false;break;}nr.sr_work[nindex-27]=uint32_t(v);}
            else if(nindex<3){if(!integer(v,3)){ok=false;break;}
                if(nindex==0)ok=v==1;else if(nindex==1)nr.enabled=uint32_t(v);else nr.layers=uint32_t(v);
            }else{unsigned field=(nindex-3)%8;auto& p=nr.layer[(nindex-3)/8];if(field<4&&!integer(v,3)){ok=false;break;}
                switch(field){case 0:p.style=uint32_t(v);break;case 1:p.preset=uint32_t(v);break;case 2:p.auto_mask=uint32_t(v);break;case 3:p.ui_correction=uint32_t(v);break;
                    case 4:p.intensity=v;break;case 5:p.local_structure=v;break;case 6:p.local_tone=v;break;case 7:p.global_tone=v;break;}
            }
        }
    }
    ok=ok&&!std::ferror(f)&&seen[0]&&(!has_nr||seen_nr[0])&&k033::valid(s)&&k033::nr_settings(nr);std::fclose(f);
    if(ok){out=s;nr_out=nr;}return ok;
}
template<class Char>bool read_settings(const Char* path,K033_Settings& out){K033_NrSettings nr;return read_settings(path,out,nr);}
inline void print_grade(FILE* f,const K033_Settings& s){
    _fprintf_l(f,"033_runtime_version=1\nenabled=%u\nstyle=%u\nexposure=%.9g\ncontrast=%.9g\nsaturation=%.9g\nwarmth=%.9g\ntint=%.9g\nhighlights=%.9g\nstyle_strength=%.9g\n",numeric_locale(),
        s.enabled,s.style,s.exposure,s.contrast,s.saturation,s.warmth,s.tint,s.highlights,s.style_strength);
}
inline void print_nr(FILE* f,const K033_NrSettings& nr){
    std::fprintf(f,"033_nr_version=1\nnr_enabled=%u\nnr_layers=%u\n",nr.enabled,nr.layers);
    std::fprintf(f,"033_nr_sr_work1=%u\n033_nr_sr_work2=%u\n033_nr_sr_work3=%u\n",nr.sr_work[0],nr.sr_work[1],nr.sr_work[2]);
    for(unsigned i=0;i<3;++i){const auto& p=nr.layer[i];unsigned n=i+1;
        _fprintf_l(f,"nr_layer%u_style=%u\nnr_layer%u_preset=%u\nnr_layer%u_auto_mask=%u\nnr_layer%u_ui_correction=%u\nnr_layer%u_intensity=%.9g\nnr_layer%u_local_structure=%.9g\nnr_layer%u_local_tone=%.9g\nnr_layer%u_global_tone=%.9g\n",numeric_locale(),
            n,p.style,n,p.preset,n,p.auto_mask,n,p.ui_correction,n,p.intensity,n,p.local_structure,n,p.local_tone,n,p.global_tone);
    }
}
template<class Char>bool write_groups(const Char* path,const K033_Settings* s,const K033_NrSettings* nr){
    if(!path_valid(path)||(!s&&!nr)||(s&&!k033::valid(*s))||(nr&&!k033::nr_settings(*nr))||!numeric_locale())return false;
    std::array<const char*,40> owned{};unsigned count=0;
    if(s)for(auto key:keys)owned[count++]=key;else owned[count++]=keys[0];
    if(nr)for(const auto& key:nr_keys())owned[count++]=key.c_str();
    return configstore::Update(path,owned.data(),count,[&](FILE* f){if(s)print_grade(f,*s);else std::fputs("033_runtime_version=1\n",f);if(nr)print_nr(f,*nr);},256*1024);
}
template<class Char>bool write_settings(const Char* path,const K033_Settings& s){return write_groups(path,&s,nullptr);}
template<class Char>bool write_settings(const Char* path,const K033_Settings& s,const K033_NrSettings& nr){return write_groups(path,&s,&nr);}
template<class Char>bool write_nr(const Char* path,const K033_NrSettings& nr){return write_groups(path,nullptr,&nr);}
}
