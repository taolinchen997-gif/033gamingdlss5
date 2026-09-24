#pragma once
// S38 (owner 2026-09-24: 「30系反应是游戏里面帧生成选项都没有」): read-only checks of what YanYun's
// Streamline 2.11.1 demands before the game may show DLSS frame generation at all:
//  - Windows hardware-accelerated GPU scheduling (sl.interposer: "Feature '%s' requires GPU hardware
//    scheduling to be enabled in the OS"); the setting is HKLM ...\GraphicsDrivers HwSchMode (2 on, 1 off)
//    and takes effect after a restart;
//  - Windows 10 2004 or later (sl.dlss_g: "Win10 20H1 (version 2004) is required to use DLSS-G").
// On RTX 20/30 the third condition is the bundled bridge, which answers NGX's feature requirements;
// the panel already knows whether it is there. Nothing here writes anything.
#include <Windows.h>
#pragma comment(lib,"advapi32.lib")
namespace fgcheck033 {
enum class Hags:unsigned {Unknown,Off,On};
inline Hags Scheduling(){
 DWORD value=0,size=sizeof(value);
 if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",L"HwSchMode",RRF_RT_REG_DWORD,nullptr,&value,&size)!=ERROR_SUCCESS)
  return Hags::Unknown;
 return value==2?Hags::On:value==1?Hags::Off:Hags::Unknown;
}
inline constexpr unsigned MinimumBuild=19041; // Windows 10 2004 (20H1)
inline unsigned WindowsBuild(){
 using Get=LONG(WINAPI*)(OSVERSIONINFOW*);const auto ntdll=GetModuleHandleW(L"ntdll.dll");
 const auto get=ntdll?reinterpret_cast<Get>(GetProcAddress(ntdll,"RtlGetVersion")):nullptr;
 OSVERSIONINFOW info{};info.dwOSVersionInfoSize=sizeof(info);
 return get&&get(&info)==0&&info.dwMajorVersion>=10?unsigned(info.dwBuildNumber):0u;
}
}
