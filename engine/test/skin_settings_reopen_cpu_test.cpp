#include "../src/nr_controls_abi.h"
#include "../src/nr_layer_settings.h"
#include "../src/beta2_shared_settings.h"
#include "../runtime/shared/settings_codec.h"
#include "../runtime/shared/beta2_fg_codec.h"
int main(){
 struct Consumer {int enabled=0,passes=0,hotkey=0,style=0,preset=0,auto_mask=0,ui_correct=0,work=70,passwork=70,passwork3=70;
  float intensity=0,local_structure=0,local_tone=0,global_tone=0,skin_structure=-1,skin_lift=.35f,sharpen=0.f,natural_look=0.f;nrlayers::Model extra[2];};
 K033_Settings grade{};K033_NrSettings nr{};K033_Beta2FgSettings fg{};
 if(!k033settings::read_settings("skin-settings-roundtrip.ini",grade,nr)||!k033settings::read_fg("skin-settings-roundtrip.ini",fg))return 1;
 Consumer c;k033beta2::ApplyNr(c,nr);
 if(c.skin_structure!=.812345f||c.extra[0].skin!=-1||c.extra[1].skin!=1.765432f||c.skin_lift!=.712345f)return 2;
 if(c.natural_look!=.4375f)return 5;
 if(c.sharpen!=.612345f)return 4;
 if(grade.exposure!=.5f||fg.native_multiplier!=6||c.passes!=3)return 3;
 std::puts("Settings after legacy writer: 3 checks passed; all skin values, grade edit and native 6x retained");return 0;
}
