#define K033_ENABLE_RETIRED_UNIVERSAL_FG 1 // CPU-only historical provider tests
#include <cstdio>
#include <cmath>
#include <d3dcompiler.h>
#include "../src/universal_fg_policy.h"
#include "../src/framegen_policy.h"
#include "../src/universal_fg_images.h"
#include "../third_party/FramePacing033/failure_policy.h"
static int checks=0,failed=0;
static void check(bool ok,const char* message){++checks;if(!ok){++failed;std::printf("FAIL: %s\n",message);}}
#include "fg_route_cpu_cases.h"
#include "device_identity_cpu_cases.h"
#include "nr_input_cpu_cases.h"
#include "nr_runtime_state_cpu_cases.h"
#include "nr_create_cpu_cases.h"
#include "nr_resize_cpu_cases.h"
#include "gpu_fault_cpu_cases.h"
#include "gputime_disabled_cpu_cases.h"
#include "framegen_refinement_cpu_cases.h"
#include "nr_zero_upload_cpu_cases.h"
#include "present_result_cpu_cases.h"
int main(){
 routeChecks();
 deviceIdentityChecks();
 nrInputChecks();
 nrRuntimeStateChecks();
 nrCreateChecks();
 nrResizeChecks();
 gpuFaultChecks();disabledTimingChecks();
 framegenRefinementChecks();
 nrZeroUploadChecks();
 presentResultChecks();
 // Inject failures through the same reset/submission/guard code used by the
 // presenter. These checks must not call a graphics or driver API.
 struct Allocator {HRESULT result=S_OK;int calls=0;HRESULT Reset(){++calls;return result;}};
 struct List {HRESULT result=S_OK;int resets=0;HRESULT Reset(Allocator*,void*){++resets;return result;}};
 for(int failure=0;failure<3;++failure){Allocator allocator;List list;pacing033::Fault fault;
   if(failure==1)allocator.result=E_FAIL;if(failure==2)list.result=E_FAIL;
   auto hr=pacing033::Guard(fault,[&]{pacing033::Reset(&allocator,&list);return S_OK;});
   check((failure==0)==SUCCEEDED(hr),"reset failure propagated");
   check(list.resets==(failure==1?0:1),"failed allocator cannot reset command list");
 }
 for(int failure=0;failure<3;++failure){pacing033::Fault fault;int close=0,execute=0,signal=0;
   auto hr=pacing033::Guard(fault,[&]{pacing033::Submit(true,[&]{++close;return failure==1?E_FAIL:S_OK;},
       [&]{++execute;},[&]{++signal;return failure==2?E_FAIL:S_OK;});return S_OK;});
   check((failure==0)==SUCCEEDED(hr),"submission fault reaches owner");
   check(close==1&&execute==(failure==1?0:1)&&signal==(failure==1?0:1),"Close failure forbids execution and retirement signal");
   bool reused=false;if(failure)pacing033::Guard(fault,[&]{reused=true;return S_OK;});check(!reused,"fault blocks later resource reuse");
 }
 struct Fence{uint64_t done=0;uint64_t GetCompletedValue(){return done;}} fence;
 check(!pacing033::Completed(&fence,1),"incomplete fence cannot retire");fence.done=7;check(pacing033::Completed(&fence,7),"actual completed fence retires");
 fence.done=UINT64_MAX;pacing033::Fault removed;auto lost=pacing033::Guard(removed,[&]{pacing033::Completed(&fence,7);return S_OK;});
 check(lost==DXGI_ERROR_DEVICE_REMOVED,"device removal sentinel is failure, not completion");
 CRITICAL_SECTION cs;InitializeCriticalSection(&cs);pacing033::Fault locked;
 auto lockedFailure=pacing033::Guard(locked,[&]{pacing033::Enter(&cs);pacing033::Enter(&cs);pacing033::Check(E_FAIL);return S_OK;});
 check(FAILED(lockedFailure)&&pacing033::lockCount==0,"recursive critical sections unwound on failure");DeleteCriticalSection(&cs);
 HANDLE event=CreateEventW(nullptr,FALSE,TRUE,nullptr);pacing033::Fault cancelled;cancelled.error=E_FAIL;
 auto cancelledWait=pacing033::Guard(cancelled,[&]{pacing033::Wait(event,INFINITE);return S_OK;});
 check(cancelledWait==E_FAIL,"fault exits even an infinite worker wait");CloseHandle(event);
 using namespace ufgpolicy033;
 for(unsigned multiplier=2;multiplier<=3;++multiplier){const unsigned n=Count(multiplier,2);
   check(n==multiplier-1,"actual output count");check(Count(multiplier,n-1)==0,"no partial 3x with a wrong time phase");
   double before=0;for(unsigned i=0;i<n;++i){const auto phase=Phase(i,n);check(phase>before&&phase<1,"chronological true intermediates");before=phase;}
   check(std::abs(PresentInterval(33.333333,multiplier)*multiplier-33.333333)<1e-9,"frame interval preserved");
 }
 // A 2x -> 3x -> 2x live multiplier change must always use the phases
 // for the actually requested batch. A one-output provider cannot substitute
 // a midpoint for one of the two thirds of a 3x request.
 const unsigned ratios[]={2,3,2};
 const unsigned expectedOutputs[2][4]={{0,1,1,1},{0,0,2,2}};
 for(unsigned pair=0;pair<3;++pair){
   for(unsigned capacity=0;capacity<4;++capacity){
     const auto count=Count(ratios[pair],capacity);
     check(count==expectedOutputs[ratios[pair]-2][capacity],"live ratio honors whole-batch provider capacity");
     if(count==1)check(std::abs(Phase(0,count)-0.5f)<1e-6f,"2x uses midpoint");
     if(count==2){
       check(std::abs(Phase(0,count)-1.f/3.f)<1e-6f,"3x first output uses one third");
       check(std::abs(Phase(1,count)-2.f/3.f)<1e-6f,"3x second output uses two thirds");
     }
   }
   const auto interval=PresentInterval(30.0,ratios[pair]);
   check(interval==double(ratios[pair]==2?15:10),"live ratio leaves real-frame duration unchanged");
 }
 // Each pair's two resources retire separately; the following pair cannot use
 // either before both old outputs have been composed, including 2x/3x toggles.
 for(uint64_t frame=0;frame<1000;++frame){auto base=OutputBase(frame);check(base==0||base==2,"two double-buffered batches");
   check(base+1<4,"second intermediate is in bounds");check(base!=OutputBase(frame+1),"next pair cannot alias live pair");}
 framegen033::Pool pool;auto first=pool.Acquire(0),second=pool.Acquire(0),third=pool.Acquire(0);
 check(first&&second&&third,"one immutable ticket per pair");check(!pool.Acquire(0),"no descriptor overwrite while pending");
 check(pool.Submit(first,7),"pair submitted once");check(!pool.Acquire(6),"cannot retire before GPU fence");
 check(bool(pool.Acquire(7)),"both output passes retire at shared actual signal");
 const char* names[]={"downsample","estimate","interpolate","capture","mask","resolve","scene"};
 for(int i=0;i<7;++i){const char* code=i<3?kFramegenFlowShader:ufg033::colourShader;ID3DBlob *b=nullptr,*error=nullptr;
   auto hr=D3DCompile(code,std::strlen(code),names[i],nullptr,nullptr,names[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&error);
   if(FAILED(hr)&&error)std::printf("%.*s\n",int(error->GetBufferSize()),static_cast<char*>(error->GetBufferPointer()));
   check(SUCCEEDED(hr),names[i]);if(b)b->Release();if(error)error->Release();}
 std::printf("UNIVERSAL CPU: %d checks, %d failures; shaders compiled, no GPU/provider execution\n",checks,failed);
 return failed?1:0;
}
