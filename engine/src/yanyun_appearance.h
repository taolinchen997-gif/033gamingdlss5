#pragma once
#include <cstring>
// Presentation preferences are independent from render controls and recipes.
namespace yyappearance {
inline bool light=false,english=false;
inline void(*changed)()=nullptr;
inline void SetLight(bool value){if(light!=value){light=value;if(changed)changed();}}
inline void SetEnglish(bool value){if(english!=value){english=value;if(changed)changed();}}
inline const char* Text(const char* zh,const char* en){return english?en:zh;}
}
