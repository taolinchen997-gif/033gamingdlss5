#define K033_MONOLITHIC_ENGINE
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdexcept>
static std::string game_dir(){char p[MAX_PATH]{};GetModuleFileNameA(nullptr,p,MAX_PATH);std::string s(p);return s.substr(0,s.find_last_of('\\'));}
static void Log(const char*,...){}
#include "safemode.h"
static void Require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
    const auto proxy=safemode::game_dir_w()+L"\\dxgi.dll";
    const auto record=safemode::game_dir_w()+L"\\_安装记录.txt";
    FILE* f=nullptr;_wfopen_s(&f,proxy.c_str(),L"wb");Require(f!=nullptr,"test proxy create");fputs("test proxy must remain",f);fclose(f);
    _wfopen_s(&f,record.c_str(),L"wb");Require(f!=nullptr,"test record create");fputs("proxy=dxgi.dll\nproxyall=dxgi.dll\n",f);fclose(f);
    for(int n:{0,1,2,3,99}){
        safemode::s_level=0;safemode::s_leveled=false;safemode::s_attached=false;
        safemode::write_count(n);
        Require(safemode::on_attach(),"adapter must remain available");
        Require(safemode::off()==(n>=2),"NR suspension threshold");
        Require(GetFileAttributesW(proxy.c_str())!=INVALID_FILE_ATTRIBUTES,"proxy renamed by heuristic");
        Require(GetFileAttributesW((proxy+L".dlss5-off").c_str())==INVALID_FILE_ATTRIBUTES,"unexpected disarm file");
        if(n>=2){safemode::retry();Require(safemode::off()&&safemode::read_count()==0,"retry must wait for complete startup registration");}
    }
    _wfopen_s(&f,proxy.c_str(),L"rb");char text[64]{};fread(text,1,63,f);fclose(f);
    Require(std::string(text)=="test proxy must remain","proxy content changed");
    printf("Single-engine safety: 5 abnormal-exit states; no proxy mutation; restart-only recovery PASS\n");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
