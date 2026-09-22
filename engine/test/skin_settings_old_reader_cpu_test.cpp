// Frozen pre-extension codec. No product or graphics execution.
#include "fixtures/shared-settings-runtime-v1/shared/settings_codec.h"
int main(){
 K033_Settings grade{};K033_NrSettings nr{};
 if(!k033settings::read_settings("skin-settings-roundtrip.ini",grade,nr))return 1;
 if(nr.layers!=3||nr.layer[1].style!=1||nr.layer[2].style!=2||grade.exposure!=.25f)return 2;
 grade.exposure=.5f;
 if(!k033settings::write_settings("skin-settings-roundtrip.ini",grade,nr))return 3;
 std::puts("Frozen settings reader/writer: 3 checks passed; skin extension must be preserved");return 0;
}
