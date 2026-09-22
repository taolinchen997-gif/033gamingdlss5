#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

// Fuses the independent runtime's explicit provenance/no-placeholder rule
// into the existing hostnr executor. This is a recording receipt only; no GPU
// completion, presentation or visual acceptance is inferred from SDK success.
namespace nrbeta2 {
enum class Source : uint32_t { Unknown, NgxDeclared, SceneDeclared };
enum class State : uint32_t { Waiting, Disabled, MissingGuides, UnknownFlags,
    UnknownMotionScale, JitteredMotion, UnverifiedBridge, WaitingScene,
    WaitingModel, Recorded,
    // 2026-09-12 只增不插（数值不动，Snapshot.version 保持 1）：以前这两种情况都在 inject 里静默
    // return，面板打的是初始值「等待游戏原生输入」—— 跟"完全没挂上"同一句话，几万条报障糊在一起
    // 没法分流。现在各给一句，看一眼面板就知道断在哪。
    NoOutputTexture, NoRenderSize };
struct Snapshot {
    uint32_t size=sizeof(Snapshot),version=1;
    uint32_t source=0,state=uint32_t(State::Waiting),native_guides_recorded=0,same_evaluate=0;
    uint64_t records=0,source_serial=0;
};
inline bool NgxInput(bool depth,bool motion,bool flagsKnown,bool scaleKnown,
                     float sx,float sy,bool mvJittered,State& reason) {
    if(!depth||!motion){reason=State::MissingGuides;return false;}
    if(!flagsKnown){reason=State::UnknownFlags;return false;}
    if(!scaleKnown||!std::isfinite(sx)||!std::isfinite(sy)||sx==0.f||sy==0.f){reason=State::UnknownMotionScale;return false;}
    if(mvJittered){reason=State::JitteredMotion;return false;}
    reason=State::WaitingModel;return true;
}
inline std::atomic<uint32_t> source{0},state{uint32_t(State::Waiting)},guides{0},sameEval{0};
inline std::atomic<uint64_t> records{0},serial{0};
inline void Note(State value,Source origin=Source::Unknown) {
    source.store(uint32_t(origin));state.store(uint32_t(value));guides.store(0);sameEval.store(0);
}
inline void Recorded(Source origin,bool sameEvaluate,uint64_t sourceSerial=0) {
    source.store(uint32_t(origin));serial.store(sourceSerial);guides.store(1);
    sameEval.store(sameEvaluate?1:0);records.fetch_add(1);state.store(uint32_t(State::Recorded));
}
inline const char* Text(State value) {
    switch(value) {
    case State::Disabled:return "NR 已关闭";
    case State::MissingGuides:return "原生深度或运动未接入；NR 等待，不使用零占位";
    case State::UnknownFlags:return "原生输入创建语义尚未确认；NR 等待";
    case State::UnknownMotionScale:return "运动矢量尺度尚未确认；NR 等待";
    case State::JitteredMotion:return "运动含相机抖动，归一化尚未接通；NR 等待";
    case State::UnverifiedBridge:return "此桥接输入的原生来源尚未确认；NR 等待";
    case State::WaitingScene:return "等待有效场景输入；不使用零占位替代";
    case State::WaitingModel:return "原生输入已准入；正在准备模型或等待提交";
    case State::Recorded:return "已记录 NR 命令；实际显示与画质待手动验收";
    case State::NoOutputTexture:return "已接到游戏的 DLSS，但参数块里取不到输出纹理；NR 等待";
    case State::NoRenderSize:return "已接到游戏的 DLSS，但读不到渲染分辨率；NR 等待";
    default:return "等待游戏原生输入";
    }
}
inline int Read(Snapshot* out) {
    if(!out||out->size!=sizeof(*out)||out->version!=1)return 0;
    Snapshot result;result.source=source.load();result.state=state.load();
    result.native_guides_recorded=guides.load();result.same_evaluate=sameEval.load();
    result.records=records.load();result.source_serial=serial.load();*out=result;return 1;
}
}
