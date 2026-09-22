// Offline local file/parameter tests only. The reviewed wrapper fixes cwd to
// this isolated engine's build directory; no user settings, DLL or GPU access.
#ifdef K033_OLD_SR_READER
#include "fixtures/shared-settings-runtime-v1/shared/settings_codec.h"
#else
#include "../runtime/shared/settings_codec.h"
#include "../src/beta2_shared_settings.h"
#include "../src/nr_layer_settings.h"
#include "../src/nr_controls_abi.h"
#endif
#include <cstdio>
#include <cstring>
#include <string>
static int checks=0,failed=0;
static void check(bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL %s\n",label);}}
static constexpr const char* fixture="beta2-sr-settings-compat.ini";
int main(){
#ifdef K033_OLD_SR_READER
 K033_Settings grade{};K033_NrSettings nr{};
 check(k033settings::read_settings(fixture,grade,nr),"unchanged v1 reader accepts SR extension keys");
 check(nr.version==1&&nr.layers==3&&nr.enabled==1,"v1 appearance state preserved");
 check(nr.layer[0].preset==1&&nr.layer[1].preset==2&&nr.layer[2].preset==3,"all layer presets survive v1 read");
 check(grade.exposure==0.25f&&grade.style==2,"grade unchanged for legacy reader");
#else
 struct Consumer {pregrade::Settings pre;int enabled=0,passes=1,hotkey=0,style=0,preset=0,auto_mask=0,ui_correct=0;
  int work=137,passwork=71,passwork3=99;float intensity=0,local_structure=0,local_tone=0,global_tone=0,skin_structure=-1,skin_lift=.35f,sharpen=0.f,natural_look=0.f;nrlayers::Model extra[2];};
 Consumer c;auto nr=k033::nr_defaults(true);auto grade=k033::defaults();
 check(sizeof(nr)==160&&nr.version==6&&nr.reserved==0,"explicit appended v6 output layout");
 check(k033::nr_settings(nr),"legacy zero sentinels admitted");
 k033beta2::ApplyNr(c,nr);
 check(c.work==137&&c.passwork==71&&c.passwork3==71,"legacy SR migration preserves first and copies old extra scale");
 c.passwork3=89;nr=k033beta2::Nr(c);nr.layers=3;
 check(nr.sr_work[0]==137&&nr.sr_work[1]==71&&nr.sr_work[2]==89,"three SR requests are independent");
 Consumer other;k033beta2::ApplyNr(other,nr);
 check(other.work==137&&other.passwork==71&&other.passwork3==89,"three SR requests reach actual carrier mapping");
 // Migrate a real prior beauty file without resetting grade/NR/SR fields.
 const auto beforeGrade=grade;const auto beforeNr=nr;
 check(k033settings::write_settings(fixture,grade,nr),"prepare prior settings");
 {FILE* f=nullptr;fopen_s(&f,fixture,"ab");check(f!=nullptr,"open legacy extension fixture");if(f){std::fputs("033_post_beauty=100\n033_post_beauty=invalid\n",f);std::fclose(f);}}
 K033_Settings migratedGrade{};K033_NrSettings migratedNr{};
 check(k033settings::read_settings(fixture,migratedGrade,migratedNr),"legacy beauty key does not invalidate settings");
 check(migratedNr.reserved==0&&k033::nr_same_appearance(beforeNr,migratedNr),"removed beauty cannot activate or change NR");
 check(!std::memcmp(&beforeGrade,&migratedGrade,sizeof(grade)),"legacy beauty migration preserves grade");
 check(k033settings::write_settings(fixture,migratedGrade,migratedNr),"save migrated settings");
 {FILE* f=nullptr;fopen_s(&f,fixture,"rb");std::string bytes;char line[1024];if(f){while(std::fgets(line,sizeof(line),f))bytes+=line;std::fclose(f);}check(bytes.find("033_post_beauty")==std::string::npos,"removed key must not be persisted");}
 auto retired=nr;retired.reserved=1;check(!k033::nr_settings(retired),"retired binary ABI word must stay zero");
 const auto original=nr;auto changed=nr;changed.sr_work[2]=90;
 check(!k033::nr_same_appearance(original,changed),"SR-only request invalidates shared snapshot identity");
 check(k033::nr_apply(nr,changed)==K033_OK&&nr.appearance_epoch==2,"SR edit advances request epoch");
 nr=original;auto invalid=nr;invalid.version=1;check(!k033::nr_settings(invalid),"old binary ABI cannot overwrite v2 snapshot");
 invalid=nr;invalid.reserved=1;check(!k033::nr_settings(invalid),"reserved storage is deterministic");
 for(unsigned layer=0;layer<3;++layer){invalid=nr;invalid.sr_work[layer]=layer?49:24;check(!k033::nr_settings(invalid),"lower SR bound enforced");
  invalid=nr;invalid.sr_work[layer]=layer?101:201;check(!k033::nr_settings(invalid),"upper SR bound enforced");}
 for(unsigned layer=0;layer<3;++layer)nr.layer[layer].preset=layer+1;
 grade.exposure=0.25f;grade.style=2;
 FILE* file=nullptr;check(!fopen_s(&file,fixture,"wb")&&file,"open isolated fixture");
 if(!file)return 1;std::fputs("033_runtime_version=1\n033_fg_version=1\nfg_native_multiplier=6\nkeep_user_line=unchanged\n",file);std::fclose(file);
 check(k033settings::write_settings(fixture,grade,nr),"write through actual shared transaction codec");
 K033_Settings readGrade{};K033_NrSettings readNr{};
 check(k033settings::read_settings(fixture,readGrade,readNr),"read current shared codec");
 check(k033::nr_same_appearance(nr,readNr),"SR and all three presets roundtrip without aliasing");
 check(readGrade.exposure==grade.exposure&&readGrade.style==grade.style,"grade preserved with NR update");
 std::string bytes;char line[512];fopen_s(&file,fixture,"rb");if(file){while(std::fgets(line,sizeof(line),file))bytes+=line;std::fclose(file);}
 check(bytes.find("fg_native_multiplier=6")!=std::string::npos&&bytes.find("keep_user_line=unchanged")!=std::string::npos,"unowned FG and user fields preserved");
 check(bytes.find("033_nr_version=1")!=std::string::npos,"appearance file version remains rollback-compatible");
 for(int layer=0;layer<3;++layer){const auto id=nrcontrolsabi::LayerId(nrcontrolsabi::Preset,layer);
  check(!nrcontrolsabi::Valid(id,1.01f)&&nrcontrolsabi::Valid(id,float(layer+1)),"actual preset ABI requires an integer");}
#endif
 std::printf("SR settings CPU: checks=%d failed=%d; local fixture only\n",checks,failed);return failed?1:0;
}
