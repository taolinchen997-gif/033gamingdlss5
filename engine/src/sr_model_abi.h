#pragma once
#include <cstdint>
#include <cstring>
// S32 (owner 2026-09-24): which model YanYun's own DLSS Super Resolution uses.
// NGX render presets applied to every quality mode: 0 keeps the game's own request,
// 11 = K, 13 = M, 12 = L (nvsdk_ngx_defs.h: J=10, K=11, L=12, M=13).
// Shared by the 033 panel and the bundled OptiScaler, which recreates the feature.
namespace srmodelabi {
inline constexpr uint32_t GameDefault=0,K=11,L=12,M=13;
inline bool Allowed(uint32_t preset){return preset==GameDefault||preset==K||preset==L||preset==M;}
// L and M are the second-generation transformer presets: DLSS 310.5 or newer.
inline bool SecondGeneration(uint32_t preset){return preset==L||preset==M;}
inline bool SupportsSecondGeneration(uint32_t major,uint32_t minor){return major>310||(major==310&&minor>=5);}
// S33 (owner 2026-09-24: 「设L，就自动将游戏设置成超级性能」, then after trying Ultra
// Performance 「不是那么低的档位，例如K,M不配性能档位，配质量。 L配平衡」): the model also
// sets the game's render size, whatever quality mode the game asks for: K and M = Quality
// (ratio 1.5, render 2/3), L = Balanced (ratio 1.7). Game default leaves it alone.
// S36 (owner: 「超分L 不要平衡，设置成质量」): L is Quality too, so every model renders at 2/3.
// Ratio = output / render (OptiScaler's own table); 0 means the game's own mode decides.
inline float LinkedRatio(uint32_t preset){return preset==K||preset==M||preset==L?1.5f:0.0f;}
// The game quality mode a forced ratio stands for (panel wording); 0 when not forced.
inline uint32_t LinkedMode(uint32_t forcedMilli){return forcedMilli==1500?1u:forcedMilli==1700?2u:0u;} // 1 Quality, 2 Balanced
inline constexpr uint32_t None=0xFFFFFFFFu;
// The per-user file: exactly "schema=1\npreset=<0|11|12|13>\n". Anything else keeps
// the game's own model; a damaged file never forces a model the player did not pick.
inline uint32_t ParseStored(const char* text){
    static const char head[]="schema=1\npreset=";
    if(!text||std::strncmp(text,head,sizeof(head)-1))return GameDefault;
    const char* p=text+sizeof(head)-1;uint32_t value=0;int digits=0;
    while(*p>='0'&&*p<='9'&&digits<3){value=value*10+uint32_t(*p-'0');++p;++digits;}
    if(!digits||p[0]!='\n'||p[1]!=0||!Allowed(value))return GameDefault;
    return value;
}
struct Status {
    uint32_t size=sizeof(Status),version=2;
    uint32_t requested=GameDefault; // last choice handed to the core
    uint32_t applied=None;          // preset written into the last DLSS SR creation (0 = the game's own); None = no creation yet
    uint32_t creations=0;           // DLSS SR creations seen in this process
    uint32_t pending=0;             // a recreation was requested and has not happened yet
    uint32_t major=0,minor=0,patch=0; // DLSS snippet version read after the last creation (0 = unknown)
    uint32_t external=0;            // the driver (NVIDIA App / Inspector) overrides presets for this game
    // S33: the game's render-size questions ("optimal settings") and our answers.
    uint32_t queries=0;             // how often the game asked in this process
    uint32_t outputW=0,outputH=0;   // the output size it asked for last
    uint32_t renderW=0,renderH=0;   // the render size we answered last
    uint32_t gameMode=0;            // the NGX quality mode the game asked for last
    uint32_t forcedMilli=0;         // forced output/render ratio x1000 in force now (0 = the game's own mode)
    uint32_t sizePending=0;         // a forced ratio changed and the game has not asked again since
};
static_assert(sizeof(Status)==72,"srmodelabi::Status wire layout");
}
