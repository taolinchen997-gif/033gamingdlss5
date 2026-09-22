#include "../third_party/FramePacing033/present_result_policy.h"
static void presentResultChecks(){
 const HRESULT surfaces[]={DXGI_STATUS_OCCLUDED,DXGI_STATUS_MODE_CHANGED,DXGI_STATUS_MODE_CHANGE_IN_PROGRESS,
     DXGI_ERROR_INVALID_CALL,DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,DXGI_ERROR_WAS_STILL_DRAWING};
 for(auto hr:surfaces)for(bool generated:{false,true}){
  check(pacing033::ProbeSurfaceRecovery(hr)==(hr!=DXGI_ERROR_INVALID_CALL),"transient busy/occlusion may recover without buffer recreation");
  pacing033::Fault fault;std::atomic<uint64_t> real{0},interpolated{0},failedCount{0};std::atomic<HRESULT> status{0};int signals=0;
  auto result=pacing033::SurfaceBoundary(fault,[&]{pacing033::RetirePresentation(hr,generated,real,interpolated,failedCount,status,[&]{++signals;return S_OK;});return hr;});
  check(result==hr,"surface HRESULT retained for caller");check(fault.error==0,"surface error must not poison scheduler");
  check(signals==1,"rejected presentation still signals submitted GPU retirement");check(real==0&&interpolated==0,"unshown frames not counted displayed");
  check(failedCount==(FAILED(hr)?1:0),"presentation failures counted independently");
  bool drained=false;check(pacing033::Guard(fault,[&]{drained=true;return S_OK;})==S_OK&&drained,"surface failure permits shutdown drain");
 }
 for(auto hr:{DXGI_ERROR_DEVICE_REMOVED,DXGI_ERROR_DEVICE_RESET,DXGI_ERROR_DEVICE_HUNG,E_OUTOFMEMORY,E_FAIL}){
  pacing033::Fault fault;std::atomic<uint64_t> r{0},g{0},f{0};std::atomic<HRESULT> status{0};int signals=0;
  const auto result=pacing033::SurfaceBoundary(fault,[&]{pacing033::RetirePresentation(hr,false,r,g,f,status,[&]{++signals;return S_OK;});return hr;});
  check(result==hr&&fault.error==hr,"real device/submission failure stays terminal");check(signals==0,"lost device not falsely retired");
 }
 for(auto actual:{S_OK,DXGI_ERROR_INVALID_CALL}){
  pacing033::Fault fault;std::atomic<uint64_t> r{0},g{0},f{0};std::atomic<HRESULT> status{0};
  auto result=pacing033::SurfaceBoundary(fault,[&]{pacing033::RetirePresentation(actual,false,r,g,f,status,[]{return E_FAIL;});return actual;});
  check(result==E_FAIL&&fault.error==E_FAIL,"retirement Signal failure cannot be downgraded");
 }
 for(UINT flags:{0u,UINT(DXGI_PRESENT_ALLOW_TEARING),UINT(DXGI_PRESENT_ALLOW_TEARING|DXGI_PRESENT_DO_NOT_WAIT)})
 for(auto first:{S_OK,DXGI_ERROR_INVALID_CALL,DXGI_ERROR_DEVICE_REMOVED})for(auto device:{S_OK,DXGI_ERROR_DEVICE_REMOVED}){
  int calls=0;UINT retryFlags=0;auto a=pacing033::TryPresent(0,flags,[&](UINT,UINT f){retryFlags=f;return ++calls==1?first:S_OK;},[&]{return device;});
  const bool retry=first==DXGI_ERROR_INVALID_CALL&&(flags&DXGI_PRESENT_ALLOW_TEARING)&&device==S_OK;
  check(calls==(retry?2:1)&&a.retried==retry,"tearing retry narrowly bounded");
  check(a.result==(retry?S_OK:first),"unrelated error not hidden");
  if(retry)check(retryFlags==(flags&~DXGI_PRESENT_ALLOW_TEARING),"retry preserves remaining flags");
 }
 pacing033::Fault failedRetry;int calls=0;
 auto retry=pacing033::TryPresent(0,DXGI_PRESENT_ALLOW_TEARING,[&](UINT,UINT){++calls;return DXGI_ERROR_INVALID_CALL;},[]{return S_OK;});
 check(calls==2&&retry.result==DXGI_ERROR_INVALID_CALL,"failed retry remains visible and finite");
 // Reproduce the former exit failure: throwing before the queue Signal leaves
 // retirement unsignaled and latches a permanent scheduler fault.
 pacing033::Fault old;int oldSignal=0;
 pacing033::Guard(old,[&]{pacing033::Check(DXGI_ERROR_INVALID_CALL);++oldSignal;return S_OK;});
 check(oldSignal==0&&old.error==DXGI_ERROR_INVALID_CALL,"old missing-retirement defect reproduced");
}
