#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace exposurepolicy {
enum Source : uint32_t { Legacy, Fixed, Game };
enum class Status : uint32_t { Waiting, DisplayReferred, Fixed, GameRecorded, Held,
    Missing, UnknownState, Unsupported, Busy, Failed, Frozen };
constexpr uint64_t HoldMaxAgeMs=500;
inline float White(float value) { return std::isfinite(value)?std::clamp(value,0.01f,32.f):3.16f; }
inline float Trim(float value) { return std::isfinite(value)?std::clamp(value,0.25f,4.f):1.f; }
inline uint32_t CheckedSource(int source) { return source>=0&&source<=2?uint32_t(source):Legacy; }
inline bool UsesGame(uint32_t source,bool replica,int encoding) {
    return encoding==1 && (source==Game || (source==Legacy&&!replica));
}
inline float FixedWhite(uint32_t source,bool replica,float white,float trim) {
    if(source==Legacy)return replica?3.16f:White(white);
    return White(white)*(source==Game?Trim(trim):1.f);
}
inline float ExposureGain(uint32_t source,float white,float trim) {
    return source==Game?Trim(trim):White(white);
}
inline bool CanHold(uintptr_t oldStream,uintptr_t stream,uint64_t sampleTick,uint64_t now,bool completed) {
    return oldStream==stream && sampleTick && now>=sampleTick && now-sampleTick<=HoldMaxAgeMs && completed;
}
// Descriptors remain immutable until the observed command-list lease retires.
template<class Pool,class Complete>
int Reusable(const Pool& pool,int previous,Complete completed) {
    for(unsigned i=0;i<pool.size();++i)
        if(int(i)!=previous && (!pool[i].used || completed(pool[i].ticket)))return int(i);
    return -1;
}
inline const char* Note(Status state) {
    switch(state){
    case Status::DisplayReferred:return "显示参考输入：未使用游戏曝光";
    case Status::Fixed:return "固定白点";
    case Status::GameRecorded:return "已记录本帧游戏曝光读取，数值有效性由 GPU 检查";
    case Status::Held:return "曝光缺帧：暂用已完成的上一样本（最长 0.5 秒）";
    case Status::Missing:return "没有可用曝光样本，使用固定白点";
    case Status::UnknownState:return "曝光资源状态未知，使用固定白点";
    case Status::Unsupported:return "曝光纹理不兼容，使用固定白点";
    case Status::Busy:return "曝光资源仍在使用，使用固定白点";
    case Status::Failed:return "曝光准备失败，使用固定白点";
    case Status::Frozen:return "使用冻结画面对应的白点";
    default:return "等待神经渲染帧";
    }
}
}
