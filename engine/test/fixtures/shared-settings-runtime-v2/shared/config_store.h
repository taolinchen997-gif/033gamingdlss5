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
namespace detail {
struct Narrow {
    using Path=std::string;
    static int Open(FILE** f,const Path& p,bool write){return fopen_s(f,p.c_str(),write?"wbx":"rb");}
    static DWORD Attributes(const Path& p){return GetFileAttributesA(p.c_str());}
    static bool Replace(const Path& a,const Path& b){return MoveFileExA(a.c_str(),b.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
    static void Remove(const Path& p){DeleteFileA(p.c_str());}
    static Path Suffix(unsigned serial){return ".tmp."+std::to_string(GetCurrentProcessId())+"."+std::to_string(serial);}
    static HANDLE LockFile(const Path& p){return CreateFileA((p+".033lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);}
};
struct Wide {
    using Path=std::wstring;
    static int Open(FILE** f,const Path& p,bool write){return _wfopen_s(f,p.c_str(),write?L"wbx":L"rb");}
    static DWORD Attributes(const Path& p){return GetFileAttributesW(p.c_str());}
    static bool Replace(const Path& a,const Path& b){return MoveFileExW(a.c_str(),b.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
    static void Remove(const Path& p){DeleteFileW(p.c_str());}
    static Path Suffix(unsigned serial){return L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(serial);}
    static HANDLE LockFile(const Path& p){return CreateFileW((p+L".033lock").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);}
};
// Retain the same lock file across renames and process lifetimes. Never delete
// it on unlock: another writer may already hold a handle to this exact file.
template<class Ops>struct WriteLock {
    HANDLE handle=INVALID_HANDLE_VALUE;OVERLAPPED range{};bool owned=false;
    explicit WriteLock(const typename Ops::Path& path):handle(Ops::LockFile(path)){
        if(handle!=INVALID_HANDLE_VALUE)owned=LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range)!=FALSE;
    }
    ~WriteLock(){DWORD error=GetLastError();if(owned)UnlockFileEx(handle,0,1,0,&range);if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);SetLastError(error);}
    WriteLock(const WriteLock&)=delete;WriteLock& operator=(const WriteLock&)=delete;
};
template<class Ops>static bool Update(const typename Ops::Path& path, const char* const* keys, size_t count,
                   const std::function<void(FILE*)>& write, size_t max_bytes=size_t(-1)) {
    WriteLock<Ops> lock(path);if(!lock.owned)return false;
    std::string keep;
    FILE* input=nullptr;
    if (Ops::Open(&input,path,false) == 0 && input) {
        std::string line; int ch;size_t bytes=0;
        auto append=[&] {
            auto eq=line.find('='); bool owned=false;
            if(eq!=std::string::npos) for(size_t i=0;i<count;++i)
                if(line.compare(0,eq,keys[i])==0 && eq==std::strlen(keys[i])) {owned=true;break;}
            if(!owned)keep+=line;
            line.clear();
        };
        while((ch=std::fgetc(input))!=EOF) {if(bytes++==max_bytes){std::fclose(input);return false;}line+=char(ch);if(ch=='\n')append();}
        if(!line.empty())append();
        bool ok=!std::ferror(input);std::fclose(input);if(!ok)return false;
    } else if(Ops::Attributes(path)!=INVALID_FILE_ATTRIBUTES || GetLastError()!=ERROR_FILE_NOT_FOUND) {
        return false; // Never replace an unreadable original with defaults.
    }
    if(!keep.empty() && keep.back()!='\n')keep+='\n';
    static std::atomic<unsigned> serial{0};
    auto temp=path+Ops::Suffix(++serial);
    FILE* output=nullptr;
    if(Ops::Open(&output,temp,true)!=0 || !output)return false;
    bool ok=std::fwrite(keep.data(),1,keep.size(),output)==keep.size();
    if(ok)write(output);
    ok=ok && !std::ferror(output);
    auto size=_ftelli64(output);if(size<0||uint64_t(size)>max_bytes)ok=false;
    if(std::fflush(output)!=0)ok=false;
    if(_commit(_fileno(output))!=0)ok=false;
    if(std::fclose(output)!=0)ok=false;
    if(ok)ok=Ops::Replace(temp,path);
    if(!ok)Ops::Remove(temp);
    return ok;
}
}
static bool Update(const std::string& path,const char* const* keys,size_t count,const std::function<void(FILE*)>& write,size_t max_bytes=size_t(-1)){return detail::Update<detail::Narrow>(path,keys,count,write,max_bytes);}
static bool Update(const std::wstring& path,const char* const* keys,size_t count,const std::function<void(FILE*)>& write,size_t max_bytes=size_t(-1)){return detail::Update<detail::Wide>(path,keys,count,write,max_bytes);}
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
