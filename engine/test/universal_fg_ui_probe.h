#pragma once
#include <cstdint>
struct UniversalUiProbe {uint32_t created=0,destroyed=0,live=0,presents=0,overlays=0,opened=0,closed=0,keyboardOpened=0,keyboardClosed=0;};
using ReadUniversalUiProbe=bool(*)(UniversalUiProbe*);
