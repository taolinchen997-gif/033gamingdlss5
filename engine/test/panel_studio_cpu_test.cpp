// Production UI bindings and layout only. No renderer, HWND, device, DLL load,
// desktop capture or operating-system input; events go to a private ImGui IO.
#include "imgui.h"
#include "imgui_internal.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "../src/yanyun_art_atlas.h"
#include "../src/panel_studio.h"
#include "../src/panel_yanyun_recipe.h"
#include "../src/panel_yanyun_columns.h"
#include "../src/panel_yanyun_monitor.h"
#include "../src/monitor_abi.h"
#include "../src/beta2_shared_settings.h"
#include "../src/nr_layer_settings.h"
#define NOMINMAX
#include "../runtime/shared/settings_codec.h"
#include <map>
#include <string>
#include <cstdio>
#include <stdexcept>
static std::map<std::string,ImVec2> items;
static std::map<std::string,float> itemBottoms;
static unsigned checks=0;
static void Check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
#define CHECK(expression) Check(bool(expression),#expression)
#include "panel_cpu_raster.h"
#undef CHECK
static const char* rasterFile=nullptr;
static bool visualEvidenceMode=false;
static void Observe(const char* id,ImVec2 a,ImVec2 b){
 // ImGui first measures a new popup in a hidden frame before positioning it.
 // Those rectangles are never rendered or interactive; check visible frames.
 if(ImGui::GetCurrentWindow()->Hidden)return;
 if(a.x<0||b.x>ImGui::GetIO().DisplaySize.x+1)printf("BOUNDS %s %.1f..%.1f display %.1f\n",id,a.x,b.x,ImGui::GetIO().DisplaySize.x);
 Check(a.x>=0&&b.x<=ImGui::GetIO().DisplaySize.x+1,"control outside horizontal bounds");items[id]={(a.x+b.x)*.5f,(a.y+b.y)*.5f};itemBottoms[id]=b.y;}
static bool columnsMode=false;
// S23 fit cases: a movable, resizable window like the host's, sized only on first use.
static bool fitMode=false;static ImVec2 fitStart{};
static studio033::RecipeView yyView;static studio033::RecipeEdits yyEdits;
static studio033::Edits Frame(studio033::State& s,studio033::View& view){
 ImGui::NewFrame();items.clear();itemBottoms.clear();
 if(fitMode){ImGui::SetNextWindowPos({0,0},ImGuiCond_Once);ImGui::SetNextWindowSize(fitStart,ImGuiCond_Once);}
 else{ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);}
 studio033::Edits e;yyEdits={};ImGuiWindow* window=nullptr;{studio033::Theme theme;
 ImGui::Begin(fitMode?"033 fit":"033 CPU",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|(fitMode?0:ImGuiWindowFlags_NoResize));
 {studio033::BeginBody();if(visualEvidenceMode)ImGui::SetScrollY(0);if(view.page!=7)studio033::Header(s,view,e);
 if(columnsMode&&view.page==0)studio033::RecipePre(s,yyView,yyEdits);else if(columnsMode&&view.page==5)studio033::RecipeSr(s,yyView,yyEdits);else if(columnsMode&&view.page==1)studio033::RecipeColumns(s,yyView,yyEdits);else if(view.page==7){studio033::MonitorState m;m.gpu="NVIDIA GeForce RTX · CPU 示例数据";m.fpsValid=m.loadValid=m.memoryValid=m.nrValid=m.recognitionValid=true;m.fps=72;m.load=86;m.memoryGiB=9.2;m.budgetGiB=15.1;m.nrMs=6.2;m.recognitionMs=4.8;m.maskAge=22;m.regionalFrames=864;m.fg=6;m.status="示例：人物与场景处理中";studio033::MonitorBody(m);}else if(view.page==6)studio033::RecipePanel(s,yyView,yyEdits);else if(view.page==0)studio033::Picture(s,e);else if(view.page==1)studio033::Model(s,view,e);else if(view.page==2)studio033::FrameGeneration(s,e);else if(view.page==3)studio033::InputNormalization(s,e);else if(view.page==5)studio033::InternalSr(s,view,e);if(columnsMode)studio033::RecipeFooter(s,e);else studio033::Footer(s,e);studio033::EndBody();}
 window=ImGui::GetCurrentWindow();ImGui::End();}ImGui::Render();Check(ImGui::GetDrawData()!=nullptr,"UI draw lists available");
 if(rasterFile){panel_cpu_raster(rasterFile,window);rasterFile=nullptr;}return e;
}
static studio033::Edits Click(studio033::State& s,studio033::View& view,const char* id,float offsetX=0){
 for(int settle=0;settle<3;++settle){Frame(s,view);if(items.count(id))break;}
 if(!items.count(id))printf("MISSING %s page=%d\n",id,view.page);Check(items.count(id)>0,"control absent");auto pt=items.at(id);pt.x+=offsetX;auto& io=ImGui::GetIO();
 io.AddMousePosEvent(pt.x,pt.y);Frame(s,view);io.AddMouseButtonEvent(0,true);const auto pressed=Frame(s,view);
 io.AddMouseButtonEvent(0,false);auto released=Frame(s,view);released.changed|=pressed.changed;released.toggle|=pressed.toggle;return released;
}

static void DraftCases(){
 using namespace nrcontrolsabi;
 // All staged fields share one state machine, including count and SR precision.
 // Sized from the live Appearance list: V6.1 added Skin to it and a literal 28 overran the stack (0xC0000409).
 Id ids[3*(sizeof(studio033::Appearance)/sizeof(studio033::Appearance[0]))+4];unsigned count=0;
 for(unsigned layer=0;layer<3;++layer)for(auto first:studio033::Appearance)ids[count++]=LayerId(first,layer);
 for(auto id:{Passes,Work,PassWork,PassWork3})ids[count++]=id;
 for(unsigned i=0;i<count;++i){const auto id=ids[i];const float oldValue=id==Passes?1.f:(id==Work||id==PassWork||id==PassWork3?100.f:0.f);
  const float nextValue=id==Passes?2.f:(id==Work||id==PassWork||id==PassWork3?75.f:1.f);
  paneldraft::Fields<Count> fields;fields.Observe(id,oldValue,true,0);fields.Edit(id,nextValue);
  const auto firstRevision=fields.Revision(id);fields.Accepted(id,firstRevision,nextValue);
  // Returning to the old value is still an explicit new edit, even though it
  // numerically equals the last observed snapshot.
  fields.Edit(id,oldValue);fields.Observe(id,nextValue,true,0);
  Check(fields.Value(id)==oldValue&&fields.NeedsSubmit(id),"old ack overwrote newer edit back to old value");
  const auto secondRevision=fields.Revision(id);fields.Accepted(id,secondRevision,oldValue);
  fields.Observe(id,oldValue,false,0);Check(fields.InFlight(id),"reused snapshot falsely acknowledged a matching request");
  fields.Observe(id,oldValue,true,1);Check(fields.InFlight(id),"undrained queue falsely acknowledged matching request");
  fields.Observe(id,oldValue,true,0);Check(!fields.InFlight(id)&&!fields.NeedsSubmit(id),"fresh drained request acknowledgement failed");
  fields.Edit(id,nextValue);const auto thirdRevision=fields.Revision(id);fields.Failed(id,thirdRevision);
  fields.Observe(id,oldValue,true,0);Check(fields.Value(id)==nextValue&&fields.Rejected(id)&&fields.NeedsSubmit(id),"rejected request lost retry draft");
  fields.Accepted(id,thirdRevision,nextValue);fields.Edit(id,oldValue);fields.Discard(id);
  Check(fields.Value(id)==nextValue&&fields.InFlight(id)&&!fields.NeedsSubmit(id),"Discard rolled back an accepted in-flight request");
  fields.Observe(id,nextValue,true,0);fields.Observe(id,oldValue,true,0);
  Check(fields.Value(id)==oldValue&&!fields.NeedsSubmit(id),"clean field did not receive external requested update");
  fields.Edit(id,nextValue);fields.Accepted(id,fields.Revision(id),nextValue);fields.Observe(id,oldValue,true,0);
  Check(!fields.InFlight(id)&&fields.Value(id)==oldValue&&!fields.NeedsSubmit(id),"fresh drained superseding request left a permanent in-flight state");
  fields.Edit(id,nextValue);const auto staleRevision=fields.Revision(id);fields.Edit(id,oldValue);
  fields.Accepted(id,staleRevision,nextValue);
  Check(fields.Value(id)==oldValue&&fields.NeedsSubmit(id),"older submission result cleared newer edit revision");
 }
}
static void DelayedUiCase(studio033::State baseline){
 using namespace nrcontrolsabi;studio033::View view;view.page=1;baseline.values[Style]=0;baseline.values[Passes]=1;
 for(int settle=0;settle<3;++settle)Frame(baseline,view);
 Click(baseline,view,"nr_style_1");auto e=Click(baseline,view,"nr_style_1_1");
 Check(!e.changed&&baseline.values[Style]==0,"style selection submitted before Apply");e=Click(baseline,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<Style),"style Apply did not submit");studio033::SubmitEdits(baseline,view,e,[](uint32_t id,float value){return Valid(id,value);});
 Check(view.drafts.InFlight(Style),"real Apply did not preserve its accepted request");
 baseline.values[Style]=0;baseline.controlsFresh=false;
 Click(baseline,view,"nr_style_1");e=Click(baseline,view,"nr_style_1_2");
 Check(!e.changed,"new style edit bypassed Apply");e=Click(baseline,view,"apply_appearance");
 studio033::SubmitEdits(baseline,view,e,[](uint32_t,float){return false;});
 baseline.values[Style]=1;baseline.controlsFresh=true;baseline.controlPending=0;Frame(baseline,view);
 Check(view.drafts.Value(Style)==2&&view.drafts.NeedsSubmit(Style),"delayed actual UI snapshot overwrote newer style draft");
 e=Click(baseline,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<Style)&&baseline.values[Style]==2,"newer UI draft did not submit after delayed ack");
 studio033::SubmitEdits(baseline,view,e,[](uint32_t id,float value){return Valid(id,value);});baseline.modelPending=true;
 Frame(baseline,view);Check(items.count("model_submit_pending")!=0,"submitted model wait is not distinguished from an unsent draft");
 e=Click(baseline,view,"apply_appearance");Check(e.changed==0,"clean pending request was unnecessarily resubmitted");
}


static void ManualDragCase(studio033::State s){
 using namespace nrcontrolsabi;studio033::View view;view.page=1;view.layer=0;
 s.available=true;s.controlsFresh=true;s.controlPending=0;s.values[Passes]=1;s.values[Intensity]=.25f;
 for(int settle=0;settle<3;++settle)Frame(s,view);
 auto& io=ImGui::GetIO();auto pt=items.at("整体强度");
 io.AddMousePosEvent(pt.x,pt.y);Frame(s,view);io.AddMouseButtonEvent(0,true);
 auto e=Frame(s,view);Check(!e.changed,"drag start rebuilt the model");
 for(int i=0;i<5;++i){io.AddMousePosEvent(pt.x+float(i*4),pt.y);e=Frame(s,view);Check(!e.changed,"held drag submitted a model request");}
 Check(view.drafts.NeedsSubmit(Intensity)&&s.values[Intensity]==.25f,"held drag lost draft or changed requested model");
 io.AddMouseButtonEvent(0,false);e=Frame(s,view);
 Check(!e.changed&&s.values[Intensity]==.25f,"drag release submitted before Apply");
 const float draft=view.drafts.Value(Intensity);Check(draft!=.25f,"actual slider did not edit draft");
 for(int i=0;i<5;++i)Check(!Frame(s,view).changed,"idle frame submitted unapplied model draft");
 e=Click(s,view,"save_settings");Check(e.save&&!e.changed&&s.values[Intensity]==.25f,"Save applied an NR draft");
 e=Click(s,view,"model_layer_2");Check(!e.changed&&s.values[Intensity]==.25f,"accordion switch applied NR draft");
 view.page=5;for(int i=0;i<3;++i)Check(!Frame(s,view).changed,"SR tab applied NR draft");
 view.page=1;for(int i=0;i<3;++i)Check(!Frame(s,view).changed,"returning to NR applied draft");
 e=Click(s,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<Intensity)&&s.values[Intensity]==draft,"Apply did not submit the released value");
 studio033::SubmitEdits(s,view,e,[](uint32_t id,float value){return Valid(id,value);});
 for(int i=0;i<5;++i)Check(!Frame(s,view).changed,"idle frame repeated an applied request");
 e=Click(s,view,"nr_count_3");Check(!e.changed&&s.values[Passes]==1&&view.drafts.Value(Passes)==3,"layer count changed before Apply");
 e=Click(s,view,"save_settings");Check(e.save&&!e.changed&&s.values[Passes]==1,"Save applied a layer-count draft");
 e=Click(s,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<Passes)&&s.values[Passes]==3,"Apply did not submit selected layer count");
 studio033::SubmitEdits(s,view,e,[](uint32_t,float){return false;});
 for(int i=0;i<5;++i)Check(!Frame(s,view).changed,"queue rejection caused automatic retry");
 Check(view.drafts.Rejected(Passes)&&view.drafts.Value(Passes)==3,"rejected request disappeared");
 e=Click(s,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<Passes),"rejected request cannot be retried manually");
 studio033::SubmitEdits(s,view,e,[](uint32_t id,float value){return Valid(id,value);});Frame(s,view);
 e=Click(s,view,"nr_count_2");Check(!e.changed&&s.values[Passes]==3,"new layer count applied early");
 e=Click(s,view,"discard_appearance");Check(!e.changed&&view.drafts.Value(Passes)==3&&!view.drafts.NeedsSubmit(Passes),"Discard failed to restore applied count");
 // SR precision retains its separate explicit Apply transaction and does not
 // leak through NR Apply, accordion navigation or Save.
 view.page=5;view.srLayer=0;s.values[Work]=s.values[PassWork]=s.values[PassWork3]=100;
 for(int i=0;i<3;++i)Frame(s,view);
 e=Click(s,view,"sr_layer_1");Check(!e.changed&&s.values[Work]==100,"SR slider release applied precision");
 const float srDraft=view.drafts.Value(Work);Check(srDraft!=100,"actual SR slider did not edit draft");
 e=Click(s,view,"save_settings");Check(e.save&&!e.changed&&s.values[Work]==100,"Save applied SR precision draft");
 e=Click(s,view,"sr_open_2");Check(!e.changed&&s.values[Work]==100,"SR accordion applied precision draft");
 view.page=1;for(int i=0;i<3;++i)Check(!Frame(s,view).changed,"NR tab applied SR draft");
 e=Click(s,view,"apply_appearance");Check(!e.changed&&s.values[Work]==100,"NR Apply committed SR precision");
 view.page=5;for(int i=0;i<3;++i)Frame(s,view);
 e=Click(s,view,"apply_sr");Check(e.changed==(uint64_t(1)<<Work)&&s.values[Work]==srDraft,"SR Apply did not submit precision");
 studio033::SubmitEdits(s,view,e,[](uint32_t id,float value){return Valid(id,value);});
}

static void DelayedSrUiCase(studio033::State baseline){
 using namespace nrcontrolsabi;studio033::View view;view.page=5;
 baseline.values[Work]=baseline.values[PassWork]=baseline.values[PassWork3]=100;
 for(int settle=0;settle<3;++settle)Frame(baseline,view);
 Click(baseline,view,"sr_layer_1");auto e=Click(baseline,view,"apply_sr");const float first=baseline.values[Work];
 studio033::SubmitEdits(baseline,view,e,[](uint32_t id,float value){return Valid(id,value);});
 baseline.values[Work]=100;baseline.controlsFresh=false;Click(baseline,view,"sr_layer_1",40.f);
 const float newer=view.drafts.Value(Work);Check(newer!=first,"SR delayed edit fixture did not move the actual slider");
 baseline.values[Work]=first;baseline.controlsFresh=true;baseline.controlPending=0;Frame(baseline,view);
 Check(view.drafts.Value(Work)==newer&&view.drafts.NeedsSubmit(Work),"actual SR old ack overwrote newer precision draft");
 e=Click(baseline,view,"apply_sr");Check(e.changed==(uint64_t(1)<<Work)&&baseline.values[Work]==newer,"newer SR draft failed to submit");
}
static void PinnedModelUiCase(studio033::State baseline){
 using namespace nrcontrolsabi;studio033::View view;view.page=1;baseline.values[Preset]=2;baseline.values[Style]=3;
 for(int settle=0;settle<3;++settle)Frame(baseline,view);
 Check(!items.count("nr_preset_1")&&!items.count("nr_preset_1_2"),"single-preset model still exposes false preset alternatives");
 Check(items.count("legacy_style_mapping")!=0&&baseline.values[Style]==3&&!view.drafts.NeedsSubmit(Style),"legacy style 3 was silently rewritten");
 Check(baseline.values[Preset]==2&&!view.drafts.NeedsSubmit(Preset),"fixed model display rewrote compatible saved preset ID");
 for(auto id:{Preset,L2Preset,L3Preset}){for(float value:{0.f,1.f,2.f,3.f})Check(Valid(id,value),"legacy integer preset ABI compatibility changed");Check(!Valid(id,.5f),"legacy integer preset ABI accepted a fraction");}
}

int main(){try{
 DraftCases();
 {studio033::State state;studio033::View view;studio033::Edits edits;
  state.values[nrcontrolsabi::NaturalLook]=.75f;
  studio033::Changed(edits,nrcontrolsabi::NaturalLook);
  Check(edits.changed==(uint64_t(1)<<63),"64th control occupies the valid highest unsigned mask bit");
  unsigned calls=0;studio033::SubmitEdits(state,view,edits,[&](uint32_t id,float value){++calls;
   Check(id==nrcontrolsabi::NaturalLook&&value==.75f,"highest control bit reaches the real submit path");return true;});
  Check(calls==1,"highest control bit neither disappears nor aliases a lower control");}

 struct Consumer {pregrade::Settings pre;int work=100,passwork=100,passwork3=100;int enabled=0,passes=1,hotkey=0x7A,style=0,preset=0,auto_mask=0,ui_correct=0;
  float intensity=0,local_structure=0,local_tone=0,global_tone=0,skin_structure=-1,skin_lift=.35f,sharpen=0.f,natural_look=0.f;nrlayers::Model extra[2];};
 Consumer mirror;
 K033_Settings sharedGrade{sizeof(K033_Settings),1,1,2,.2f,1.1f,.9f,.05f,-.05f,.1f,.75f};
 K033_NrSettings sharedNr{};sharedNr.size=sizeof(sharedNr);sharedNr.version=6;sharedNr.enabled=1;sharedNr.layers=3;sharedNr.appearance_epoch=1;
 for(unsigned i=0;i<3;++i){sharedNr.layer[i]={i,3-i,i%2,(i+1)%2,.5f+i*.5f,.25f+i*.25f,.1f+i*.2f,.2f+i*.3f};sharedNr.sr_work[i]=100;}
 k033beta2::ApplyGrade(mirror,sharedGrade);k033beta2::ApplyNr(mirror,sharedNr);
 const auto gradeAgain=k033beta2::Grade(mirror);const auto nrAgain=k033beta2::Nr(mirror);
 Check(!std::memcmp(&sharedGrade,&gradeAgain,sizeof(sharedGrade)),"S53 grade mapping lost parameters");
 Check(!std::memcmp(&sharedNr,&nrAgain,sizeof(sharedNr)),"S53 independent three-layer mapping lost parameters");
 const auto untouchedGrade=mirror.pre;mirror.enabled=0;
 Check(!std::memcmp(&untouchedGrade,&mirror.pre,sizeof(mirror.pre))&&mirror.passes==3&&mirror.hotkey==0x7A,"NR-only toggle altered grade/layers/hotkey");
 IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1040,2200};io.DeltaTime=1.f/60;
 io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",18,nullptr,io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
 int artW=0,artH=0,artC=0;auto* artPixels=stbi_load("../../assets/yanyun-approved-panel.png",&artW,&artH,&artC,4);
 Check(artPixels&&yanyunart::Install(io.Fonts,artPixels,artW,artH),"production artwork installed in CPU atlas");
 {
  ImFontAtlasRect artRect;Check(io.Fonts->GetCustomRect(yanyunart::rectangles.at(io.Fonts),&artRect),"art rect available");
  auto atlasPixel=[&](int x,int y){return static_cast<unsigned char*>(io.Fonts->TexData->GetPixelsAt(artRect.x+x,artRect.y+y));};
  for(int y=61;y<181;y+=7)for(int x=22;x<757;x+=7)
   Check(std::memcmp(atlasPixel(x,y),artPixels+(y*artW+x)*4,4)==0,"complete header was changed by icon matting");
  for(auto pt:{ImVec2(126,302),ImVec2(443,302),ImVec2(57,807),ImVec2(57,856)})
   Check(atlasPixel(int(pt.x),int(pt.y))[3]==0,"icon retains rectangular background");
 }
 stbi_image_free(artPixels);
 studio033::visual::artwork=&yanyunart::Get;
 unsigned char* pixels=nullptr;int w=0,h=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(ImTextureID(1));
 studio033::observer=Observe;studio033::State s;studio033::View view;view.page=0;
 using namespace nrcontrolsabi;s.values[Enabled]=1;s.values[Work]=s.values[PassWork]=100;s.values[Passes]=1;
 s.modelActive=true;s.running=true;s.mfgAvailable=true;s.mfgRequested=s.mfgAccepted=6;
 ManualDragCase(s);DelayedUiCase(s);DelayedSrUiCase(s);PinnedModelUiCase(s);
 // Select every integer style through the real per-layer combo, then apply all drafts.
 s.values[Passes]=3;view.page=1;
 uint64_t styleMask=0;
 for(int layer=0;layer<3;++layer){
  char selector[32],menu[32],choice[40];std::snprintf(selector,sizeof(selector),"model_layer_%d",layer+1);
  std::snprintf(menu,sizeof(menu),"nr_style_%d",layer+1);std::snprintf(choice,sizeof(choice),"nr_style_%d_%d",layer+1,layer%2+1);
  if(view.layer!=layer)Click(s,view,selector);for(int settle=0;settle<3;++settle)Frame(s,view);
  Check(view.layer==layer,"NR accordion did not select exactly one layer");
  for(int other=0;other<3;++other){char otherMenu[32];std::snprintf(otherMenu,sizeof(otherMenu),"nr_style_%d",other+1);Check(bool(items.count(otherMenu))==(other==layer),"NR accordion exposed another layer body");}
  Click(s,view,menu);auto draft=Click(s,view,choice);
  Check(!draft.changed&&s.values[LayerId(Style,layer)]==0,"per-layer style submitted before Apply");
  Check(view.drafts.Value(LayerId(Style,layer))==float(layer%2+1),"per-layer draft lost its selection");
  styleMask|=uint64_t(1)<<LayerId(Style,layer);
 }
 auto request=Click(s,view,"apply_appearance");Check(request.changed==styleMask,"Apply did not submit all edited layers together");
 for(int layer=0;layer<3;++layer){const auto id=LayerId(Style,layer);Check(s.values[id]==float(layer%2+1)&&Valid(id,s.values[id]),"combo sent a fractional or incorrect style");}
 studio033::SubmitEdits(s,view,request,[](uint32_t id,float value){return id!=L2Style&&Valid(id,value);});
 Check(view.drafts.RejectedMask()==(uint64_t(1)<<L2Style),"queue rejection was silently cleared");
 request=Click(s,view,"apply_appearance");Check(request.changed==(uint64_t(1)<<L2Style),"rejected field was not retryable");
 studio033::SubmitEdits(s,view,request,[](uint32_t id,float value){return Valid(id,value);});Check(view.drafts.RejectedMask()==0,"accepted retry retained failure");
 view.page=5;s.values[Work]=s.values[PassWork]=s.values[PassWork3]=100;
 for(int layer=0;layer<3;++layer){char header[32],slider[32];std::snprintf(header,sizeof(header),"sr_open_%d",layer+1);std::snprintf(slider,sizeof(slider),"sr_layer_%d",layer+1);
  if(view.srLayer!=layer)Click(s,view,header);for(int settle=0;settle<3;++settle)Frame(s,view);Check(view.srLayer==layer,"SR accordion did not select exactly one layer");
  for(int other=0;other<3;++other){char otherId[32];std::snprintf(otherId,sizeof(otherId),"sr_layer_%d",other+1);Check(bool(items.count(otherId))==(other==layer),"SR accordion exposed another layer body");}
  const auto counts=s.values[Passes];auto untouched=s;Click(s,view,slider);
  Check(s.values[Passes]==counts&&!std::memcmp(s.values,untouched.values,sizeof(s.values)),"SR draft selection changed submitted parameters");
 }
 request=Click(s,view,"apply_sr");const uint64_t srMask=(uint64_t(1)<<Work)|(uint64_t(1)<<PassWork)|(uint64_t(1)<<PassWork3);
 Check(request.changed==srMask,"SR page did not submit three independent precision fields");
 Check(s.values[Work]!=100&&s.values[PassWork]!=100&&s.values[PassWork3]!=100,"SR edits remained display-only");
 for(auto id:{Work,PassWork,PassWork3})Check(Valid(id,s.values[id]),"SR precision is not an exact valid integer");
 // Restore fixture defaults for the existing regression cases below.
 view={};view.page=0;for(int layer=0;layer<3;++layer){s.values[LayerId(Preset,layer)]=0;s.values[LayerId(Style,layer)]=0;}
 s.values[Passes]=1;s.values[Work]=s.values[PassWork]=s.values[PassWork3]=100;

 Check(!Valid(RetiredEffect,1)&&Valid(RetiredEffect,0),"retired effect cannot accept a nonzero request");
 for(float width:{420.f,1040.f}){io.DisplaySize.x=width;Frame(s,view);
   Check(!items.count("美肤强度")&&!items.count("post_beauty"),"removed beauty UI still visible");
  Check(!items.count("肤色提亮（NR 前）")&&!items.count("前置肤色保护")&&!items.count("提亮程度"),"removed skin controls still visible");}
 Check(Valid(Passes,3)&&!Valid(Passes,4),"NR three-layer ceiling not enforced by production ABI");
 Check(!items.count("皮肤细节")&&!items.count("自动识别皮肤"),"removed beta2 skin UI unexpectedly restored");
 // V6.1 (owner 2026-09-13 「弄回来」): the per-layer skin-structure slider is back, tagged （实验）, and is drafted/applied
 // with the other model parameters. The old guard asserted the opposite from when the control had been removed.
 {bool skinInAppearance=false;for(auto first:studio033::Appearance)skinInAppearance|=first==Skin;Check(skinInAppearance,"V6.1 per-layer skin structure control missing from appearance edit map");}
 struct LegacyConfig {int passes=4;float faceboost=1;int portrait_enabled=1;float portrait_strength=1;struct {float skinProtection=1;} pre;int work=100;int style=2;};
 LegacyConfig legacy;nrfeatures::Restrict(legacy);
 Check(legacy.passes==3&&legacy.faceboost==0&&legacy.portrait_enabled==0&&legacy.portrait_strength==0&&legacy.pre.skinProtection==0,"old config revives removed processing");
 Check(legacy.work==100&&legacy.style==2&&s.mfgRequested==6,"requested removal changed unrelated quality or native FG");
 studio033::Edits e;
 e=Click(s,view,"light_natural");
 Check(s.values[Grade]==1&&s.values[PreStyle]==0&&s.values[Contrast]==1.03f&&s.values[Saturation]==1.02f&&s.values[Highlights]==.04f,"S52 light-natural shortcut values changed");
 Check(s.values[Enabled]==1&&s.values[Passes]==1&&s.mfgRequested==6&&s.mfgAccepted==6,"light-natural shortcut changed NR/FG intent");
 // Operate the real three-layer editor in a private ImGui context only.
 view.page=1;s.values[Passes]=3;
 for(int layer=0;layer<3;++layer){
  s.values[LayerId(Intensity,layer)]=.25f+layer*.25f;s.values[LayerId(Style,layer)]=float(layer);
 }
 e=Click(s,view,"model_layer_2");Check(view.layer==1&&!e.changed,"layer selector must not change model values");
 Click(s,view,"native_style");e=Click(s,view,"native_style_2");
 Check(!e.changed&&s.values[L2Style]==1,"second-layer draft applied without confirmation");e=Click(s,view,"apply_appearance");
 Check(e.changed==(uint64_t(1)<<L2Style)&&s.values[Style]==0&&s.values[L2Style]==2&&s.values[L3Style]==2,"second-layer apply leaks to another layer");
 studio033::SubmitEdits(s,view,e,[](uint32_t id,float value){return Valid(id,value);});
 s.values[Passes]=1;Click(s,view,"model_layer_3");Click(s,view,"native_style");e=Click(s,view,"native_style_0");
 Check(!e.changed&&s.values[L3Style]==2,"inactive third-layer draft applied without confirmation");e=Click(s,view,"apply_appearance");Check(e.changed==(uint64_t(1)<<L3Style)&&s.values[L3Style]==0,"inactive third layer cannot be applied independently");
 studio033::SubmitEdits(s,view,e,[](uint32_t id,float value){return Valid(id,value);});
 Check(s.values[Passes]==1&&s.values[L2Intensity]==.5f&&s.values[L3Intensity]==.75f,"editing inactive layer changes count or another parameter");
 for(int layer=0;layer<3;++layer)for(auto first:studio033::Appearance){auto id=LayerId(first,layer);Check(Valid(id,s.values[id]),"per-layer ABI mapping invalid");}
 for(float width:{420.f,1040.f}){io.DisplaySize.x=width;for(int layer=0;layer<3;++layer){view.layer=layer;Frame(s,view);}}
 io.DisplaySize.x=1040;view.page=3;s.values[White]=3.16f;s.values[WhiteTrim]=1;s.values[Replica]=1;s.whiteEncoding=1;
 e=Click(s,view,"白点尺度");Check(!e.changed&&s.values[White]==3.16f,"legacy fixed white unexpectedly editable");
 Click(s,view,"white_source");e=Click(s,view,"white_source_1");
 Check(s.values[WhiteSource]==1&&(e.changed&(uint64_t(1)<<WhiteSource)),"fixed white source not bound");
 e=Click(s,view,"白点尺度");Check((e.changed&(uint64_t(1)<<White))&&s.values[White]>15,"explicit fixed white still blocked by replica");
 e=Click(s,view,"曝光微调");Check(!e.changed&&s.values[WhiteTrim]==1,"fixed source accepts inactive exposure trim");
 Click(s,view,"white_source");e=Click(s,view,"white_source_2");
 Check(s.values[WhiteSource]==2&&(e.changed&(uint64_t(1)<<WhiteSource)),"game exposure source not bound");
 e=Click(s,view,"曝光微调");Check(e.changed&(uint64_t(1)<<WhiteTrim),"game exposure trim not bound");
 s.whiteEncoding=0;const float savedWhite=s.values[White];e=Click(s,view,"备用白点尺度");
 Check(!e.changed&&s.values[White]==savedWhite,"SDR no-op control accepts edits");
 s.whiteEncoding=1;s.gradeAvailable=false;e=Click(s,view,"备用白点尺度");Check(!e.changed,"unavailable normalization accepts edits");s.gradeAvailable=true;
 for(float width:{420.f,1040.f}){io.DisplaySize.x=width;for(unsigned status=0;status<=unsigned(exposurepolicy::Status::Frozen);++status){s.whiteStatus=exposurepolicy::Status(status);Frame(s,view);}}
 Check(s.mfgRequested==6&&s.mfgAccepted==6,"normalization edits changed native multiplier");
 s.paused=true;s.recoveryReady=false;view.page=0;
 e=Click(s,view,"recover_nr");Check(e.recover,"recovery button not bound");
 e=Click(s,view,"toggle_nr");Check(!e.toggle,"paused session must not enable half-registered renderer");
 s.recoveryReady=true;Frame(s,view);Check(!items.count("recover_nr"),"scheduled recovery still offers ineffective repeated clicks");
 s.recoveryReady=false;s.recoveryWriteFailed=true;Frame(s,view);Check(items.count("recover_nr")>0,"failed recovery must remain retryable");
 // Rasterize the same production body/header/pages/footer at C's narrow size.
 visualEvidenceMode=true;s.paused=false;s.values[Passes]=3;view={};view.page=1;io.DisplaySize={420,780};
 for(int settle=0;settle<3;++settle)Frame(s,view);rasterFile="panel-c4-nr-420-cpu.bmp";Frame(s,view);
 Check(items.count("author_fixed")&&items.at("author_fixed").y>730,"author is not fixed at the compact window bottom");
 Check(itemBottoms.count("apply_appearance")&&itemBottoms.at("apply_appearance")<items.at("author_fixed").y,"NR Apply is below fixed author footer");
 for(int layer=1;layer<=3;++layer){char id[32];std::snprintf(id,sizeof(id),"model_layer_%d",layer);Check(itemBottoms.count(id)&&itemBottoms.at(id)<items.at("author_fixed").y,"NR layer heading below fixed author footer");}
 view.page=5;view.srLayer=1;for(int settle=0;settle<3;++settle)Frame(s,view);rasterFile="panel-c4-sr-420-cpu.bmp";Frame(s,view);
 Check(itemBottoms.count("apply_sr")&&itemBottoms.at("apply_sr")<items.at("author_fixed").y,"SR Apply is below fixed author footer");
 // Dedicated recipe interaction uses actual ImGui controls with no OS input.
  for(unsigned i=0;i<nrcontrolsabi::Count;++i)s.values[i]=nrcontrolsabi::definitions[i].minimum;
  s.available=true;s.controlsFresh=true;s.paused=false;view.page=6;io.DisplaySize={420,1800};
  for(int i=0;i<3;++i)Frame(s,view);
  Click(s,view,"yy_preset_0");Check(yyView.draft.kind==yanyunrecipe::Realistic,"preset button changes draft");
  const auto beforeGroups=yyView.draft;Click(s,view,"yy_group_1");Check(yyView.group==1,"character selection");
  Click(s,view,"yy_apply_whole");Check(yyEdits.applyWhole&&yyView.draft.regional==1,"regional apply preserves mode and both groups");
  Check(!std::memcmp(&beforeGroups,&yyView.draft,sizeof beforeGroups),"group selection changes no values");
  Click(s,view,"yy_group_0");Click(s,view,"yy_apply_whole");Check(yyEdits.applyWhole,"whole apply is explicitly bound");
  Click(s,view,"yy_mask_preview");Check(yyView.preview,"mask preview bound");
  Click(s,view,"yy_save");Check(yyEdits.save,"draft save bound");
  for(int group=0;group<3;++group){yyView.group=group;io.DisplaySize={420,860};for(int i=0;i<3;++i)Frame(s,view);
   rasterFile=group==0?"yanyun-whole-cpu.bmp":group==1?"yanyun-character-cpu.bmp":"yanyun-scene-cpu.bmp";Frame(s,view);}

  // Dedicated production NR body: three simultaneous columns, independent
  // layer accordions, integer count buttons and no implicit model submission.
  columnsMode=true;view.page=1;yyView={};io.DisplaySize={1260,2200};
  for(unsigned i=0;i<Count;++i)s.values[i]=definitions[i].minimum;
  s.values[Passes]=1;s.values[Work]=s.values[PassWork]=s.values[PassWork3]=70;
  for(int i=0;i<3;++i)Frame(s,view);
  Check(items.at("yy_column_0").x<items.at("yy_column_1").x&&items.at("yy_column_1").x<items.at("yy_column_2").x,"three NR columns not displayed together");
  const auto submittedBefore=s;
  for(unsigned g=0;g<3;++g){
   Click(s,view,g?"yy_mode_regional":"yy_mode_whole");
   auto before=yyView.draft;char key[48];std::snprintf(key,sizeof(key),"yy_count_%u_%u",g,g+1);Click(s,view,key);
   Check(yanyunrecipe::Get(yyView.draft,yanyunrecipe::Group(g),Passes)==float(g+1),"per-column count button failed");
   for(unsigned other=0;other<3;++other)if(other!=g)Check(!std::memcmp(before.values[other],yyView.draft.values[other],sizeof before.values[other]),"count crossed columns");
   for(int layer=0;layer<3;++layer){
    std::snprintf(key,sizeof(key),"yy_layer_%u_%d",g,layer+1);if(yyView.opened[g]!=layer)Click(s,view,key);
    Check(yyView.opened[g]==layer,"per-column accordion failed");
    std::snprintf(key,sizeof(key),"yy_style_%u_%d",g,layer+1);Click(s,view,key);
    std::snprintf(key,sizeof(key),"yy_style_%u_%d_%d",g,layer+1,1+layer%2);Click(s,view,key);
    Check(yanyunrecipe::Get(yyView.draft,yanyunrecipe::Group(g),LayerId(Style,layer))==float(1+layer%2),"style edited wrong group/layer");
    Check(!yyEdits.applyWhole&&!std::memcmp(s.values,submittedBefore.values,sizeof s.values),"editing auto-applied a model request");
   }
  }
  const auto allDrafts=yyView.draft;Click(s,view,"yy_mode_regional");
  Check(yyView.draft.regional==1&&!std::memcmp(allDrafts.values,yyView.draft.values,sizeof allDrafts.values),"mode switch lost a column");
  Click(s,view,"yy_apply_whole");Check(yyEdits.applyWhole,"regional application not emitted");
  for(int i=0;i<5;++i){Frame(s,view);Check(!yyEdits.applyWhole,"apply action leaked into another frame");}
  Click(s,view,"yy_mode_whole");Click(s,view,"yy_apply_whole");Check(yyEdits.applyWhole&&yyView.draft.regional==0,"whole mode applied regional processing");
  Frame(s,view);Check(!items.count("yy_preset_0")&&!items.count("yy_preset_1")&&!items.count("yy_preset_2"),"removed preset buttons remain in production toolbar");
  Check(yyView.draft.regional==0&&!yyEdits.applyWhole,"toolbar changed explicitly chosen mode or auto-applied");
  const auto savedDraft=yyView.draft;studio033::RecipeSaved(yyView,true);Frame(s,view);
  Check(yyView.dirty&&!yyEdits.applyWhole&&!std::memcmp(savedDraft.values,yyView.draft.values,sizeof savedDraft.values),"saving draft hid pending edits or applied settings");
  yyView.dirty=false;studio033::RecipeLoaded(yyView,savedDraft);Frame(s,view);Check(yyView.dirty&&!yyEdits.applyWhole,"loading draft treated it as applied");
  Click(s,view,"yy_mode_regional");
  yyView.opened[0]=yyView.opened[1]=yyView.opened[2]=0;
  for(float width:{980.f,1260.f,1600.f}){io.DisplaySize={width,2200};for(int i=0;i<3;++i)Frame(s,view);Check(items.count("yy_column_2"),"scene column missing");}
  io.DisplaySize={1080,940};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s3-nr-columns-cpu.bmp";Frame(s,view);
  Check(items.count("yy_fold_0")&&!items.count("yy_count_0_1")&&items.count("yy_count_1_1")&&items.count("yy_count_2_1"),"regional mode did not fold whole bank");
  char workKey[48];std::snprintf(workKey,sizeof(workKey),"yy_value_1_%u",unsigned(Work));Check(!items.count(workKey),"SR precision duplicated on NR page");
  Click(s,view,"page_1");Check(view.page==5,"SR is not second tab");Frame(s,view);
  Check(items.count(workKey)&&!items.count("yy_count_1_1"),"SR/NR controls not separated");
  const auto srBefore=yyView.draft;Click(s,view,workKey,24);Frame(s,view);
  Check(yanyunrecipe::Get(srBefore,yanyunrecipe::Character,Work)!=yanyunrecipe::Get(yyView.draft,yanyunrecipe::Character,Work),"SR slider did not edit person recipe");
  Check(!std::memcmp(srBefore.values[0],yyView.draft.values[0],sizeof srBefore.values[0])&&!std::memcmp(srBefore.values[2],yyView.draft.values[2],sizeof srBefore.values[2]),"SR crossed recipe banks");
  rasterFile="yanyun-s3-sr-cpu.bmp";Frame(s,view);
  Click(s,view,"page_0");Check(view.page==0,"pre is not first tab");Frame(s,view);
  char gradeKey[48];std::snprintf(gradeKey,sizeof(gradeKey),"yy_value_1_%u",unsigned(Grade));Check(items.count(gradeKey)&&!items.count(workKey),"pre controls overlap SR");
  const auto preBefore=yyView.draft;Click(s,view,gradeKey);Frame(s,view);
  Check(yanyunrecipe::Get(preBefore,yanyunrecipe::Character,Grade)!=yanyunrecipe::Get(yyView.draft,yanyunrecipe::Character,Grade),"pre edited wrong bank");
  Check(!std::memcmp(preBefore.values[0],yyView.draft.values[0],sizeof preBefore.values[0])&&!std::memcmp(preBefore.values[2],yyView.draft.values[2],sizeof preBefore.values[2]),"pre crossed recipe banks");
  rasterFile="yanyun-s3-pre-cpu.bmp";Frame(s,view);
  const auto bankBefore=yyView.draft;Click(s,view,"page_2");Check(view.page==1,"NR is not third tab");
  Click(s,view,"yy_fold_0");Frame(s,view);
  Check(yyView.draft.regional==0&&items.count("yy_count_0_1")&&items.count("yy_fold_1")&&items.count("yy_fold_2")&&!items.count("yy_count_1_1")&&!items.count("yy_count_2_1"),"whole mode did not fold both regional banks");
  Check(!std::memcmp(bankBefore.values,yyView.draft.values,sizeof bankBefore.values),"fold discarded parameter bank");
  rasterFile="yanyun-s3-whole-cpu.bmp";Frame(s,view);
  Click(s,view,"page_3");Check(view.page==2,"FG is not fourth tab");
  columnsMode=false;
  view.page=7;io.DisplaySize={380,730};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-monitor-cpu.bmp";Frame(s,view);
  // S4 production at larger font sizes: same real ImGui controls and viewport limits.
  columnsMode=true;view.page=1;yyView.outputW=yyView.renderW=5120;yyView.outputH=yyView.renderH=2160;
  io.DisplaySize={1500,2000};yyView.draft.regional=1;yyView.opened[1]=yyView.opened[2]=0;
  yanyunrecipe::Set(yyView.draft,yanyunrecipe::Character,Full,1);yanyunrecipe::Set(yyView.draft,yanyunrecipe::Character,Work,200);
  Click(s,view,"yy_apply_whole");Check(!yyEdits.applyWhole,"5K out-of-contract precision may not be applied");
  yanyunrecipe::Set(yyView.draft,yanyunrecipe::Character,Work,100);Click(s,view,"yy_apply_whole");Check(yyEdits.applyWhole,"valid 5K precision rejected");
  ImGui::GetStyle().FontSizeBase=20;ImGui::GetStyle().FontScaleMain=1;
  io.DisplaySize={1350,1064};yyView.runtimeNote="分区尚未生效时，这里显示具体原因";yyView.dirty=false;
  for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-nr-cpu.bmp";Frame(s,view);
  view.page=5;for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-sr-cpu.bmp";Frame(s,view);
  view.page=0;for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-pre-cpu.bmp";Frame(s,view);
  yyView.draft.regional=0;view.page=1;for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-whole-cpu.bmp";Frame(s,view);
  yyView.draft.regional=1;ImGui::GetStyle().FontScaleMain=2;io.DisplaySize={2700,2144};
  for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-5k-cpu.bmp";Frame(s,view);
  ImGui::GetStyle().FontScaleMain=1;io.DisplaySize={1040,704};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-720p-cpu.bmp";Frame(s,view);
  columnsMode=false;view.page=7;io.DisplaySize={450,750};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s4-monitor-cpu.bmp";Frame(s,view);
  // S5: changing presentation must not mutate or submit any render recipe.
  columnsMode=true;view.page=1;io.DisplaySize={950,1064};yyView.runtimeNote="";
  const auto appearanceRecipe=yanyunrecipe::Encode(yyView.draft);
  for(bool light:{false,true})for(bool english:{false,true}){
   // S23: real header controls in both themes (no painted banner, no theme menu).
   Click(s,view,light?"theme_light":"theme_dark");Click(s,view,english?"language_en":"language_zh");
   for(const char* key:{"theme_dark","theme_light","language_zh","language_en","minimize_panel","maximize_panel","close_panel"})
    Check(items.count(key)!=0,"header control missing");
   Check(items.at("close_panel").x>items.at("language_en").x&&items.at("language_en").x>items.at("theme_light").x&&items.at("page_0").y>items.at("close_panel").y,
    "header controls not right-aligned above the page tabs");
   Check(yyappearance::light==light&&yyappearance::english==english,"theme/language choices not independent");
   Check(yanyunrecipe::Encode(yyView.draft)==appearanceRecipe&&!yyEdits.applyWhole,"appearance changed or submitted render recipe");
   Check(std::strcmp(studio033::L("保存与分享"),english?"Save and share":"保存与分享")==0,"locale did not change live");
   {studio033::Theme theme;
    const auto base=ImGui::ColorConvertFloat4ToU32(studio033::visual::surface());
    for(auto colour:{ImGuiCol_WindowBg,ImGuiCol_ChildBg,ImGuiCol_Button,ImGuiCol_Header,ImGuiCol_FrameBg})
     Check(ImGui::GetColorU32(colour)==base,"panel/fold/utility/input backing colours differ");
    Check(studio033::visual::card_colour()==base,"layer card backing differs from common surface");
   }
   for(unsigned group:{1u,2u})for(auto field:{Skin,AutoMask}){char key[48];std::snprintf(key,sizeof(key),"yy_value_%u_%u",group,unsigned(field));
    // S18 user decision: skin options belong to the character column only.
    Check(group==1?items.count(key)!=0:items.count(key)==0,group==1?"character skin parameter is hidden behind More":"scene column still offers skin options");}
   const char* name=light?(english?"yanyun-s5-light-en.bmp":"yanyun-s5-light-zh.bmp"):(english?"yanyun-s5-dark-en.bmp":"yanyun-s5-dark-zh.bmp");
   for(int i=0;i<3;++i)Frame(s,view);rasterFile=name;Frame(s,view);
  }
  {auto he=Click(s,view,"maximize_panel");Check(he.maximize&&!he.close,"header maximize control");
   he=Click(s,view,"minimize_panel");Check(he.close&&!he.maximize,"header minimize control hides the panel");
   he=Click(s,view,"close_panel");Check(he.close&&!he.maximize,"header close control");}
  {// S23 (user): the panel opens with everything in view, no dragging or scrolling.
   Check(studio033::FitWindow(500,300,0,1400).x==800&&studio033::FitWindow(1300,-400,0,1400).x==900,"fit grows and shrinks to the content");
   Check(studio033::FitWindow(1500,400,100,1400).x==1384&&studio033::FitWindow(900,0,700,1400).y==492,"fit stays on the screen, moving up when needed");
   const ImVec2 display=io.DisplaySize;io.DisplaySize={900,1400};fitMode=true;view.page=1;yyView.draft.regional=1;
   auto hidden=[&](int n){for(int i=0;i<n;++i){ImGui::NewFrame();ImGui::EndFrame();}};
   fitStart={900,520};hidden(3);for(int i=0;i<8;++i)Frame(s,view); // opened after frames without the panel
   auto* fw=ImGui::FindWindowByName("033 fit");
   Check(fw&&fw->Size.y>520&&std::fabs(studio033::fit.overflow)<=3.f,"panel did not grow to show all content on opening");
   Check(fw->Pos.y+fw->Size.y<=io.DisplaySize.y-8+1,"fitted panel left the screen");
   const float snug=fw->Size.y;
   ImGui::SetWindowSize("033 fit",ImVec2(900,snug-150));for(int i=0;i<6;++i)Frame(s,view);
   Check(std::fabs(fw->Size.y-(snug-150))<.5f,"a size the user dragged was taken back without a new opening");
   hidden(3);for(int i=0;i<8;++i)Frame(s,view);
   Check(std::fabs(fw->Size.y-snug)<1.f&&std::fabs(studio033::fit.overflow)<=3.f,"reopening did not fit the panel again");
   ImGui::SetWindowSize("033 fit",ImVec2(900,1380));hidden(3);for(int i=0;i<8;++i)Frame(s,view);
   Check(std::fabs(fw->Size.y-snug)<1.f,"panel opened taller than its content was not made snug");
   view.page=5;for(int i=0;i<8;++i)Frame(s,view);
   Check(std::fabs(studio033::fit.overflow)<=3.f,"page change did not refit the panel");
   view.page=1;yyView.draft.regional=0;for(int i=0;i<8;++i)Frame(s,view);
   Check(std::fabs(studio033::fit.overflow)<=3.f,"whole/regional switch did not refit the panel");
   yyView.draft.regional=1;for(int i=0;i<8;++i)Frame(s,view);
   // Opening a card or adding saved records grows the panel; closing shrinks it back.
   io.DisplaySize.y=2000; // room for every record; the 1400 case above is screen-limited
   const float closedH=fw->Size.y;yyView.storageOpen=true;yyView.libraryLoaded=true;yyView.library.clear();for(int i=0;i<8;++i)Frame(s,view);
   const float openH=fw->Size.y;printf("fit: closed %.0f, sharing open %.0f\n",closedH,openH);
   Check(openH>closedH+40&&std::fabs(studio033::fit.overflow)<=3.f,"opening 保存与分享 did not grow the panel to show it");
   yanyunrecipe::LibraryEntry record;yanyunrecipe::SetName(record,"记录");record.recipe=yyView.draft;yyView.library.assign(2,record);for(int i=0;i<8;++i)Frame(s,view);
   printf("fit: two records %.0f\n",fw->Size.y);
   Check(fw->Size.y>openH+40&&std::fabs(studio033::fit.overflow)<=3.f,"new saved records did not grow the panel");
   yyView.library.clear();yyView.storageOpen=false;for(int i=0;i<8;++i)Frame(s,view);
   Check(std::fabs(fw->Size.y-closedH)<1.f,"closing 保存与分享 did not shrink the panel back");
   {// S25 (user 「面板会闪烁」): hints and status notes that come and go while the
    // player tunes never make the panel jump; only the player's own actions shrink it.
    yyView.detectionOpen=true;yyView.runtimeNote="";yyView.dirty=false;for(int i=0;i<8;++i)Frame(s,view);
    const float cardH=fw->Size.y;
    const char* longNote="人物分区平滑过渡中，这一行故意写得很长很长，让它在面板里折成好几行，模拟状态说明在游戏里变长变短、提示出现又消失的情况。";
    yyView.runtimeNote=longNote;yyView.dirty=true;for(int i=0;i<8;++i)Frame(s,view);
    const float grown=fw->Size.y;printf("fit: card %.0f, with long note and hint %.0f\n",cardH,grown);
    Check(grown>cardH+10&&studio033::fit.overflow<=3.f,"a longer note and the unapplied hint grow the panel to show them");
    bool steady=true;
    for(int round=0;round<3;++round){
     yyView.runtimeNote="";yyView.dirty=false;for(int i=0;i<8;++i)Frame(s,view);steady&=std::fabs(fw->Size.y-grown)<.5f;
     yyView.runtimeNote=longNote;yyView.dirty=true;for(int i=0;i<8;++i)Frame(s,view);steady&=std::fabs(fw->Size.y-grown)<.5f;
    }
    Check(steady,"notes and hints toggling made the panel jump (S23 shrank it back every time)");
    yyView.detectionOpen=false;for(int i=0;i<8;++i)Frame(s,view);
    Check(fw->Size.y<grown-20&&std::fabs(studio033::fit.overflow)<=3.f,"closing the card (a player action) shrinks the panel");
    yyView.runtimeNote="";yyView.dirty=false;
   }
   fitMode=false;io.DisplaySize=display;
  }
  io.DisplaySize.y=1800; // Expanded controls require scrolling in a shorter viewport.
  Click(s,view,"##yy_detection");Check(yyView.detectionOpen&&items.count("yy_mask_preview"),"recognition strip did not open real settings");
  s.values[Enabled]=0;Click(s,view,"yy_mask_preview");Check(!yyView.preview,"NR-off preview pretended to activate");
  s.values[Enabled]=1;yyView.recognitionFailed=true;Click(s,view,"yy_mask_preview");Check(!yyView.preview,"failed detector accepts misleading preview");
  yyView.recognitionFailed=false;yyView.regionalApplied=false;Click(s,view,"yy_mask_preview");Check(yyView.preview,"preview needs to work before applying regional draft");
  {// S28 (user: 「人物保真度和场景强度默认不打折，直接去掉！」): only feathering is left
   // under 人物识别; dragging it edits feathering and nothing else (S19 ID scopes).
   const char* feather=studio033::L("边缘柔化");auto& io3=ImGui::GetIO();
   yyView.draft.fidelity=.2f;yyView.draft.sceneStrength=.9f;yyView.draft.feather=1.f;
   for(int settle=0;settle<3&&!items.count(feather);++settle)Frame(s,view);Check(items.count(feather)!=0,"feather slider missing");
   Check(!items.count(studio033::L("人物保真"))&&!items.count(studio033::L("场景强度")),"person fidelity and scene strength sliders are gone");
   const auto at=items.at(feather);
   io3.AddMousePosEvent(at.x,at.y);Frame(s,view);io3.AddMouseButtonEvent(0,true);Frame(s,view);io3.AddMousePosEvent(at.x+40,at.y);Frame(s,view);io3.AddMouseButtonEvent(0,false);Frame(s,view);
   Check(yyView.draft.feather!=1.f,"feather slider did not edit feathering");
   Check(yyView.draft.fidelity==.2f&&yyView.draft.sceneStrength==.9f,"retired fields stay untouched in the record (ignored by the composite)");
  }
  Click(s,view,"##yy_detection");
  Click(s,view,"##yy_storage");Check(yyView.storageOpen&&items.count("yy_save"),"sharing strip did not open real actions");Click(s,view,"##yy_storage");
  {// S23 (user): saved records under 保存与分享 -- rename in place, one-click
   // switch, two-click delete; generated share codes carry the author credit.
   using namespace yanyunrecipe;
   const auto keepDraft=yyView.draft;yyView.storageOpen=true;yyView.libraryLoaded=true;yyView.library.clear();yyView.removeArmed=-1;
   for(int i=0;i<3;++i)Frame(s,view);
   Check(items.count("yy_save")&&items.count("yy_encode")&&items.count("yy_copy")&&items.count("yy_import")&&!items.count("yy_record_name_0"),"empty record list shows rows or lost an action");
   Check(!items.count("yy_load"),"the old single-slot load button is still offered");
   LibraryEntry a,b;SetName(a,"09-21 22:05");a.recipe=yyView.draft;SetName(b,"夜战");b.recipe=yyView.draft;b.recipe.regional=b.recipe.regional?0u:1u;b.recipe.fidelity=.42f;
   Check(Valid(b.recipe),"fixture record valid");yyView.library={a,b};for(int i=0;i<3;++i)Frame(s,view);
   for(const char* key:{"yy_record_name_0","yy_record_switch_0","yy_record_remove_0","yy_record_name_1","yy_record_switch_1","yy_record_remove_1"})Check(items.count(key)!=0,"record row control missing");
   Check(items.at("yy_record_name_0").x<items.at("yy_record_switch_0").x&&items.at("yy_record_switch_0").x<items.at("yy_record_remove_0").x&&
    items.at("yy_record_name_1").y>items.at("yy_record_name_0").y+10,"record row is not name | switch | delete, one row per record");
   Click(s,view,"yy_record_switch_1");Check(yyEdits.switchTo==1&&yyEdits.remove<0&&!yyEdits.libraryChanged,"switch not bound to its own row");
   Click(s,view,"yy_record_remove_0");Check(yyEdits.remove<0&&yyView.removeArmed==0,"first delete click must only arm");
   Click(s,view,"yy_record_remove_0");Check(yyEdits.remove==0&&yyView.removeArmed<0,"second delete click not bound to that row");
   Click(s,view,"yy_record_remove_1");Click(s,view,"yy_record_switch_0");Check(yyView.removeArmed<0&&yyEdits.switchTo==0&&yyEdits.remove<0,"switching left a delete armed");
   // Rename in place: every edit is written at once; leaving the field tidies the name.
   auto press=[&](ImGuiKey k,bool ctrl){if(ctrl){io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiMod_Ctrl,true);}io.AddKeyEvent(k,true);Frame(s,view);
    io.AddKeyEvent(k,false);if(ctrl){io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);}Frame(s,view);};
   Click(s,view,"yy_record_name_1");press(ImGuiKey_A,true);io.AddInputCharactersUTF8("  雨夜 人物  ");Frame(s,view);
   Check(yyEdits.libraryChanged&&std::string(yyView.library[1].name)=="  雨夜 人物  ","typing a name is not written at once");
   io.AddKeyEvent(ImGuiKey_Enter,true);bool committed=false;for(int i=0;i<3;++i){Frame(s,view);committed|=yyEdits.libraryChanged;if(!i)io.AddKeyEvent(ImGuiKey_Enter,false);}
   Check(committed&&std::string(yyView.library[1].name)=="雨夜 人物","leaving the name field did not tidy and keep it");
   Check(!std::memcmp(&yyView.library[1].recipe,&b.recipe,sizeof b.recipe),"renaming changed the saved parameters");
   Click(s,view,"yy_record_name_0");press(ImGuiKey_A,true);press(ImGuiKey_Backspace,false);press(ImGuiKey_Enter,false);Frame(s,view);
   Check(std::string(yyView.library[0].name)=="方案","an emptied name has no fallback");
   // Share: the code box holds the author credit and the whole draft.
   Click(s,view,"yy_encode");const std::string shared=yyView.code;Recipe got;
   Check(shared.rfind(ShareCredit,0)==0,"share code lacks the author credit");
   Check(DecodeShared(shared,got)==DecodeResult::Ok&&!std::memcmp(&got,&yyView.draft,sizeof got),"share code does not carry the whole draft");
   std::snprintf(yyView.code,sizeof(yyView.code),"朋友发来的：%s\r\n",ShareText(b.recipe).c_str());yyView.dirty=false;
   Click(s,view,"yy_import");Check(!std::memcmp(&yyView.draft,&b.recipe,sizeof got)&&yyView.dirty&&!yyEdits.applyWhole,"credited code pasted with other text did not import as an unapplied draft");
   {const bool light=yyappearance::light,english=yyappearance::english;const ImVec2 display=io.DisplaySize;yyappearance::light=false;yyappearance::english=false;
    yyView.draft=keepDraft;yyView.dirty=false;yyView.note="已存为一条新记录，名字可以直接改。";std::snprintf(yyView.code,sizeof(yyView.code),"%s",shared.c_str());
    yyView.library={a,b,a};SetName(yyView.library[0],"09-21 22:05");SetName(yyView.library[1],"雨夜 人物");SetName(yyView.library[2],"白天 · 写实");yyView.removeArmed=2;
    io.DisplaySize={1100,1500};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s23-records-cpu.bmp";Frame(s,view);
    yyappearance::light=true;for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s23-records-light-cpu.bmp";Frame(s,view);
    io.DisplaySize=display;yyappearance::light=light;yyappearance::english=english;}
   yyView.draft=keepDraft;yyView.library.clear();yyView.storageOpen=false;yyView.dirty=false;yyView.removeArmed=-1;yyView.code[0]=0;yyView.note="";
  }
  char dialKey[48];std::snprintf(dialKey,sizeof(dialKey),"yy_value_1_%u",unsigned(Intensity));Frame(s,view);Check(items.count(dialKey)!=0,"real intensity slider missing");
  const auto dialBefore=yyView.draft;auto dialAt=items.at(dialKey);io.AddMousePosEvent(dialAt.x,dialAt.y);Frame(s,view);io.AddMouseButtonEvent(0,true);Frame(s,view);io.AddMousePosEvent(dialAt.x+30,dialAt.y);Frame(s,view);io.AddMouseButtonEvent(0,false);Frame(s,view);
  Check(yanyunrecipe::Get(dialBefore,yanyunrecipe::Character,Intensity)!=yanyunrecipe::Get(yyView.draft,yanyunrecipe::Character,Intensity),"slider did not edit real character intensity");
  Check(!yyEdits.applyWhole&&!std::memcmp(dialBefore.values[0],yyView.draft.values[0],sizeof dialBefore.values[0])&&!std::memcmp(dialBefore.values[2],yyView.draft.values[2],sizeof dialBefore.values[2]),"slider crossed banks or bypassed Apply");
  std::snprintf(dialKey,sizeof(dialKey),"number_yy_value_1_%u",unsigned(Intensity));
  // Like the real backend, submit the aggregate modifier as well as its key.
  io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiMod_Ctrl,true);Frame(s,view);dialAt=items.at(dialKey);
  io.AddMousePosEvent(dialAt.x,dialAt.y);Frame(s,view);io.AddMouseButtonEvent(0,true);Frame(s,view);io.AddMouseButtonEvent(0,false);Frame(s,view);
  io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);Frame(s,view);io.AddInputCharactersUTF8("1.37");Frame(s,view);
  io.AddKeyEvent(ImGuiKey_Enter,true);Frame(s,view);io.AddKeyEvent(ImGuiKey_Enter,false);Frame(s,view);
  Check(std::fabs(yanyunrecipe::Get(yyView.draft,yanyunrecipe::Character,Intensity)-1.37f)<.001f,"slider numeric entry failed after releasing Ctrl");
  Check(!yyEdits.applyWhole&&!std::memcmp(dialBefore.values[0],yyView.draft.values[0],sizeof dialBefore.values[0])&&!std::memcmp(dialBefore.values[2],yyView.draft.values[2],sizeof dialBefore.values[2]),"slider numeric entry changed another bank or auto-applied");
  yyView.dirty=true;io.DisplaySize={1100,2200};Click(s,view,"yy_apply_bottom");Check(yyEdits.applyWhole,"bottom Apply did not submit draft");
  yyView.outputW=5120;yyView.outputH=2160;yyView.renderW=5120;yyView.renderH=2160;
  yanyunrecipe::Set(yyView.draft,yanyunrecipe::Character,Work,200);Click(s,view,"yy_apply_bottom");Check(!yyEdits.applyWhole,"bottom Apply bypassed dimension gate");
  yanyunrecipe::Set(yyView.draft,yanyunrecipe::Character,Work,100);
  ImGui::GetStyle().FontSizeBase=18;ImGui::GetStyle().FontScaleMain=4.f/3.f;io.DisplaySize={1140,1170};yyView.detectionOpen=false;yyView.dirty=false;yyappearance::light=false;yyappearance::english=false;
  for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s6-compact-2160.bmp";Frame(s,view);
  ImGui::GetStyle().FontScaleMain=1;io.DisplaySize={855,877.5f};for(int i=0;i<3;++i)Frame(s,view);rasterFile="yanyun-s6-compact-1080.bmp";Frame(s,view);
  for(auto page:{0,5,1,2}){view.page=page;io.DisplaySize={1040,704};for(int i=0;i<3;++i)Frame(s,view);}
  for(bool light:{false,true}){
   yyappearance::light=light;io.DisplaySize={420,182};
   for(int i=0;i<3;++i){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);ImGuiWindow* monitorWindow=nullptr;{studio033::Theme theme;ImGui::Begin("Monitor only",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings);
    studio033::MonitorState m;m.fpsValid=m.loadValid=m.memoryValid=m.temperatureValid=true;m.fps=120;m.load=68;m.temperature=62;m.memoryGiB=9.8;m.budgetGiB=16;studio033::MonitorBody(m);monitorWindow=ImGui::GetCurrentWindow();ImGui::End();}ImGui::Render();if(i==2)panel_cpu_raster(light?"yanyun-s5-monitor-light.bmp":"yanyun-s5-monitor-dark.bmp",monitorWindow);}
  }
  monitor033::PresentationRate rate;rate.Sample(false,0,0,1000);Check(!rate.valid,"absent presentation data fabricated FPS");rate.Sample(true,120,1000,1000);Check(!rate.valid,"one presentation sample fabricated FPS");rate.Sample(true,240,2000,1000);Check(rate.valid&&rate.fps==120,"presentation rate calculation");rate.Sample(true,250,2000,1000);Check(!rate.valid,"stale display timestamp accepted");rate.Sample(true,1,3000,1000);Check(!rate.valid,"counter reset accepted");rate.Sample(false,0,0,0);Check(!rate.valid,"failed DXGI sample kept stale FPS");
  ImGui::DestroyContext();printf("PANEL CPU: %u checks, 0 failures; control bindings/state layouts only, no graphics API\n",checks);return 0;
}catch(const std::exception& e){printf("PANEL CPU FAIL: %s\n",e.what());return 1;}}
