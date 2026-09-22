// RTX 20/30 frame-generation bridge (nvidia_mfg_bridge, dlssg-to-fsr3 lineage) as it
// sits beside the game executable. File facts and its INI only: nothing here loads,
// calls or patches the bridge, and no answer depends on the GPU model.
#pragma once
#include <Windows.h>
#include "mfg2030_identity.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
namespace bridge2030 {
enum class Layout : unsigned { None=0, Legacy=1, Single=2 };
struct Files { Layout layout=Layout::None; char loader[32]={}; bool ini=false,sm75=false,sm86=false; };
// -1: key absent from its section. The bridge then applies its own default (FGMode 0 = FSR3.1).
struct Settings { int mode=-1,force=-1,menu=-1; };
// v0.4x kept the generator in its own module next to a renamed loader.
inline constexpr const char* LegacyModules[]={"nvidia_mfg_bridge.dll","dlssg_to_fsr3_amd_is_better.dll"};
// v0.50x ships a single nvngx.dll that users rename; a generic loader name alone is not identity.
inline constexpr const char* LoaderNames[]={"version.dll","winhttp.dll","dbghelp.dll","cryptsp.dll","nvngx.dll","XINPUT9_1_0.dll","IPHLPAPI.dll"};
// The loader opens its INI and reads the override key by wide string (static audit of the v0.43
// version.dll and the v0.501 nvngx.dll). Every marker must be present, as ASCII or UTF-16LE.
inline constexpr const char* LoaderMarkers[]={"dlssg_to_fsr3.ini","ForceFrameGenOverride"};
inline constexpr char IniName[]="dlssg_to_fsr3.ini";
inline constexpr char ProviderSm75[]="nvngx_dlssg.sm75.dll";
inline constexpr char ProviderSm86[]="nvngx_dlssg.sm86.dll";
// v0.501 reads FGMode/FGProviderArch from [Setting] and the multiplier keys from [Debug];
// the same key in any other section is not the bridge's setting.
inline const char* SectionOf(const char* key){
    return _stricmp(key,"FGMode")==0||_stricmp(key,"FGProviderArch")==0?"Setting":"Debug";
}

struct Stamp { bool exists=false; ULONGLONG size=0,written=0; };
inline Stamp StampOf(const std::string& path){
    WIN32_FILE_ATTRIBUTE_DATA data{};Stamp stamp;
    if(!GetFileAttributesExA(path.c_str(),GetFileExInfoStandard,&data)||(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return stamp;
    stamp.exists=true;stamp.size=(ULONGLONG(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
    stamp.written=(ULONGLONG(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
    return stamp;
}
// Bounded sequential read. A marker split across two reads is still found.
inline bool HasLoaderMarkers(const std::string& path,ULONGLONG maxBytes=ULONGLONG(512)<<20){
    const size_t count=sizeof(LoaderMarkers)/sizeof(LoaderMarkers[0]);
    std::vector<std::string> ascii,wide;size_t longest=0;
    for(const char* marker:LoaderMarkers){
        std::string narrow(marker),doubled;for(char c:narrow){doubled+=c;doubled+='\0';}
        longest=(std::max)(longest,doubled.size());ascii.push_back(narrow);wide.push_back(doubled);
    }
    HANDLE file=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    std::vector<bool> found(count,false);size_t remaining=count;
    std::vector<char> chunk(size_t(1)<<20);std::string window;ULONGLONG total=0;DWORD read=0;
    while(remaining&&total<maxBytes&&ReadFile(file,chunk.data(),DWORD(chunk.size()),&read,nullptr)&&read){
        total+=read;window.append(chunk.data(),read);
        for(size_t i=0;i<count;++i)
            if(!found[i]&&(window.find(ascii[i])!=std::string::npos||window.find(wide[i])!=std::string::npos)){found[i]=true;--remaining;}
        if(window.size()>=longest)window.erase(0,window.size()-(longest-1));
    }
    CloseHandle(file);
    return remaining==0;
}
struct LoaderScan { std::string path; Stamp stamp; bool match=false,embedded=false; };
// Pure directory facts (dir ends with a separator). Scan results are reused while an
// image keeps its size and write time.
inline Files ProbeDirectory(const std::string& dir,std::vector<LoaderScan>& scans){
    Files files;
    files.ini=StampOf(dir+IniName).exists;files.sm75=StampOf(dir+ProviderSm75).exists;files.sm86=StampOf(dir+ProviderSm86).exists;
    for(const char* name:LegacyModules){
        if(StampOf(dir+name).exists){files.layout=Layout::Legacy;std::snprintf(files.loader,sizeof(files.loader),"%s",name);return files;}
    }
    for(const char* name:LoaderNames){
        const std::string path=dir+name;const Stamp stamp=StampOf(path);
        if(!stamp.exists)continue;
        auto scan=std::find_if(scans.begin(),scans.end(),[&](const LoaderScan& s){return s.path==path;});
        if(scan==scans.end()){scans.push_back(LoaderScan{path});scan=scans.end()-1;}
        if(!scan->stamp.exists||scan->stamp.size!=stamp.size||scan->stamp.written!=stamp.written){scan->stamp=stamp;scan->match=HasLoaderMarkers(path);const auto known=scan->match?bridge2030identity::FileA(path.c_str()):nullptr;scan->embedded=known&&known->embedded;}
        if(scan->match){files.sm75=files.sm75||scan->embedded;files.sm86=files.sm86||scan->embedded;files.layout=Layout::Single;std::snprintf(files.loader,sizeof(files.loader),"%s",name);return files;}
    }
    return files;
}
inline std::string Trim(const std::string& text){
    const auto first=text.find_first_not_of(" \t\r");if(first==std::string::npos)return {};
    const auto last=text.find_last_not_of(" \t\r");return text.substr(first,last-first+1);
}
// "[Name]" -> "Name" (the line is already trimmed and starts with '[').
inline std::string SectionName(const std::string& line){
    const auto close=line.find(']');
    return Trim(line.substr(1,close==std::string::npos?std::string::npos:close-1));
}
inline Settings ParseSettings(const std::string& text){
    Settings settings;std::string section;size_t begin=0;
    while(begin<text.size()){
        size_t end=text.find('\n',begin);if(end==std::string::npos)end=text.size();
        const std::string line=Trim(text.substr(begin,end-begin));begin=end+1;
        if(line.empty()||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='['){section=SectionName(line);continue;}
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        const std::string key=Trim(line.substr(0,eq)),value=Trim(line.substr(eq+1));
        if(_stricmp(section.c_str(),SectionOf(key.c_str()))!=0)continue;
        char* stop=nullptr;const long number=std::strtol(value.c_str(),&stop,10);
        if(stop==value.c_str())continue;
        if(_stricmp(key.c_str(),"FGMode")==0)settings.mode=int(number);
        else if(_stricmp(key.c_str(),"ForceFrameGenOverride")==0)settings.force=int(number);
        else if(_stricmp(key.c_str(),"EnableFrameGenOverrideMenu")==0)settings.menu=int(number);
    }
    return settings;
}
// Rewrites each key's value on its line inside the key's own section (first occurrence; later
// duplicates there are dropped) and inserts missing keys after the last entry of that section,
// adding the section at the end when it is absent. Every other byte, comment, same-named key in
// another section and the file's line endings stay as they were.
inline std::string WithValues(const std::string& text,const std::vector<std::pair<const char*,int>>& values){
    const std::string eol=text.empty()||text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    struct Line { std::string raw,section; bool entry; };
    std::vector<Line> out;std::vector<bool> written(values.size(),false);std::string section;
    for(size_t begin=0;begin<text.size();){
        const size_t end=text.find('\n',begin);const size_t stop=end==std::string::npos?text.size():end+1;
        const std::string raw=text.substr(begin,stop-begin);begin=stop;
        const std::string line=Trim(raw);
        if(!line.empty()&&line[0]=='['){section=SectionName(line);out.push_back({raw,section,true});continue;}
        const auto eq=line.find('=');
        const bool pair=!line.empty()&&line[0]!=';'&&line[0]!='#'&&eq!=std::string::npos;
        bool owned=false;
        if(pair){
            const std::string key=Trim(line.substr(0,eq));
            for(size_t i=0;i<values.size();++i){
                if(_stricmp(key.c_str(),values[i].first)!=0||_stricmp(section.c_str(),SectionOf(values[i].first))!=0)continue;
                owned=true;
                if(!written[i]){
                    written[i]=true;
                    const bool newline=!raw.empty()&&raw.back()=='\n',crlf=raw.size()>=2&&raw[raw.size()-2]=='\r';
                    out.push_back({std::string(values[i].first)+"="+std::to_string(values[i].second)+(newline?(crlf?"\r\n":"\n"):""),section,true});
                }
                break;
            }
        }
        if(!owned)out.push_back({raw,section,pair});
    }
    for(size_t i=0;i<values.size();++i){
        if(written[i])continue;
        const std::string target=SectionOf(values[i].first);
        const std::string entry=std::string(values[i].first)+"="+std::to_string(values[i].second)+eol;
        bool found=false;size_t at=0;
        for(size_t j=out.size();j-->0;)if(out[j].entry&&_stricmp(out[j].section.c_str(),target.c_str())==0){at=j+1;found=true;break;}
        if(!found){
            if(!out.empty()&&out.back().raw.back()!='\n')out.back().raw+=eol;
            out.push_back({"["+target+"]"+eol,target,true});
            out.push_back({entry,target,true});
            continue;
        }
        if(out[at-1].raw.back()!='\n')out[at-1].raw+=eol;
        out.insert(out.begin()+std::ptrdiff_t(at),Line{entry,target,true});
    }
    std::string result;for(const auto& line:out)result+=line.raw;
    return result;
}
inline bool ReadText(const std::string& path,std::string& text){
    FILE* file=nullptr;if(fopen_s(&file,path.c_str(),"rb")!=0||!file)return false;
    char buffer[4096];size_t n=0;text.clear();
    while((n=std::fread(buffer,1,sizeof(buffer),file))>0)text.append(buffer,n);
    const bool ok=!std::ferror(file);std::fclose(file);return ok;
}
// Commit the complete file with an atomic rename, as config_store.h does.
inline bool WriteText(const std::string& path,const std::string& text){
    static std::atomic<unsigned> serial{0};
    const std::string temp=path+".tmp."+std::to_string(GetCurrentProcessId())+"."+std::to_string(++serial);
    FILE* output=nullptr;if(fopen_s(&output,temp.c_str(),"wbx")!=0||!output)return false;
    bool ok=std::fwrite(text.data(),1,text.size(),output)==text.size();
    if(std::fflush(output)!=0)ok=false;
    if(_commit(_fileno(output))!=0)ok=false;
    if(std::fclose(output)!=0)ok=false;
    if(ok)ok=MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileA(temp.c_str());
    return ok;
}
// Never creates an INI the bridge was not installed with. mode: 0 FSR3.1 / 2 NVIDIA
// model; force: 0 follow the game, 1..4 = 3x..6x; a negative value leaves that key.
inline bool WriteSettingsAt(const std::string& path,int mode,int force){
    std::vector<std::pair<const char*,int>> values;
    if(mode>=0){if(mode!=0&&mode!=2)return false;values.push_back({"FGMode",mode});}
    if(force>=0){if(force>4)return false;values.push_back({"ForceFrameGenOverride",force});}
    if(values.empty())return true;
    std::string text;
    if(!StampOf(path).exists||!ReadText(path,text))return false;
    // A saved old 2x-only switch must not silently defeat an explicit 3x..6x
    // request. Commit its prerequisite in the SAME atomic INI replacement.
    const int requested=force>=0?force:ParseSettings(text).force;
    if(requested>0)values.push_back({"EnableMultiFrameGeneration",1});
    return WriteText(path,WithValues(text,values));
}

inline const std::string& ExeDirectory(){
    static const std::string dir=[]{
        std::string path(32768,'\0');const DWORD n=GetModuleFileNameA(nullptr,path.data(),DWORD(path.size()));
        if(!n||n>=path.size())return std::string();
        path.resize(n);const size_t cut=path.find_last_of("\\/");
        return cut==std::string::npos?std::string():path.substr(0,cut+1);
    }();
    return dir;
}
struct Shared { std::mutex lock; ULONGLONG checked=0; Files files; bool seen=false; std::vector<LoaderScan> scans; Stamp iniStamp; Settings settings; };
inline Shared& Cache(){static Shared shared;return shared;}
// File attributes are read again at most every two seconds; an image is scanned again
// only when its size or write time changes.
inline Files Current(){
    auto& c=Cache();std::lock_guard<std::mutex> guard(c.lock);
    const ULONGLONG now=GetTickCount64();
    if(c.checked&&now-c.checked<2000)return c.files;
    c.checked=now;
    Files files;
    if(!ExeDirectory().empty())files=ProbeDirectory(ExeDirectory(),c.scans);
    for(const char* name:LegacyModules){
        if(files.layout!=Layout::None)break;
        if(GetModuleHandleA(name)){files.layout=Layout::Legacy;std::snprintf(files.loader,sizeof(files.loader),"%s",name);}
    }
    c.files=files;c.seen=c.seen||files.layout!=Layout::None;
    return files;
}
// Once seen, a bridge stays present for this process: its hooks may already be live.
inline bool Present(){
    const bool now=Current().layout!=Layout::None;
    auto& c=Cache();std::lock_guard<std::mutex> guard(c.lock);return now||c.seen;
}
inline Settings CurrentSettings(){
    if(ExeDirectory().empty())return {};
    const std::string path=ExeDirectory()+IniName;const Stamp stamp=StampOf(path);
    auto& c=Cache();std::lock_guard<std::mutex> guard(c.lock);
    if(!stamp.exists){c.iniStamp={};c.settings={};return c.settings;}
    if(!c.iniStamp.exists||stamp.size!=c.iniStamp.size||stamp.written!=c.iniStamp.written){
        std::string text;
        c.settings=stamp.size<=(ULONGLONG(1)<<20)&&ReadText(path,text)?ParseSettings(text):Settings{};
        c.iniStamp=stamp;
    }
    return c.settings;
}
inline bool WriteSettings(int mode,int force){
    if(ExeDirectory().empty())return false;
    const bool ok=WriteSettingsAt(ExeDirectory()+IniName,mode,force);
    auto& c=Cache();std::lock_guard<std::mutex> guard(c.lock);c.iniStamp={};
    return ok;
}
}
