#include "EmbeddedUi033.h"
#pragma once
#include "Core033.h"
// This lives inside the same menu as every upstream SR/FG/latency/HUD/graphics
// control. The renderer remains in the 033 add-on; edits go to its real owner.
namespace Core033 {
inline void RenderControls(Config* config){
    const auto* api=Controls();
    if(!api){Ui033::TextWrapped("033 NR controls are waiting for the matching 033 add-on.");return;}
    static nrcontrolsabi::Snapshot state;
    nrcontrolsabi::Snapshot current;
    if(api->read(&current) && !Ui033::IsAnyItemActive() && !current.pending)state=current;
    Ui033::Text("NR %ux%u | submitted %llu | resource bypass %llu",state.modelW,state.modelH,
        (unsigned long long)state.frames,(unsigned long long)state.leaseBypass);
    Ui033::Text("Preset requested %.0f / built %u / last parameter-container echo %u%s",state.values[nrcontrolsabi::Preset],
        state.presetBuilt,state.presetReadback,state.tuningPending?" (applying)":"");
    Ui033::TextWrapped("Preset 1/2/3 and native global tone have no reliably verified visual effect in the fixed-fixture audit. Parameter echo is not model acceptance. Input style is a separate, measured before-NR shader.");
    Ui033::TextWrapped("033 grades the input before NR. Model skin structure and automatic mask are native NR settings.");
    if(Ui033::Button("Save all 033 settings")){config->SaveIni();api->action(nrcontrolsabi::Save);}
    Ui033::SameLine();if(Ui033::Button("Capture matched inputs/result"))api->action(nrcontrolsabi::Capture);
    const char* lastGroup=nullptr;
    for(uint32_t id=0;id<nrcontrolsabi::Count;++id){
        // The ReShade-owned page already renders these controls and status.
        if(EmbeddedUiOwned() && (id==nrcontrolsabi::White || id==nrcontrolsabi::WhiteSource || id==nrcontrolsabi::WhiteTrim))continue;
        // 033 artistic controls are before NR. Keep legacy configuration fields
        // readable for migration, but do not offer inactive output modifiers.
        if(id==nrcontrolsabi::Blend || id==nrcontrolsabi::Colour || id==nrcontrolsabi::Mas ||
           id==nrcontrolsabi::MasStill || id==nrcontrolsabi::MasMoving || id==nrcontrolsabi::MasThreshold ||
           id==nrcontrolsabi::Sharpen || id==nrcontrolsabi::Compose)continue;
        const auto& def=nrcontrolsabi::definitions[id];
        if(!lastGroup || strcmp(lastGroup,def.group)!=0){Ui033::SeparatorText(def.group);lastGroup=def.group;}
        Ui033::PushID(int(id));float& value=state.values[id];bool commit=false;
        const bool unavailable=((id==nrcontrolsabi::Exposure || id==nrcontrolsabi::Contrast || id==nrcontrolsabi::Saturation || id==nrcontrolsabi::Warmth || id==nrcontrolsabi::Tint || id==nrcontrolsabi::Highlights || id==nrcontrolsabi::PreStyle || id==nrcontrolsabi::PreStyleStrength) && state.values[nrcontrolsabi::Grade]==0) || (id==nrcontrolsabi::PreStyleStrength && state.values[nrcontrolsabi::PreStyle]==0) || (id==nrcontrolsabi::Curve && state.values[nrcontrolsabi::Replica]!=0) || (id==nrcontrolsabi::White && state.values[nrcontrolsabi::Replica]!=0 && state.values[nrcontrolsabi::WhiteSource]==0) || (id==nrcontrolsabi::WhiteTrim && state.values[nrcontrolsabi::WhiteSource]!=2);
        Ui033::BeginDisabled(unavailable);
        if(def.kind==nrcontrolsabi::Toggle){bool on=value!=0;if(Ui033::Checkbox(def.label,&on)){value=on?1.f:0.f;commit=true;}}
        else if(def.kind==nrcontrolsabi::Integer){int number=int(value);if(Ui033::SliderInt(def.label,&number,int(def.minimum),int(def.maximum)))value=float(number);commit=Ui033::IsItemDeactivatedAfterEdit();}
        else {Ui033::SliderFloat(def.label,&value,def.minimum,def.maximum,"%.2f");commit=Ui033::IsItemDeactivatedAfterEdit();}
        Ui033::EndDisabled();
        if(commit)api->set(id,value);
        Ui033::PopID();
    }
    if(Ui033::Button("Reset pre-grade to neutral"))api->action(nrcontrolsabi::NeutralGrade);
    Ui033::SeparatorText("033 native multi-frame generation");
    static const char* choices[]={"Game setting","2x","3x","4x","5x","6x"};
    int selected=state.mfgRequested>=2?int(state.mfgRequested)-1:0;
    if(Ui033::Combo("Native multiplier",&selected,choices,6))api->setMfg(selected?uint32_t(selected+1):0u);
    Ui033::Text("Runtime accepted multiplier: %u (requires a live DLSS-G response)",state.mfgAccepted);
    Ui033::TextWrapped("Alternative FG, HUD fix and latency providers are in the Frame Generation and Low Latency sections of this same console. Unsupported hardware remains unsupported.");
}
}
