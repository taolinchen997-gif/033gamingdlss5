#pragma once
#include "../runtime/include/033_nr.h"
#include <cstdint>
namespace k033beta2 {
struct SettingsStatus {
    uint32_t size=sizeof(SettingsStatus),version=1;
    int32_t admission=K033_BUSY,load_result=K033_BYPASS,save_result=K033_BYPASS,nr_save_result=K033_BYPASS;
    uint32_t active=0,win32_error=0;
    uint64_t queued=0,saved=0,nr_queued=0,nr_saved=0;
    uint64_t fg_queued=0,fg_saved=0;int32_t fg_save_result=K033_BYPASS;
};
int Prepare() noexcept;
int Initial(K033_Settings&,K033_NrSettings&) noexcept;
int OfferGrade(const K033_Settings&) noexcept;
int OfferNr(const K033_NrSettings&) noexcept;
int ReceiveGrade(K033_Settings&) noexcept;
int ReceiveNr(K033_NrSettings&) noexcept;
int Inspect(SettingsStatus&) noexcept;
// These mappings preserve the S53 schema and only update the consumer mirror.
// No game-specific file, path or secondary persisted defaults live here.
template<class C>void ApplyGrade(C& c,const K033_Settings& s){
 c.pre.enabled=s.enabled;c.pre.style=s.style;c.pre.exposure=s.exposure;c.pre.contrast=s.contrast;
 c.pre.saturation=s.saturation;c.pre.warmth=s.warmth;c.pre.tint=s.tint;c.pre.highlights=s.highlights;c.pre.styleStrength=s.style_strength;c.pre.skinProtection=0;
}
template<class C>void ApplyNr(C& c,const K033_NrSettings& s){
 c.enabled=int(s.enabled);c.passes=int(s.layers); // S37: the NR key is the player's own (hotkey.cfg), not shared
 c.skin_structure=s.skin_structure[0];c.extra[0].skin=s.skin_structure[1];c.extra[1].skin=s.skin_structure[2];c.skin_lift=s.skin_lift;c.sharpen=s.final_clarity;c.natural_look=s.natural_look;
 if(s.sr_work[0])c.work=int(s.sr_work[0]);
 if(s.sr_work[1])c.passwork=int(s.sr_work[1]);
 // Legacy files have no third-layer request; preserve their shared extra scale.
 c.passwork3=s.sr_work[2]?int(s.sr_work[2]):c.passwork;
 for(unsigned i=0;i<3;++i){const auto& p=s.layer[i];
  if(i==0){c.style=int(p.style);c.preset=int(p.preset);c.auto_mask=int(p.auto_mask);c.ui_correct=int(p.ui_correction);
   c.intensity=p.intensity;c.local_structure=p.local_structure;c.local_tone=p.local_tone;c.global_tone=p.global_tone;}
  else {auto& m=c.extra[i-1];m.style=int(p.style);m.preset=int(p.preset);m.autoMask=int(p.auto_mask);m.uiCorrect=int(p.ui_correction);
   m.intensity=p.intensity;m.structure=p.local_structure;m.tone=p.local_tone;m.globalTone=p.global_tone;}
 }
}
template<class C>K033_Settings Grade(const C& c){return {sizeof(K033_Settings),1,c.pre.enabled,c.pre.style,c.pre.exposure,c.pre.contrast,c.pre.saturation,c.pre.warmth,c.pre.tint,c.pre.highlights,c.pre.styleStrength};}
template<class C>K033_NrSettings Nr(const C& c){K033_NrSettings s{};s.size=sizeof(s);s.version=6;s.enabled=uint32_t(c.enabled);s.layers=uint32_t(c.passes);s.appearance_epoch=1;
 s.skin_structure[0]=c.skin_structure;s.skin_structure[1]=c.extra[0].skin;s.skin_structure[2]=c.extra[1].skin;s.skin_lift=c.skin_lift;s.final_clarity=c.sharpen;s.natural_look=c.natural_look;
 s.sr_work[0]=uint32_t(c.work);s.sr_work[1]=uint32_t(c.passwork);s.sr_work[2]=uint32_t(c.passwork3);
 for(unsigned i=0;i<3;++i){auto& p=s.layer[i];
  if(i==0)p={uint32_t(c.style),uint32_t(c.preset),uint32_t(c.auto_mask),uint32_t(c.ui_correct),c.intensity,c.local_structure,c.local_tone,c.global_tone};
  else {const auto& m=c.extra[i-1];p={uint32_t(m.style),uint32_t(m.preset),uint32_t(m.autoMask),uint32_t(m.uiCorrect),m.intensity,m.structure,m.tone,m.globalTone};}}
 return s;
}
}
extern "C" __declspec(dllexport) int __cdecl K033_Beta2PrepareSettings();
