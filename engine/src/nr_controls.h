#pragma once
#include "nr_controls_abi.h"
#include "yanyun_recipe.h"
#include "yanyun_recipe_store.h"
namespace nrcontrols {
static std::mutex mutex;
static float pending[nrcontrolsabi::Count]={};
static bool dirty[nrcontrolsabi::Count]={};
static uint32_t actions=0,pendingMfg=UINT32_MAX;
static yanyunrecipe::RequestQueue recipeQueue;
static uint64_t RecipeRevision(){std::lock_guard<std::mutex> lock(mutex);return recipeQueue.revision;}
static yanyunrecipe::SubmitResult SetRecipe(const yanyunrecipe::Recipe& recipe,bool regional,uint64_t expected){
 std::lock_guard<std::mutex> lock(mutex);
 const auto result=recipeQueue.Submit(recipe,regional,expected,true);
 if(result==yanyunrecipe::SubmitResult::Accepted){
  for(auto id:yanyunrecipe::Fields)dirty[id]=false; // this newer complete request supersedes older individual edits
 }
 return result;
}
static int __cdecl Set(uint32_t id,float value){
 if(!nrcontrolsabi::Valid(id,value))return 0;
 std::lock_guard<std::mutex> lock(mutex);
 // A newer individual edit overlays an already queued whole-frame recipe.
 pending[id]=value;dirty[id]=true;return 1;
}
static int __cdecl Action(uint32_t action){
 if(action<nrcontrolsabi::Save || action>nrcontrolsabi::NeutralGrade)return 0;
 std::lock_guard<std::mutex> lock(mutex);actions|=1u<<action;return 1;
}
static int __cdecl SetMfg(uint32_t count){
 if(count!=0 && (count<2 || count>6))return 0;
 std::lock_guard<std::mutex> lock(mutex);pendingMfg=count;return 1;
}
static int __cdecl Read(nrcontrolsabi::Snapshot* out){
 if(!out||!nrcontrolsabi::ReplyHeader(out->size,out->version,out->count))return 0;
 nrdispatch::WriterAccess writer;if(!writer.entered)return 0;
 nrcontrolsabi::Snapshot result;
#define X(id,field,kind,lo,hi,group,label) result.values[nrcontrolsabi::id]=float(carrier::cfg.field);
 K033_NR_CONTROLS(X)
#undef X
 result.modelW=hostnr::model_w();result.modelH=hostnr::model_h();
 if(hostnr::s_feat){result.layerModelW[0]=result.modelW;result.layerModelH[0]=result.modelH;}
 if(hostnr::s_extra_feat[0]){result.layerModelW[1]=hostnr::s_ew;result.layerModelH[1]=hostnr::s_eh;}
 if(hostnr::s_extra_feat[1]){result.layerModelW[2]=hostnr::s_tw;result.layerModelH[2]=hostnr::s_th;}
 result.presetBuilt=hostnr::s_feat?uint32_t(hostnr::s_model_cfg.preset):UINT32_MAX;result.presetReadback=nrfwd::preset_back();
 result.tuningPending=carrier::TuneSig()!=hostnr::s_built_tune || hostnr::s_building.load() || hostnr::s_want_build || carrier::cfg.work!=hostnr::s_built_work || carrier::cfg.modelfull!=hostnr::s_built_full;
 unsigned long long submitted=0,bypass=0;resolveleases::Counts(submitted,bypass);
 result.frames=submitted;result.leaseBypass=bypass;
 result.mfgRequested=mfgunlock::framecount::g_force_multiplier.load();
 result.mfgAccepted=mfgunlock::framecount::g_accepted_mode.load()==~0u?0:mfgunlock::framecount::g_accepted_generated.load()+1;
 result.mfgOff=mfgunlock::framecount::g_accepted_mode.load()==0;
 result.mfgPacingReady=g_flip_meter_patched.load();result.mfgBlocked=mfgunlock::framecount::g_declined_no_pacing.load();
 result.modelActive=!nrfault033::Blocked() && hostnr::s_feat!=nullptr;
 result.activeLayers=result.modelActive?uint32_t(hostnr::s_selected_passes):0;
 result.cachedLayers=result.modelActive?uint32_t(hostnr::s_built_passes):0;
 if(nrfault033::Blocked())std::snprintf(result.modelWait,sizeof(result.modelWait),"%s",nrfault033::Note());
 else if(result.tuningPending)std::snprintf(result.modelWait,sizeof(result.modelWait),"%s",hostnr::s_build_note.c_str());
 result.effectiveWhite=carrier::EffectiveWhite();
 result.hotkey=uint32_t(carrier::cfg.hotkey);
 result.whiteEncoding=result.modelActive?carrier::EncodeMode(hostnr::s_fmt,nrbackbuffer::Claimed()):-1;
 result.whiteStatus=carrier::cfg.enabled && result.modelActive && GetTickCount64()-hostnr::s_exposure.lastTick<500
    ?hostnr::s_exposure.status:exposurepolicy::Status::Waiting;
 result.exposureRecorded=hostnr::s_exposure.offered;result.exposureHeld=hostnr::s_exposure.missing;
 result.exposureBypass=hostnr::s_exposure.bypass;
 result.portraitStatus=nrcontrolsabi::PortraitStatus::Off;

#define X(id,field,kind,lo,hi,group,label) result.modelValues[nrcontrolsabi::id]=float(hostnr::s_model_cfg.field);
 K033_NR_CONTROLS(X)
#undef X
 {std::lock_guard<std::mutex> lock(mutex);for(bool item:dirty)if(item)++result.pending;
  if(actions)++result.pending;if(pendingMfg!=UINT32_MAX)++result.pending;if(recipeQueue.queued)++result.pending;}
 return nrcontrolsabi::Reply(out,out->size,out->version,out->count,result)?1:0;
}
static void Pump(){
 nrdispatch::WriterAccess writer;if(!writer.entered)return;
 std::lock_guard<std::mutex> lock(mutex);
 // Integrated rendering skips the legacy finish-effects carrier callback.
 // Poll its switch here on real frames, including while NR is disabled.
 if(rendercore::Integrated())carrier::HotkeyTick();
 static yanyunrecipe::Store<> activeStore(true);static bool restored=false;
 if(!restored){restored=true;yanyunrecipe::Recipe saved;
  if(activeStore.Load(saved)){yanyundual::Apply(saved,saved.regional!=0);Log("[033 YY recipe] restored complete applied recipe");}}
 recipeQueue.Consume([&](const yanyunrecipe::Recipe& recipe){
  yanyundual::Apply(recipe,recipe.regional!=0);
  Log("[033 YY recipe] applied revision=%llu regional=%u layers=%d/%d/%d stored_fidelity=%.3f stored_scene=%.3f (S28: not applied, person and scene at full effect); render acceptance pending",yanyundual::revision,unsigned(recipe.regional),int(yanyunrecipe::Get(recipe,yanyunrecipe::Whole,nrcontrolsabi::Passes)),int(yanyunrecipe::Get(recipe,yanyunrecipe::Character,nrcontrolsabi::Passes)),int(yanyunrecipe::Get(recipe,yanyunrecipe::Scene,nrcontrolsabi::Passes)),recipe.fidelity,recipe.sceneStrength);
  if(!activeStore.Save(recipe))Log("[033 YY recipe] active save failed; current session applied; previous disk state preserved");
 });
#define X(id,field,kind,lo,hi,group,label) if(dirty[nrcontrolsabi::id]){carrier::cfg.field=static_cast<decltype(carrier::cfg.field)>(pending[nrcontrolsabi::id]);dirty[nrcontrolsabi::id]=false;}
 K033_NR_CONTROLS(X)
#undef X
 nrfeatures::Restrict(carrier::cfg);
 carrier::cfg.retired_effect=0;
 if(actions&(1u<<nrcontrolsabi::PortraitNatural)) {
  // Conservative starting point, not an assertion of subjective beauty.
  // Model auto-mask provides the semantic skin control; the colour cue below
  // only limits INPUT grading and never crops/reprocesses a face.
  carrier::cfg.auto_mask=1;carrier::cfg.skin_structure=1.25f;
  carrier::cfg.faceboost=0.f;
 }
 if(actions&(1u<<nrcontrolsabi::NeutralGrade)){carrier::cfg.pre={};carrier::cfg.pre.enabled=1;}
 if(actions&(1u<<nrcontrolsabi::Capture))matchedcapture::Request();
 if(pendingMfg!=UINT32_MAX){
  mfg::set_multiplier(pendingMfg);pendingMfg=UINT32_MAX;
 }
 if(actions&(1u<<nrcontrolsabi::Save))carrier::PollConfig(true);
 static int previousEnabled=carrier::cfg.enabled;
 if(previousEnabled!=carrier::cfg.enabled){hostnr::invalidate_history();carrier::g.need_reset=true;previousEnabled=carrier::cfg.enabled;}
 static bool recipeDirty=false;static ULONGLONG recipeChanged=0;
 if(yanyundual::SyncScene()){recipeDirty=true;recipeChanged=GetTickCount64();}
 if(recipeDirty&&((actions&(1u<<nrcontrolsabi::Save))||GetTickCount64()-recipeChanged>=600)){
  if(activeStore.Save(yanyundual::appliedRecipe))recipeDirty=false;
  else recipeChanged=GetTickCount64();
 }
 actions=0;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl K033_SubmitYanYunRecipe(
 const yanyunrecipe::Recipe* recipe,uint32_t bytes,uint32_t regional,uint64_t expected){
 if(!recipe||bytes!=sizeof(yanyunrecipe::Recipe)||regional>1)return uint32_t(yanyunrecipe::SubmitResult::Invalid);
 return uint32_t(nrcontrols::SetRecipe(*recipe,regional!=0,expected));
}
extern "C" __declspec(dllexport) uint64_t __cdecl K033_YanYunRecipeRevision(){return nrcontrols::RecipeRevision();}
extern "C" __declspec(dllexport) const nrcontrolsabi::Api* __cdecl K033_GetNrControls(uint32_t version){
 static const nrcontrolsabi::Api api{sizeof(nrcontrolsabi::Api),nrcontrolsabi::Version,nrcontrols::Read,nrcontrols::Set,nrcontrols::Action,nrcontrols::SetMfg};
 return version==nrcontrolsabi::Version?&api:nullptr;
}

extern "C" __declspec(dllexport) int __cdecl K033_GetNrLifetime(nrlifetime::Snapshot* out){
 if(!out || out->size!=sizeof(*out) || out->version!=nrlifetime::Version)return 0;
 nrdispatch::WriterAccess writer;if(!writer.entered)return 0;
 nrlifetime::Snapshot result;
 result.created=nrfwd::s_created;result.released=nrfwd::s_released;result.releaseFailed=nrfwd::s_release_failed;
 result.fullBuilds=hostnr::s_full_builds;result.passChanges=hostnr::s_pass_builds;
 result.parkedObjects=uint32_t(hostnr::s_parked.size());
 for(const auto& item:hostnr::s_parked)if(item.feat)++result.parkedFeatures;
 result.activePasses=hostnr::s_feat?uint32_t(hostnr::s_selected_passes):0;
 result.cachedPasses=hostnr::s_feat?uint32_t(hostnr::s_built_passes):0;
 result.candidatePasses=hostnr::s_candidate_valid?uint32_t(hostnr::s_candidate.built_passes):0;
 result.extraW=hostnr::s_ew;result.extraH=hostnr::s_eh;
 hostnr::SampleMemory(hostnr::s_dev);result.usage=hostnr::s_memory_usage;result.budget=hostnr::s_memory_budget;
 result.lastBuildMs=hostnr::s_last_build_ms;
 const auto timing=gputime::snapshot();result.timingSamples=timing.samples;result.gpuMs=timing.fresh()?timing.total:-1;
 *out=result;return 1;
}
