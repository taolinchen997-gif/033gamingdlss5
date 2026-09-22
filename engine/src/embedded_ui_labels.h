#pragma once
#include <cstring>
#include <string>
namespace embeddedui {
// Use stable original-name suffixes, independent of the displayed translation.
inline const char* Label(const char* text,bool keepId=true){
 if(!text)return "";
 struct Pair{const char* source;const char* translated;};
 static const Pair labels[]={
 {"Framerate","响应与限帧"},{"Apply Limit","应用上限"},
 {"Current method: Fallback","当前方式：常规限帧"},{"Current method: Reflex","当前方式：Reflex"},
 {"fakenvapi","低延迟接口"},{"Force XeLL","使用 XeLL"},
 {"Force Anisotropic Filtering","过滤倍率"},{"Modify Compare","应用于比较采样"},
 {"Modify Min/Max","应用于最小／最大采样"},{"Skip Point Filters","跳过点采样"},
 {"Set","应用"},{"Calculate Mipmap Bias","计算纹理偏移"},
 {"Extra-pass resolution (%)","后续层精度 (%)"},
 {"Upscalers","超分辨率"},{"Change Upscaler","应用超分算法"},
 {"Advanced DLSS Settings","DLSS 高级设置"},{"Advanced Init Flags","初始化高级设置"},
 {"Advanced Settings","高级设置"},{"Anisotropic Filtering","各向异性过滤"},
 {"FPS Overlay","帧率与性能叠加"},{"Keybinds","快捷键"},{"Logging","日志"},
 {"Magnifier","局部放大镜"},{"Menu Theme and Color","叠加层主题与颜色"},
 {"Mipmap Bias","纹理细节偏移"},{"Motion Adaptive Sharpness","运动自适应锐化"},
 {"Upscaler Inputs","超分输入接口"},{"V-Sync Settings","垂直同步"},
 {"VRR Frame Cap Calculator","可变刷新率帧率上限计算"},
 {"Resource Barriers","资源状态转换"},{"Root Signatures","计算与图形状态恢复"},
 {"XeSS Settings","XeSS 设置"},{"FSR Common Settings","FSR 通用设置"},
 {"Frame Generation","帧生成"},{"Frame Generation Settings","帧生成设置"},
 {"Sharpness","锐化"},{"Override","覆盖"},{"Apply Changes","应用修改"},
 {"Enable RCAS/DA","启用 RCAS／深度锐化"},{"Enable Motion Adaptive Sharpness","启用运动自适应锐化"},
 {"Contrast Enabled","启用对比度"},{"Upscale Ratio Override","覆盖超分比例"},
 {"Override all","统一覆盖"},{"Override per quality preset","按画质档位覆盖"},
 {"Output Scaling","输出缩放"},{"Enable","启用"},{"Downscaler","缩小算法"},
 {"Apply Change","应用修改"},{"Ratio","比例"},{"Init Flags","初始化参数"},
 {"Auto Exposure","自动曝光"},{"Disable Reactive Mask","禁用反应遮罩"},
 {"Enable Extended Limits","启用扩展范围"},{"Use Precompiled Shaders","使用预编译着色器"},
 {"DRS (Dynamic Resolution Scaling)","动态分辨率"},{"Override Minimum","覆盖最小值"},
 {"Override Maximum","覆盖最大值"},{"FPS Limit","帧率上限"},{"Reset Limit","重置上限"},
 {"Reset Target","重置目标"},{"FG Input","帧生成输入"},{"FG Output","帧生成输出"},
 {"Override DLSSG Ratio","覆盖 DLSS 帧生成倍率"},{"Force Dynamic MFG","强制动态多帧生成"},
 {"DMFG FPS Target","动态多帧目标帧率"},{"FrameTime","帧耗时"},{"Upscaler","超分处理耗时"},
 {"FG Rectangle Settings","帧生成画面区域"},{"Frame Pacing Tuning","帧间隔调节"},
 {"Tracking Settings","资源跟踪设置"},{"Resource Settings","资源设置"},
 {"Syncing Settings","同步设置"},{"Save Settings","保存设置"},{"Close","关闭"},
 {"Custom Accent Color","自定义强调色"},{"Custom BG Colour","自定义背景色"},
 {"Neural Rendering","神经渲染"},{"DLSS Neural Rendering","神经渲染完整设置"},
 {"Disable HUD Fix","关闭界面保护"},{"Enable HUD Fix","启用界面保护"},
 {"Refresh Rate","刷新率"},{"Reset","重置"},{"Performance Overlay","性能叠加"}
 };
 const auto marker=std::strstr(text,"##");const size_t length=marker?size_t(marker-text):std::strlen(text);
 for(const auto& p:labels)if(std::strlen(p.source)==length&&!std::strncmp(text,p.source,length)){
  static thread_local std::string ring[16];static thread_local unsigned slot=0;
  auto& result=ring[slot++%16];result=p.translated;
  if(keepId){result+="###";result+=text;}return result.c_str();
 }
 return text;
}
}
