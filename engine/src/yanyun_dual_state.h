#pragma once
#include "yanyun_recipe.h"
// Accessed only under the existing NR writer. Background recognition receives
// immutable copies; it never reads or temporarily replaces carrier::cfg.
namespace yanyundual {
inline bool enabled=false;
inline std::atomic<bool> previewMask{false};
inline yanyunrecipe::Recipe appliedRecipe{};
inline bool haveAppliedRecipe=false;
inline carrier::Cfg person;
inline uint64_t revision=0;
inline float fidelity=.85f,sceneStrength=1.f,feather=2.f;
inline std::atomic<const char*> note{"人物分区未开启"};
inline std::atomic<uint64_t> recorded{0},recognitions{0};
inline void Apply(const yanyunrecipe::Recipe& requested,bool regional){
 using namespace yanyunrecipe;
 auto r=requested;NeutralSceneSkin(r); // scene layers carry no skin-specific settings (S18)
 auto fill=[&](carrier::Cfg& cfg,Group group){
  float values[nrcontrolsabi::Count]{};
  for(unsigned i=0;i<FieldCount;++i)values[Fields[i]]=r.values[group][i];
#define X(id,field,kind,lo,hi,section,label) if(Index(nrcontrolsabi::id)>=0)cfg.field=static_cast<decltype(cfg.field)>(values[nrcontrolsabi::id]);
  K033_NR_CONTROLS(X)
#undef X
 };
 person=carrier::cfg;fill(person,Character);fill(carrier::cfg,regional?Scene:Whole);
 appliedRecipe=r;haveAppliedRecipe=true;
 enabled=regional;fidelity=r.fidelity;sceneStrength=r.sceneStrength;feather=r.feather;
 ++revision;note=regional?"正在准备人物分区":"全画面方案";
}
inline bool SyncScene(){
 if(!haveAppliedRecipe)return false;
 if(enabled)yanyunrecipe::NeutralSceneSkin(carrier::cfg); // a legacy single edit cannot re-enable scene skin settings
 const auto group=enabled?yanyunrecipe::Scene:yanyunrecipe::Whole;bool changed=false;
#define X(id,field,kind,lo,hi,section,label) {const int i=yanyunrecipe::Index(nrcontrolsabi::id);if(i>=0&&appliedRecipe.values[group][i]!=float(carrier::cfg.field)){appliedRecipe.values[group][i]=float(carrier::cfg.field);changed=true;}}
 K033_NR_CONTROLS(X)
#undef X
 if(changed){appliedRecipe.kind=yanyunrecipe::Custom;++revision;}return changed;
}
}
