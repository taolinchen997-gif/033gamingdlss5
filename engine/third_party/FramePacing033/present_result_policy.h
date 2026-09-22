#pragma once
#include "failure_policy.h"
namespace pacing033 {
// Surface presentation errors do not prove a failed GPU submission. Keep
// scheduled composition/fence retirement alive so resize and shutdown can drain.
inline bool SurfaceUnavailable(HRESULT hr) {
    return hr==DXGI_STATUS_OCCLUDED || hr==DXGI_STATUS_MODE_CHANGED ||
        hr==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS || hr==DXGI_ERROR_INVALID_CALL ||
        hr==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || hr==DXGI_ERROR_WAS_STILL_DRAWING;
}
inline bool ProbeSurfaceRecovery(HRESULT hr) {
    // INVALID_CALL can require new flip buffers. Other transient surface
    // statuses may recover without a resize (for example occlusion or busy).
    return SurfaceUnavailable(hr) && hr!=DXGI_ERROR_INVALID_CALL;
}
template<class F> HRESULT SurfaceBoundary(Fault& fault,F&& operation) {
    HRESULT surface=S_OK;
    const auto result=Guard(fault,[&] {
        surface=operation();return SurfaceUnavailable(surface)?S_OK:surface;
    });
    return FAILED(result)?result:surface;
}
struct PresentAttempt {HRESULT first=S_OK,result=S_OK;bool retried=false;};
template<class Present,class Healthy> PresentAttempt TryPresent(UINT sync,UINT flags,Present&& present,Healthy&& healthy) {
    PresentAttempt a;a.first=a.result=present(sync,flags);
    // A fullscreen transition can invalidate the previously sampled tearing
    // eligibility. Retry this rejected image once without that optional flag.
    if(a.result==DXGI_ERROR_INVALID_CALL && (flags&DXGI_PRESENT_ALLOW_TEARING) && healthy()==S_OK) {
        a.retried=true;a.result=present(sync,flags&~DXGI_PRESENT_ALLOW_TEARING);
    }
    return a;
}
template<class Count,class Status,class Signal>
void RetirePresentation(HRESULT actual,bool generated,Count& real,Count& interpolated,Count& failed,Status& status,Signal&& signal) {
    if(actual==S_OK){if(generated)++interpolated;else ++real;}
    else if(FAILED(actual))++failed;
    status.store(actual);
    if(!SurfaceUnavailable(actual))Check(actual);
    // This must be a queue Signal after recorded GPU work, never a CPU fence
    // write or a claim that a rejected image was displayed.
    Check(signal());
}
}
