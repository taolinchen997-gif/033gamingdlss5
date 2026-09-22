#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <fstream>
#include "../third_party/OptiScaler033/OptiScaler/dlssnr/DlssNr_Capture.h"
// The upstream raw before/after writer is reused with an observed submission
// ticket. No "wait N frames" assumption, and no file I/O on the render thread.
namespace matchedcapture {
enum Status {Idle=0,Requested=1,Recording=2,WaitingGpu=3,Writing=4,Complete=5,Failed=6};
inline std::atomic<int> status{Idle};
inline std::mutex mutex;
inline std::unique_ptr<capture::FrameCapture> pending;
inline resolveleases::Ticket ticket;
inline std::string metadata,lastPath;
inline UINT64 expectedBefore=0,expectedAfter=0;
inline void Request(){int before=status.load();if(before==Idle||before==Complete||before==Failed)status.compare_exchange_strong(before,Requested);}
inline const char* Note(){switch(status.load()){
case Requested:return "等待有效 NR 画面";case Recording:return "正在记录同帧画面对照";case WaitingGpu:return "等待这组 GPU 指令完成";
case Writing:return "后台写入无损对照";case Complete:return "同帧原画和结果已保存";case Failed:return "抓帧未完成，请看日志";default:return "尚未抓帧";}}
inline void Record(ID3D12Device* dev,ID3D12GraphicsCommandList* cmd,ID3D12Resource* before,ID3D12Resource* after,
                   resolveleases::Slot* lease,const std::string& info){
    if(status.load()!=Requested)return;
    std::lock_guard<std::mutex> lock(mutex);
    if(status.load()!=Requested)return;
    status=Recording;pending=std::make_unique<capture::FrameCapture>();pending->request(1);
    pending->record(cmd,dev,before,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,after,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(!pending->readyToWrite()){pending->release();pending.reset();status=Failed;return;}
    ticket=resolveleases::GetTicket(lease);metadata=info;status=WaitingGpu;
    const auto beforeDesc=before->GetDesc(),afterDesc=after->GetDesc();
    dev->GetCopyableFootprints(&beforeDesc,0,1,0,nullptr,nullptr,nullptr,&expectedBefore);
    dev->GetCopyableFootprints(&afterDesc,0,1,0,nullptr,nullptr,nullptr,&expectedAfter);
}
inline void Pump(){
    if(status.load()!=WaitingGpu)return;
    std::lock_guard<std::mutex> lock(mutex);
    if(status.load()!=WaitingGpu || !resolveleases::Completed(ticket))return;
    // The core pins the add-on. Also pin in standalone native/Feeder use while
    // the writer holds its code; never let overlay unloading unmap this thread.
    HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Pump),&pinned);
    auto data=std::move(pending);const auto info=metadata;
    const auto beforeBytes=expectedBefore,afterBytes=expectedAfter;
    wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);
    const auto root=std::filesystem::path(exe).parent_path()/"033-captures"/std::to_string(GetTickCount64());
    status=Writing;
    std::thread([data=std::move(data),info,root,beforeBytes,afterBytes]()mutable{
        try{
            const auto path=data->write(root);
            if(path.empty()){status=Failed;Log("[033 capture] empty or dark capture rejected");return;}
            if(!beforeBytes || !afterBytes || std::filesystem::file_size(root/"before_00.raw")!=beforeBytes
                || std::filesystem::file_size(root/"after_00.raw")!=afterBytes || !std::filesystem::file_size(root/"manifest.txt")){
                status=Failed;Log("[033 capture] raw pair incomplete; write not accepted");return;
            }
            std::ofstream manifest(root/"033-frame-contract.json",std::ios::binary);manifest<<info;manifest.close();
            if(!manifest){status=Failed;Log("[033 capture] metadata write failed");return;}
            {std::lock_guard<std::mutex> lock(mutex);lastPath=path;}
            Log("[033 capture] GPU-completed raw before/after pair: %s",path.c_str());status=Complete;
        }catch(...){status=Failed;Log("[033 capture] write failed");}
    }).detach();
}
}
