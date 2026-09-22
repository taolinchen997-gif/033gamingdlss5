// CPU and private local files only. Never load a product DLL or real settings.
#include "../src/nr_controls_abi.h"
#include "../src/nr_layer_settings.h"
#include "../src/beta2_shared_settings.h"
#include "../runtime/shared/settings_codec.h"
#include "../runtime/shared/beta2_fg_codec.h"
#if __has_include("../runtime/shared/profile_preferences_store.h")
#include "../runtime/shared/profile_preferences_store.h"
#define K033_SKIN_PROFILE_FIXTURE 1
#endif
#include <string>
#include <limits>
namespace {
int checks=0,failed=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
struct Consumer {
 pregrade::Settings pre;int enabled=1,passes=3,hotkey=0,style=0,preset=1,auto_mask=1,ui_correct=0;
 int work=70,passwork=70,passwork3=70;
 float intensity=1,local_structure=1,local_tone=1,global_tone=1,skin_structure=-1,skin_lift=.35f,sharpen=0.f,natural_look=0.f;
 nrlayers::Model extra[2];
};
std::string Read(const char* p){FILE* f=nullptr;if(fopen_s(&f,p,"rb")||!f)return {};std::string s;char line[1024];while(std::fgets(line,sizeof(line),f))s+=line;std::fclose(f);return s;}
}
int main(){
 static_assert(sizeof(nrcontrolsabi::FrozenSnapshot)==872,"shipped Feeder layout must stay frozen");
 const char* path="skin-settings-roundtrip.ini";
 Consumer source;source.skin_structure=.812345f;source.extra[0].skin=-1;source.extra[1].skin=1.765432f;source.skin_lift=.712345f;source.sharpen=.612345f;source.natural_look=.4375f;
 source.extra[0].style=1;source.extra[1].style=2;
 auto nr=k033beta2::Nr(source);auto grade=k033::defaults();grade.exposure=.25f;
 auto fg=k033beta2::fg_defaults();fg.native_multiplier=6;
 Check(k033settings::write_settings(path,grade,nr)&&k033settings::write_fg(path,fg),"save skin alongside existing NR grade and native 6x");
 K033_Settings loadedGrade{};K033_NrSettings loadedNr{};K033_Beta2FgSettings loadedFg{};
 Check(k033settings::read_settings(path,loadedGrade,loadedNr)&&k033settings::read_fg(path,loadedFg),"reopen persisted groups");
 Consumer restored;k033beta2::ApplyNr(restored,loadedNr);
 Check(restored.skin_structure==source.skin_structure,"first-layer skin survives restart exactly");
 Check(restored.extra[0].skin==source.extra[0].skin&&restored.extra[1].skin==source.extra[1].skin,"inactive layers retain distinct skin values and -1 sentinel");
 Check(restored.natural_look==source.natural_look,"natural grade survives exact common settings save/reopen");
 Check(restored.sharpen==source.sharpen,"clarity survives the shared save/reopen path exactly");
 Check(restored.skin_lift==source.skin_lift,"skin brightening survives restart exactly");
 Check(loadedFg.native_multiplier==6&&loadedGrade.exposure==grade.exposure,"NR save preserves grade and native 6x");
 for(unsigned count=1;count<=3;++count){auto inactive=source;inactive.passes=int(count);
  Check(k033settings::write_nr(path,k033beta2::Nr(inactive))&&k033settings::read_settings(path,loadedGrade,loadedNr),"save active and inactive layers together");
  Consumer reloaded;k033beta2::ApplyNr(reloaded,loadedNr);
  Check(reloaded.passes==int(count)&&reloaded.extra[0].skin==-1&&reloaded.extra[1].skin==source.extra[1].skin,"disabled layer skin survives later re-enable");}
 auto natural=source;natural.natural_look=.9f;
 Check(!k033::nr_same_appearance(nr,k033beta2::Nr(natural)),"natural grade edit reaches persistence mailbox");
 Check(nrlayers::Signature(source)==nrlayers::Signature(natural),"natural output grade never rebuilds the NR model");
 for(float bad:{-1.f,1.01f,std::numeric_limits<float>::quiet_NaN()}){natural.natural_look=bad;const auto before=Read(path);
  Check(!k033settings::write_nr(path,k033beta2::Nr(natural))&&Read(path)==before,"invalid natural grade preserves prior saved bytes");}
 // Actual production reply writer and guarded legacy buffers. A prefix memcpy
 // would corrupt these layouts because values/modelValues have different offsets.
 nrcontrolsabi::Snapshot current;current.frames=123456;current.activeLayers=3;current.values[62]=.75f;
 for(unsigned count:{62u,63u,64u}){
  alignas(8) unsigned char buffer[sizeof(current)+16];std::memset(buffer,0xA5,sizeof(buffer));
  unsigned bytes=count==62?sizeof(nrcontrolsabi::SnapshotOf<62>):count==63?sizeof(nrcontrolsabi::SnapshotOf<63>):sizeof(current);
  Check(nrcontrolsabi::Reply(buffer,bytes,12,count,current),"production snapshot writer accepts the exact legacy layout");
  bool tail=true;for(unsigned i=bytes;i<sizeof(buffer);++i)tail&=buffer[i]==0xA5;
  Check(tail,"legacy snapshot reply never overwrites caller buffer");
  if(count==62){nrcontrolsabi::SnapshotOf<62> result;std::memcpy(&result,buffer,sizeof(result));Check(result.frames==123456&&result.activeLayers==3,"frozen Feeder fields keep legacy offsets");}
  if(count==63){nrcontrolsabi::SnapshotOf<63> result;std::memcpy(&result,buffer,sizeof(result));Check(result.frames==123456&&result.values[62]==.75f,"63-control panel retains appended skin slot");}
  Check(!nrcontrolsabi::Reply(buffer,bytes-1,12,count,current),"truncated snapshot header rejected");
 }
 auto sharp=source;sharp.sharpen=.9f;
 Check(!k033::nr_same_appearance(nr,k033beta2::Nr(sharp)),"clarity edit reaches the common save mailbox");
 Check(nrlayers::Signature(source)==nrlayers::Signature(sharp),"output clarity never changes model creation signature");
 for(float bad:{-1.f,1.01f,std::numeric_limits<float>::quiet_NaN()}){sharp.sharpen=bad;const auto before=Read(path);
  Check(!k033settings::write_nr(path,k033beta2::Nr(sharp))&&Read(path)==before,"invalid clarity cannot overwrite saved settings");}
 auto changed=source;changed.skin_lift=.2f;
 Check(!k033::nr_same_appearance(nr,k033beta2::Nr(changed)),"skin-only change reaches common save mailbox");
 for(unsigned i=0;i<3;++i){changed=source;if(i)changed.extra[i-1].skin=.25f;else changed.skin_structure=.25f;
  Check(!k033::nr_same_appearance(nr,k033beta2::Nr(changed)),"each layer skin edit advances saved identity");}
 // A real legacy file has no extension keys. Keep existing factory values.
 FILE* f=nullptr;fopen_s(&f,"skin-settings-legacy.ini","wb");if(f){std::fputs("033_runtime_version=1\n033_nr_version=1\nnr_enabled=1\nnr_layers=2\nnr_layer1_local_structure=1.75\n",f);std::fclose(f);}
 Check(k033settings::read_settings("skin-settings-legacy.ini",loadedGrade,loadedNr),"old file still loads");
 restored=Consumer{};k033beta2::ApplyNr(restored,loadedNr);
 Check(restored.skin_structure==-1&&restored.extra[0].skin==-1&&restored.extra[1].skin==-1&&restored.skin_lift==.35f,"absent skin keys preserve historical defaults");
 // Invalid input is rejected before writing, with the prior file intact.
 for(float bad:{-1.01f,2.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
  changed=source;changed.skin_structure=bad;const auto before=Read(path);
  Check(!k033settings::write_nr(path,k033beta2::Nr(changed))&&Read(path)==before,"invalid skin refuses transaction without losing settings");}
 for(float bad:{-.01f,1.01f,std::numeric_limits<float>::quiet_NaN()}){
  changed=source;changed.skin_lift=bad;const auto before=Read(path);
  Check(!k033settings::write_nr(path,k033beta2::Nr(changed))&&Read(path)==before,"invalid brightening refuses transaction");}
#ifdef K033_SKIN_PROFILE_FIXTURE
 // Exercise the full production profile transaction, including its owned-key
 // buffer with all three groups present. All files are in this fixture's cwd.
 const std::wstring profile=L"skin-profile-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".ini";
 Check(k033settings::CreateProfileSettings(profile,grade,nr,fg),"create complete private per-game profile");
 auto profileNr=nr;profileNr.skin_lift=.25f;profileNr.skin_structure[2]=.5f;
 grade.exposure=.75f;
 Check(k033settings::WriteProfileGroups(profile,&grade,&profileNr,&fg),"all-group profile transaction fits every owned key");
 DWORD error=0;
 Check(k033settings::ReadProfileSettings(profile,loadedGrade,loadedNr,loadedFg,error)==K033_OK&&k033::nr_same_appearance(profileNr,loadedNr)&&loadedFg.native_multiplier==6&&loadedGrade.exposure==.75f,"per-game writer reopens exact skin values and other groups");
 grade.exposure=.25f;
#endif
 // Leave this file for a separately compiled, unchanged legacy codec process.
 Check(k033settings::write_settings(path,grade,nr),"prepare rollback compatibility fixture");
 std::printf("Skin persistence CPU: %d checks, %d failures; private files only\n",checks,failed);return failed?1:0;
}
