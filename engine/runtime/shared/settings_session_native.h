#pragma once
#include "../include/033_runtime.h"
#include <Windows.h>
#include <shlobj.h>
#include <string>
#include <utility>

namespace k033settings {
struct NativeSettingsLocation {
    static int resolve(std::wstring& folder,DWORD& failure){
        PWSTR base=nullptr;HRESULT hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&base);
        if(FAILED(hr)||!base){if(base)CoTaskMemFree(base);failure=ERROR_PATH_NOT_FOUND;return K033_IO_ERROR;}
        try{folder=std::wstring(base)+L"\\033YanYunRuntime";}catch(...){CoTaskMemFree(base);throw;}
        CoTaskMemFree(base);return K033_OK;
    }
};
// One kernel file-sharing admission gate for all production settings users.
// Join BEFORE reading settings.ini or obtaining its existing .033lock writer
// lock. Retain until all settings/GPU consumers are drained; current native
// preferences retain it for their entire process lifetime. Never delete it.
// Installer order: exclusive session handle -> settings.ini.033lock -> write
// /verify/receipt -> release writer lock -> release session handle.
template<class Location>class SettingsSessionGate {
    struct File {
        HANDLE value=INVALID_HANDLE_VALUE;
        ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
        File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
        HANDLE take()noexcept{HANDLE h=value;value=INVALID_HANDLE_VALUE;return h;}
    } directory,gate;
    std::wstring settings_folder;
    DWORD failure=ERROR_SUCCESS;
    static bool final_path(HANDLE handle,std::wstring& value){
        wchar_t path[32768]{};DWORD n=GetFinalPathNameByHandleW(handle,path,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!n||n>=32768)return false;value.assign(path,n);return true;
    }
public:
    static constexpr const wchar_t* Filename=L"settings.sessions.v1.lock";
    SettingsSessionGate()=default;SettingsSessionGate(const SettingsSessionGate&)=delete;SettingsSessionGate& operator=(const SettingsSessionGate&)=delete;
    int join(){
        if(gate.value!=INVALID_HANDLE_VALUE)return K033_OK;
        std::wstring folder;
        int resolved=Location::resolve(folder,failure);if(resolved!=K033_OK)return resolved;
        if(!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){failure=GetLastError();return K033_IO_ERROR;}
        File pinned,joined;BY_HANDLE_FILE_INFORMATION info{};
        pinned.value=CreateFileW(folder.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(pinned.value==INVALID_HANDLE_VALUE){failure=GetLastError();return K033_IO_ERROR;}
        if(!GetFileInformationByHandle(pinned.value,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
           (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){failure=ERROR_INVALID_DATA;return K033_IO_ERROR;}
        std::wstring stable;if(!final_path(pinned.value,stable)){failure=GetLastError();return K033_IO_ERROR;}
        const auto lock_path=stable+L"\\"+Filename;
        // A nonblocking open fails while an installer owns share=0. No read,
        // write, stale-default publication or background thread starts then.
        joined.value=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(joined.value==INVALID_HANDLE_VALUE){failure=GetLastError();return failure==ERROR_SHARING_VIOLATION?K033_BUSY:K033_IO_ERROR;}
        if(!GetFileInformationByHandle(joined.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){failure=ERROR_INVALID_DATA;return K033_IO_ERROR;}
        std::wstring actual;if(!final_path(joined.value,actual)||CompareStringOrdinal(actual.c_str(),-1,lock_path.c_str(),-1,TRUE)!=CSTR_EQUAL){failure=ERROR_INVALID_DATA;return K033_IO_ERROR;}
        settings_folder=std::move(stable);directory.value=pinned.take();gate.value=joined.take();failure=ERROR_SUCCESS;return K033_OK;
    }
    bool held()const noexcept{return gate.value!=INVALID_HANDLE_VALUE;}
    DWORD error()const noexcept{return failure;}
    const std::wstring& folder()const noexcept{return settings_folder;}
};
// Production cannot accept a caller-selected root. Offline file tests replace
// only this compile-time location provider, retaining the actual kernel IO.
using NativeSessionGate=SettingsSessionGate<NativeSettingsLocation>;
}
