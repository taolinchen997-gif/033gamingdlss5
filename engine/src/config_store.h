// Preserve foreign settings and commit the complete file with an atomic rename.
#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <functional>
#include <atomic>
#include <io.h>
namespace configstore {
static bool Update(const std::string& path, const char* const* keys, size_t count,
                   const std::function<void(FILE*)>& write) {
    std::string keep;
    FILE* input=nullptr;
    if (fopen_s(&input,path.c_str(),"rb") == 0 && input) {
        std::string line; int ch;
        auto append=[&] {
            auto eq=line.find('='); bool owned=false;
            if(eq!=std::string::npos) for(size_t i=0;i<count;++i)
                if(line.compare(0,eq,keys[i])==0 && eq==std::strlen(keys[i])) {owned=true;break;}
            if(!owned)keep+=line;
            line.clear();
        };
        while((ch=std::fgetc(input))!=EOF) {line+=char(ch);if(ch=='\n')append();}
        if(!line.empty())append();
        bool ok=!std::ferror(input);std::fclose(input);if(!ok)return false;
    } else if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND) {
        return false; // Never replace an unreadable original with defaults.
    }
    if(!keep.empty() && keep.back()!='\n')keep+='\n';
    static std::atomic<unsigned> serial{0};
    std::string temp=path+".tmp."+std::to_string(GetCurrentProcessId())+"."+std::to_string(++serial);
    FILE* output=nullptr;
    if(fopen_s(&output,temp.c_str(),"wbx")!=0 || !output)return false;
    bool ok=std::fwrite(keep.data(),1,keep.size(),output)==keep.size();
    if(ok)write(output);
    ok=ok && !std::ferror(output);
    if(std::fflush(output)!=0)ok=false;
    if(_commit(_fileno(output))!=0)ok=false;
    if(std::fclose(output)!=0)ok=false;
    if(ok)ok=MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileA(temp.c_str());
    return ok;
}
struct Debounce {
    unsigned signature=0;
    ULONGLONG changed=0,retry=0,saved=0;
    bool initialized=false,dirty=false,has_saved=false;
    void Init(unsigned sig) {signature=sig;initialized=true;dirty=false;}
    bool Due(unsigned sig,ULONGLONG now,bool force=false) {
        if(!initialized){Init(sig);return false;}
        if(sig!=signature){signature=sig;changed=now;dirty=true;retry=0;}
        return dirty && (force || (now-changed>=1000 && now>=retry));
    }
    void Completed(bool success,ULONGLONG now) {
        if(success){dirty=false;saved=now;has_saved=true;}else retry=now+1000;
    }
};
}
