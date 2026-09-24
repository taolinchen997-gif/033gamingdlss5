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
// S32 (033 panel 超分模型): the DLSS SR creation reports what it asked NGX for and
// which snippet it got; the evaluate thread consumes a requested recreation.
void RecordSrCreation(uint32_t appliedPreset,bool external);
void RecordSrVersion(uint32_t major,uint32_t minor,uint32_t patch);
bool ConsumeSrRecreate();
// S33: every render-size question the game asks (NGX optimal settings) and our answer.
// Read the sequence before computing the answer, record it with the answer.
uint32_t SrRatioSequence();
void RecordSrQuery(uint32_t outputW,uint32_t outputH,uint32_t renderW,uint32_t renderH,uint32_t gameMode,uint32_t ratioSequence);
}
