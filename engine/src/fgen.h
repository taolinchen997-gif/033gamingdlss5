// Retired legacy extra-Present prototype. Keep the two existing call sites and
// recognize old configuration, but never submit an extra Present or busy-wait.
// Image interpolation now lives in framegen_flow_*.h. A presentation owner must
// explicitly supply its queue, resource lifetime and scheduling contract.
#pragma once
namespace fgen {
static void load_cfg() {
    FILE* file=nullptr;const auto path=game_dir()+"\\dlss5-033.cfg";
    if(fopen_s(&file,path.c_str(),"rb")!=0||!file)return;
    char line[256];bool requested=false;
    while(std::fgets(line,sizeof(line),file)){
        char* eq=std::strchr(line,'=');if(!eq)continue;*eq=0;
        if(!std::strcmp(line,"fg"))requested=std::atoi(eq+1)!=0;
    }
    std::fclose(file);
    if(requested)Log("[fg] 旧版 fg=1 额外 Present 路径已停用；未据此开启其他插帧后端");
}
static void OnPresent(reshade::api::command_queue*,reshade::api::swapchain*) {}
}
