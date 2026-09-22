#pragma once
#include <Windows.h>
#include <winver.h>
#include <stdexcept>
#pragma comment(lib,"version.lib")
namespace yanyunmask {
inline bool SupportedMsvcVersion(DWORD high,DWORD low) noexcept {
 return high>MAKELONG(44,14)||(high==MAKELONG(44,14)&&low>=MAKELONG(0,35211));
}
// The executable's already-loaded CRT wins over a DLL's private directory.
// Inspect its mapped version resource BEFORE loading ORT: DllMain can otherwise
// fault in the old _Mtx_lock, outside any C++ exception boundary.
inline bool LoadedMsvcSupported() noexcept {
 const auto module=GetModuleHandleW(L"msvcp140.dll");if(!module)return false;
 const auto resource=FindResourceW(module,MAKEINTRESOURCEW(1),MAKEINTRESOURCEW(16));if(!resource)return false;
 const auto loaded=LoadResource(module,resource);if(!loaded)return false;
 const auto data=LockResource(loaded);if(!data)return false;
 VS_FIXEDFILEINFO* info=nullptr;UINT bytes=0;
 if(!VerQueryValueW(data,L"\\",reinterpret_cast<void**>(&info),&bytes)||bytes<sizeof(*info)||info->dwSignature!=0xFEEF04BD)return false;
 return SupportedMsvcVersion(info->dwFileVersionMS,info->dwFileVersionLS);
}
inline void RequireSupportedMsvc(){
 if(!LoadedMsvcSupported())throw std::runtime_error("033_ORT_CRT_INCOMPATIBLE: loaded msvcp140 older than 14.44.35211; upgrade with the complete YanYun installer");
}
}
