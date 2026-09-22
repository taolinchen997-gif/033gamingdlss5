#pragma once
#include <Windows.h>
#include <dxgi.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <new>
// Private scheduler failure boundary. No failed wait/reset/signal grants a
// resource lease. Threads leave on fault; uncertain GPU allocations are kept.
namespace pacing033 {
struct Failure {HRESULT code;};
struct Fault {std::atomic<int32_t> error{0};};
inline thread_local Fault* currentFault=nullptr;
inline thread_local std::array<CRITICAL_SECTION*,32> locks{};
inline thread_local size_t lockCount=0;
inline void Check(HRESULT hr){if(FAILED(hr))throw Failure{hr};}
inline void CheckFault(){if(currentFault)Check(currentFault->error.load());}
inline void Enter(CRITICAL_SECTION* cs){CheckFault();if(lockCount==locks.size())throw Failure{E_UNEXPECTED};
    // Bounded lock acquisition also exits when another scheduler thread fails.
    const auto start=GetTickCount64();while(!TryEnterCriticalSection(cs)){CheckFault();
        if(GetTickCount64()-start>5000)throw Failure{DXGI_ERROR_WAIT_TIMEOUT};Sleep(1);}
    locks[lockCount++]=cs;}
inline void Leave(CRITICAL_SECTION* cs){
    if(!lockCount||locks[lockCount-1]!=cs)throw Failure{E_UNEXPECTED};--lockCount;LeaveCriticalSection(cs);}
template<class F> HRESULT Guard(Fault& fault,F&& fn){
    // Internal virtual calls stay inside their outer failure boundary.
    if(currentFault==&fault){CheckFault();return fn();}
    Fault* previous=currentFault;currentFault=&fault;const size_t mark=lockCount;HRESULT result=S_OK;
    try{CheckFault();result=fn();}catch(const Failure& e){result=e.code;}
    catch(const std::bad_alloc&){result=E_OUTOFMEMORY;}catch(...){result=E_FAIL;}
    if(FAILED(result)){int32_t zero=0;fault.error.compare_exchange_strong(zero,result);}
    while(lockCount>mark)LeaveCriticalSection(locks[--lockCount]);
    currentFault=previous;return result;
}
inline DWORD Wait(HANDLE event,DWORD timeout){
    if(!event)throw Failure{E_HANDLE};const auto start=GetTickCount64();
    for(;;){CheckFault();auto r=WaitForSingleObject(event,10);
        if(r==WAIT_OBJECT_0)return r;if(r==WAIT_FAILED)throw Failure{HRESULT_FROM_WIN32(GetLastError())};
        if(timeout!=INFINITE&&GetTickCount64()-start>=timeout)throw Failure{DXGI_ERROR_WAIT_TIMEOUT};}
}
template<class Fence> bool Completed(Fence* fence,uint64_t value){
    if(!fence)return value==0;const auto completed=fence->GetCompletedValue();
    if(completed==UINT64_MAX)throw Failure{DXGI_ERROR_DEVICE_REMOVED};return completed>=value;
}
template<class Allocator,class List> List* Reset(Allocator* allocator,List* list){
    if(!allocator||!list)throw Failure{E_POINTER};Check(allocator->Reset());Check(list->Reset(allocator,nullptr));return list;
}
template<class Close,class Execute,class Signal> void Submit(bool open,Close&& close,Execute&& execute,Signal&& signal){
    if(open)Check(close());execute();Check(signal());
}
}
