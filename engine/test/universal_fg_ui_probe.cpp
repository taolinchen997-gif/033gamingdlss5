#include <Windows.h>
#include "../sdk/reshade-6.8.0/include/reshade.hpp"
#include "universal_fg_ui_probe.h"
static UniversalUiProbe state;
static void Init(reshade::api::effect_runtime*){++state.created;++state.live;}
static void Destroy(reshade::api::effect_runtime*){++state.destroyed;--state.live;}
static void Present(reshade::api::effect_runtime*){++state.presents;}
static void Overlay(reshade::api::effect_runtime*){++state.overlays;}
static bool Open(reshade::api::effect_runtime*,bool open,reshade::api::input_source source){
    if(open)++state.opened;else ++state.closed;
    if(source==reshade::api::input_source::keyboard){if(open)++state.keyboardOpened;else ++state.keyboardClosed;}
    return false;
}
extern "C" __declspec(dllexport) bool Read033UiProbe(UniversalUiProbe* out){if(!out)return false;*out=state;return true;}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){if(!reshade::register_addon(module))return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(Init);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(Destroy);
        reshade::register_event<reshade::addon_event::reshade_present>(Present);
        reshade::register_event<reshade::addon_event::reshade_overlay>(Overlay);
        reshade::register_event<reshade::addon_event::reshade_open_overlay>(Open);
    }else if(reason==DLL_PROCESS_DETACH)reshade::unregister_addon(module);return TRUE;
}
