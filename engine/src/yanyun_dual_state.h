#pragma once
#include "yanyun_recipe.h"
// Accessed only under the existing NR writer. Background recognition receives
// immutable copies; it never reads or temporarily replaces carrier::cfg.
namespace yanyundual {
inline bool enabled=false;
// S32: 0 whole picture, 1 模式一, 2 模式二 (yanyunrecipe::Partition).
inline uint32_t partition=yanyunrecipe::WholePicture;
inline std::atomic<bool> previewMask{false};
inline yanyunrecipe::Recipe appliedRecipe{};
inline bool haveAppliedRecipe=false;
inline carrier::Cfg person;
inline uint64_t revision=0;
inline float fidelity=.85f,sceneStrength=1.f,feather=2.f;
inline std::atomic<const char*> note{"人物分区未开启"};
inline std::atomic<uint64_t> recorded{0},recognitions{0};
inline bool SharedFirstLayerMode(){return enabled&&partition==yanyunrecipe::SharedFirstLayer;}
inline void Apply(const yanyunrecipe::Recipe& requested,uint32_t regional){
 using namespace yanyunrecipe;
 if(regional>=PartitionCount)regional=WholePicture; // Valid()/Submit() already refuse it; never guess a partition
 auto r=requested;r.regional=regional;NeutralSceneSkin(r); // scene layers carry no skin-specific settings (S18)
 auto fill=[&](carrier::Cfg& cfg,auto groupOf){
  float values[nrcontrolsabi::Count]{};
  for(unsigned i=0;i<FieldCount;++i)values[Fields[i]]=r.values[groupOf(Fields[i])][i];
#define X(id,field,kind,lo,hi,section,label) if(Index(nrcontrolsabi::id)>=0)cfg.field=static_cast<decltype(cfg.field)>(values[nrcontrolsabi::id]);
  K033_NR_CONTROLS(X)
#undef X
 };
 // 模式二 (S32): the rendered chain takes its first layer and that layer's size from
 // the whole column and everything else from the scene column; the person look is
 // that chain's first layer, finished with the person column's composition only.
 person=carrier::cfg;fill(person,[](Id){return Character;});
 fill(carrier::cfg,[&](Id id){return ChainGroup(regional,id);});
 appliedRecipe=r;haveAppliedRecipe=true;
 enabled=regional!=WholePicture;partition=regional;fidelity=r.fidelity;sceneStrength=r.sceneStrength;feather=r.feather;
 ++revision;note=regional==SharedFirstLayer?"正在准备模式二（第 1 层人物和场景共用）":regional?"正在准备人物分区":"全画面方案";
}
inline bool SyncScene(){
 if(!haveAppliedRecipe)return false;
 // A legacy single edit cannot re-enable scene skin settings; in 模式二 the first
 // layer is the whole column's, whose skin options stay its own.
 if(enabled)yanyunrecipe::NeutralSceneSkin(carrier::cfg,partition==yanyunrecipe::SharedFirstLayer);
 const uint32_t mode=enabled?partition:yanyunrecipe::WholePicture;bool changed=false;
#define X(id,field,kind,lo,hi,section,label) {const int i=yanyunrecipe::Index(nrcontrolsabi::id);if(i>=0){const auto group=yanyunrecipe::ChainGroup(mode,nrcontrolsabi::id);if(appliedRecipe.values[group][i]!=float(carrier::cfg.field)){appliedRecipe.values[group][i]=float(carrier::cfg.field);changed=true;}}}
 K033_NR_CONTROLS(X)
#undef X
 if(changed){appliedRecipe.kind=yanyunrecipe::Custom;++revision;}return changed;
}
}
