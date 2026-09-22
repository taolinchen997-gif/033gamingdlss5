#include "pch.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <nvapi.h>
#include "../../../../src/monitor_abi.h"
#include <mutex>
namespace monitor033 {
extern "C" GpuSample K033_MonitorGpu(uint64_t adapterLuid) noexcept {
 GpuSample result{};
 try {
  LUID luid{};static_assert(sizeof(luid)==sizeof(adapterLuid));std::memcpy(&luid,&adapterLuid,sizeof(luid));
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
  if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter))))return result;
  DXGI_ADAPTER_DESC1 desc{};if(SUCCEEDED(adapter->GetDesc1(&desc)))
   WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,result.name,sizeof(result.name),nullptr,nullptr);
  DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))){result.memoryValid=true;result.used=memory.CurrentUsage;result.budget=memory.Budget;}
  // Match the render adapter by its OS LUID, never take the first GPU on a
  // multi-adapter machine. Missing NVAPI/unsupported counters stay unavailable.
  struct Api {
   HMODULE module=nullptr;bool ready=false;
   decltype(&NvAPI_EnumPhysicalGPUs) enumerate=nullptr;
   decltype(&NvAPI_GetLogicalGPUFromPhysicalGPU) logical=nullptr;
   decltype(&NvAPI_GPU_GetLogicalGpuInfo) info=nullptr;
   decltype(&NvAPI_GPU_GetDynamicPstatesInfoEx) load=nullptr;
   decltype(&NvAPI_GPU_GetThermalSettings) thermal=nullptr;
   decltype(&NvAPI_GPU_GetMemoryInfoEx) memoryEx=nullptr;
   Api(){
    module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return;
    auto query=reinterpret_cast<void*(__cdecl*)(unsigned)>(GetProcAddress(module,"nvapi_QueryInterface"));if(!query)return;
    auto init=reinterpret_cast<decltype(&NvAPI_Initialize)>(query(0x0150e828));
    enumerate=reinterpret_cast<decltype(enumerate)>(query(0xe5ac921f));logical=reinterpret_cast<decltype(logical)>(query(0xadd604d1));
    info=reinterpret_cast<decltype(info)>(query(0x842b066e));load=reinterpret_cast<decltype(load)>(query(0x60ded2ed));
    thermal=reinterpret_cast<decltype(thermal)>(query(0xe3640a56));memoryEx=reinterpret_cast<decltype(memoryEx)>(query(0xc0599498));
    ready=init&&enumerate&&logical&&info&&load&&init()==NVAPI_OK;
   }
  };
  static Api api;if(!api.ready)return result;
  NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS]{};NvU32 count=0;
  if(api.enumerate(handles,&count)!=NVAPI_OK||count>NVAPI_MAX_PHYSICAL_GPUS)return result;
  for(unsigned i=0;i<count;++i){
   NvLogicalGpuHandle logical{};if(api.logical(handles[i],&logical)!=NVAPI_OK)continue;
   LUID actual{};NV_LOGICAL_GPU_DATA data{};data.version=NV_LOGICAL_GPU_DATA_VER;data.pOSAdapterId=&actual;
   if(api.info(logical,&data)!=NVAPI_OK||std::memcmp(&actual,&luid,sizeof(luid))||data.physicalGpuCount!=1)continue;
   NV_GPU_DYNAMIC_PSTATES_INFO_EX state{};state.version=NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
   if(api.load(handles[i],&state)==NVAPI_OK&&state.utilization[0].bIsPresent&&state.utilization[0].percentage<=100){result.loadValid=true;result.load=state.utilization[0].percentage;}
   if(api.thermal){NV_GPU_THERMAL_SETTINGS thermal{};thermal.version=NV_GPU_THERMAL_SETTINGS_VER;
    if(api.thermal(handles[i],NVAPI_THERMAL_TARGET_ALL,&thermal)==NVAPI_OK&&thermal.count<=NVAPI_MAX_THERMAL_SENSORS_PER_GPU)
     for(unsigned n=0;n<thermal.count;++n)if(thermal.sensor[n].target==NVAPI_THERMAL_TARGET_GPU&&thermal.sensor[n].currentTemp>=-30&&thermal.sensor[n].currentTemp<=150){result.temperature=thermal.sensor[n].currentTemp;result.temperatureValid=true;break;}}
   if(api.memoryEx){NV_GPU_MEMORY_INFO_EX memory{};memory.version=NV_GPU_MEMORY_INFO_EX_VER;
    if(api.memoryEx(handles[i],&memory)==NVAPI_OK&&memory.dedicatedVideoMemory&&memory.curAvailableDedicatedVideoMemory<=memory.dedicatedVideoMemory){result.boardMemoryValid=true;result.boardTotal=memory.dedicatedVideoMemory;result.boardUsed=memory.dedicatedVideoMemory-memory.curAvailableDedicatedVideoMemory;}}
   break;
  }
 }catch(...){result.loadValid=false;}
 return result;
}
}
