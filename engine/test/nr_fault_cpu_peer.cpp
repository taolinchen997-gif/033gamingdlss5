#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mutex>
#include <unordered_map>
#include "../src/nr_fault.h"
#include "../src/render_core_abi.h"
#include "../src/render_core_policy.h"
// Same /EHsc mode as Core033.cpp, linked against the /EHa renderer test TU.
#include "fault_invoke.inc"
int TestInvoke(k033core::AfterUpscale cb,const k033core::Frame* frame){return Invoke(cb,frame);}
const void* FaultStateAddress(){return &nrfault033::first;}
namespace claimfixture {
static uint32_t Provider(){return 0;}
static struct{bool claim(uint32_t){return true;}}ownership;
#include "fault_claim.inc"
}
bool TestClaim(uint32_t owner){return claimfixture::Claim(owner);}
namespace capfixture {
struct Device{void AddRef(){}};
struct Parameter{};
using ID3D12Device=Device;using NVSDK_NGX_Parameter=Parameter;
constexpr int NVSDK_NGX_Result_Success=1;
static struct{uint32_t get(){return k033core::Host033;}} ownership;
static int calls=0;static bool throws=false,fails=false;static Parameter parameter;
namespace NVNGXProxy {
static bool InitDx12(Device*){++calls;if(throws)RaiseException(0xc0000005,0,0,nullptr);return !fails;}
static int Get(Parameter** p){*p=&parameter;return 1;}
static auto D3D12_GetCapabilityParameters(){return &Get;}
}
#include "fault_capabilities.inc"
static void* Probe(Device* device,bool* escaped){
    __try{return ApiCapabilities(device);}__except(EXCEPTION_EXECUTE_HANDLER){*escaped=true;return nullptr;}
}
}
int TestCapabilities(bool fault){
    capfixture::Device device;bool escaped=false;capfixture::throws=fault;capfixture::fails=!fault;
    void* first=capfixture::Probe(&device,&escaped);int errors=0;
    if(fault){
        if(first||escaped||!nrfault033::Blocked())++errors;
        // An old /EHsc implementation may have stranded its mutex. Never enter
        // it again unless the production gate latched; report the failure instead.
        if(nrfault033::Blocked())capfixture::Probe(&device,&escaped);
        if(capfixture::calls!=1)++errors;
    }else{
        capfixture::fails=false;void* second=capfixture::Probe(&device,&escaped);
        if(first||!second||escaped||nrfault033::Blocked()||capfixture::calls!=2)++errors;
    }
    return errors;
}
