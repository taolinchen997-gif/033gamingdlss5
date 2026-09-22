#pragma once
#include "../../../../src/render_core_policy.h"
#include "../../../../src/nr_controls_abi.h"
namespace Core033 {
inline k033core::Ledger dx12Ledger, vkLedger;
inline k033core::Metadata ReadMetadata(int feature,NVSDK_NGX_Parameter* p){
    k033core::Metadata m;m.feature=feature;if(!p)return m;
    m.haveFlags=p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&m.flags)==NVSDK_NGX_Result_Success;
    p->Get(NVSDK_NGX_Parameter_OutWidth,&m.outputW);p->Get(NVSDK_NGX_Parameter_OutHeight,&m.outputH);return m;
}
bool OfferDx12(ID3D12GraphicsCommandList*,NVSDK_NGX_Parameter*,ID3D12CommandQueue*);
bool Claim(uint32_t owner);
bool UsesHost();
uint32_t Owner();
bool RequestCapture();
bool ConsumeMenuRequest();
bool EmbeddedUiOwned();
const nrcontrolsabi::Api* Controls();
}
