// Per-application DRS helper. Never writes the global/base profile.
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include "../third_party/OptiScaler033/external/nvapi/nvapi.h"
using Query=void*(__cdecl*)(unsigned);
template<class T> T Fn(Query q,unsigned id){return reinterpret_cast<T>(q(id));}
int wmain(int argc,wchar_t** argv){
    if(argc<3)return 2;auto m=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!m)return 2;
    auto q=reinterpret_cast<Query>(GetProcAddress(m,"nvapi_QueryInterface"));if(!q)return 2;
    auto init=Fn<decltype(&NvAPI_Initialize)>(q,0x0150E828);auto create=Fn<decltype(&NvAPI_DRS_CreateSession)>(q,0x0694D52E);
    auto load=Fn<decltype(&NvAPI_DRS_LoadSettings)>(q,0x375DBD6B);auto destroy=Fn<decltype(&NvAPI_DRS_DestroySession)>(q,0xDAD9CFF8);
    auto find=Fn<decltype(&NvAPI_DRS_FindApplicationByName)>(q,0xEEE566B2);auto base=Fn<decltype(&NvAPI_DRS_GetBaseProfile)>(q,0xDA8466A0);
    auto get=Fn<decltype(&NvAPI_DRS_GetSetting)>(q,0x73BF8338);auto set=Fn<decltype(&NvAPI_DRS_SetSetting)>(q,0x577DD202);
    auto erase=Fn<decltype(&NvAPI_DRS_DeleteProfileSetting)>(q,0xE4A26362);auto save=Fn<decltype(&NvAPI_DRS_SaveSettings)>(q,0xFCBC7E14);
    if(!init||!create||!load||!destroy||!find||!base||!get||!set||!erase||!save||init()!=NVAPI_OK)return 2;
    NvDRSSessionHandle session=nullptr;NvDRSProfileHandle profile=nullptr;auto result=create(&session);if(result!=NVAPI_OK)return 2;
    struct Guard{NvDRSSessionHandle s;decltype(destroy) fn;~Guard(){fn(s);}} guard{session,destroy};if(load(session)!=NVAPI_OK)return 2;
    NvAPI_UnicodeString name{};if(wcslen(argv[2])>=NVAPI_UNICODE_STRING_MAX)return 2;wcscpy_s(reinterpret_cast<wchar_t*>(name),NVAPI_UNICODE_STRING_MAX,argv[2]);
    NVDRS_APPLICATION app{};app.version=NVDRS_APPLICATION_VER;result=find(session,name,&profile,&app);bool found=result==NVAPI_OK;
    if(!found){if(result!=NVAPI_EXECUTABLE_NOT_FOUND){fprintf(stderr,"find=%d\n",result);return 2;}if(base(session,&profile)!=NVAPI_OK)return 2;}
    constexpr NvU32 id=0xB0CC0875;
    const bool read=wcscmp(argv[1],L"read")==0,remove=wcscmp(argv[1],L"remove")==0,setValue=wcscmp(argv[1],L"set")==0;
    if(!read){if(!found){fprintf(stderr,"No application profile; refusing a global write.\n");return 2;}
        if(remove)result=erase(session,profile,id);
        else if(setValue&&argc==4){NVDRS_SETTING setting{};setting.version=NVDRS_SETTING_VER;setting.settingId=id;setting.settingType=NVDRS_DWORD_TYPE;setting.u32CurrentValue=wcstoul(argv[3],nullptr,10);result=set(session,profile,&setting);}
        else return 2;if(result!=NVAPI_OK||save(session)!=NVAPI_OK){fprintf(stderr,"write=%d\n",result);return 2;}
    }
    NVDRS_SETTING setting{};setting.version=NVDRS_SETTING_VER;result=get(session,profile,id,&setting);
    printf("{\"foundApplication\":%s,\"status\":%d,\"value\":%u,\"location\":%u,\"predefined\":%u}\n",found?"true":"false",result,setting.u32CurrentValue,unsigned(setting.settingLocation),setting.isCurrentPredefined);
    return result==NVAPI_OK?0:1;
}
