#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <cstdio>
#include <cstdlib>

// CPU objects only: no D3D headers/libraries/device, real event, SDK or hooks.
static int checks=0,failures=0,failStep=0,steps=0,liveObjects=0,objectReleases=0;
static int events=0,names=0,deviceQueries=0,identityQueries=0;
static bool diagnose=false;
static void Check(bool value,const char* name){++checks;if(!value){++failures;printf("FAIL %s\n",name);}}
static bool Fail(){return ++steps==failStep;}
namespace gpufault033 {static bool Requested(){return diagnose;}}
struct Identity final: IUnknown {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        *out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{return --refs;}
};
enum D3D12_COMMAND_LIST_TYPE {D3D12_COMMAND_LIST_TYPE_DIRECT=0};
enum D3D12_FENCE_FLAGS {D3D12_FENCE_FLAG_NONE=0};
struct D3D12_COMMAND_QUEUE_DESC {D3D12_COMMAND_LIST_TYPE Type;};
struct ID3D12Device;
struct ID3D12CommandAllocator;
struct Object: IUnknown {
    ULONG refs=1;ID3D12Device* device;
    explicit Object(ID3D12Device* d);virtual ~Object();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        *out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{++objectReleases;const auto left=--refs;if(!left)delete this;return left;}
    HRESULT SetName(LPCWSTR){++names;return S_OK;}
};
struct __declspec(uuid("3f1c0501-47db-4b40-91a0-0195911d0101")) ID3D12CommandQueue: Object {
    bool failDevice=false;using Object::Object;
    HRESULT GetDevice(REFIID,void**);
};
struct __declspec(uuid("3f1c0501-47db-4b40-91a0-0195911d0102")) ID3D12CommandAllocator: Object{using Object::Object;};
struct __declspec(uuid("3f1c0501-47db-4b40-91a0-0195911d0103")) ID3D12GraphicsCommandList: Object {
    bool closed=false;using Object::Object;
    HRESULT Close(){if(Fail())return E_FAIL;closed=true;return S_OK;}
};
struct __declspec(uuid("3f1c0501-47db-4b40-91a0-0195911d0104")) ID3D12Fence: Object{using Object::Object;};
struct __declspec(uuid("3f1c0501-47db-4b40-91a0-0195911d0105")) ID3D12Device: IUnknown {
    ULONG refs=1;Identity* identity;bool failIdentity=false;
    explicit ID3D12Device(Identity* i):identity(i){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        *out=nullptr;
        if(id==__uuidof(IUnknown)){++identityQueries;if(failIdentity)return E_NOINTERFACE;*out=identity;identity->AddRef();return S_OK;}
        if(id==__uuidof(ID3D12Device)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{return --refs;}
    template<class T>HRESULT Make(REFIID id,void** out){
        *out=nullptr;if(id!=__uuidof(T))return E_NOINTERFACE;if(Fail())return E_OUTOFMEMORY;*out=new T(this);return S_OK;}
    HRESULT CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC*,REFIID id,void** out){return Make<ID3D12CommandQueue>(id,out);}
    HRESULT CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE,REFIID id,void** out){return Make<ID3D12CommandAllocator>(id,out);}
    HRESULT CreateCommandList(UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator* a,void*,REFIID id,void** out){
        if(!a || a->device!=this)return E_INVALIDARG;return Make<ID3D12GraphicsCommandList>(id,out);}
    HRESULT CreateFence(UINT64,D3D12_FENCE_FLAGS,REFIID id,void** out){return Make<ID3D12Fence>(id,out);}
};
Object::Object(ID3D12Device* d):device(d){++liveObjects;device->AddRef();}
Object::~Object(){--liveObjects;device->Release();}
HRESULT ID3D12CommandQueue::GetDevice(REFIID id,void** out){
    ++deviceQueries;*out=nullptr;if(failDevice)return E_FAIL;return device->QueryInterface(id,out);}
static HANDLE FakeCreateEventW(void*,BOOL,BOOL,LPCWSTR){if(Fail())return nullptr;++events;return new int(1);}
static BOOL FakeCloseHandle(HANDLE h){if(!h)return FALSE;--events;delete static_cast<int*>(h);return TRUE;}

namespace hostfixture {
static ID3D12CommandQueue* s_bq=nullptr;
static ID3D12CommandAllocator* s_ba=nullptr;
static ID3D12GraphicsCommandList* s_bl=nullptr;
static ID3D12Fence* s_bf=nullptr;
static HANDLE s_be=nullptr;
#define CreateEventW FakeCreateEventW
#define CloseHandle FakeCloseHandle
#include "nr_builder.inc"
#undef CloseHandle
#undef CreateEventW
static bool Empty(){return !s_bq&&!s_ba&&!s_bl&&!s_bf&&!s_be;}
static bool Ready(){return s_bq&&s_ba&&s_bl&&s_bf&&s_be&&s_bl->closed;}
static void Cleanup(){
    // These fixture objects have never been submitted. Not production recovery.
    if(s_be){FakeCloseHandle(s_be);s_be=nullptr;}
    if(s_bf){s_bf->Release();s_bf=nullptr;}
    if(s_bl){s_bl->Release();s_bl=nullptr;}
    if(s_ba){s_ba->Release();s_ba=nullptr;}
    if(s_bq){s_bq->Release();s_bq=nullptr;}
}
}
int main(){
    using namespace hostfixture;
    Identity identity,otherIdentity;ID3D12Device device(&identity),alias(&identity),other(&otherIdentity);
    for(int step=1;step<=6;++step){
        steps=0;failStep=step;objectReleases=0;
        const bool first=EnsureBuilder(&device);
        Check(!first,"failed preparation is not ready");
        Check(Empty(),"failed preparation publishes no partial objects");
        Check(liveObjects==0&&events==0&&device.refs==1,"failed preparation releases only its unsubmitted objects");
        failStep=0;steps=0;
        Check(EnsureBuilder(&device),"ordinary preparation failure can retry");
        Check(Ready(),"retry produces a complete closed-list builder");
        Cleanup();Check(liveObjects==0&&events==0&&device.refs==1,"fixture cleanup has balanced ownership");
    }
    steps=0;failStep=0;Check(EnsureBuilder(&device)&&Ready(),"healthy preparation succeeds");
    auto* originalQueue=s_bq;auto* originalList=s_bl;const int oldSteps=steps,oldReleases=objectReleases;
    Check(EnsureBuilder(&device),"same device builder is reused");
    Check(EnsureBuilder(&alias),"same canonical device with another interface address is reused");
    Check(!EnsureBuilder(&other),"different device cannot use the old builder");
    Check(!EnsureBuilder(nullptr),"null device cannot use an existing builder");
    Check(s_bq==originalQueue&&s_bl==originalList&&Ready(),"rejected device preserves the existing builder");
    Check(steps==oldSteps&&objectReleases==oldReleases,"ready device checks do not create or retire GPU objects");
    Check(identity.refs==1&&otherIdentity.refs==1&&device.refs==5&&alias.refs==1,"identity checks balance interface references");
    s_bq->failDevice=true;Check(!EnsureBuilder(&device),"failed queue device query rejects reuse");s_bq->failDevice=false;
    alias.failIdentity=true;Check(!EnsureBuilder(&alias),"failed requested identity query rejects reuse");alias.failIdentity=false;
    device.failIdentity=true;Check(!EnsureBuilder(&device),"failed owner identity query rejects reuse");device.failIdentity=false;
    Check(s_bq==originalQueue&&s_bl==originalList&&Ready()&&identity.refs==1&&device.refs==5,"failed identity checks preserve objects and balance acquired references");
    Cleanup();Check(liveObjects==0&&events==0&&device.refs==1,"healthy fixture teardown balances objects");
    // Deliberately incomplete preexisting state: its history is unknown to the
    // function, so rejection must not delete it or allocate a replacement.
    s_bq=new ID3D12CommandQueue(&device);originalQueue=s_bq;
    const int partialSteps=steps,partialReleases=objectReleases;
    Check(!EnsureBuilder(&device),"preexisting partial builder is not ready");
    Check(s_bq==originalQueue&&!s_ba&&!s_bl&&!s_bf&&!s_be,"unknown preexisting state is retained");
    Check(steps==partialSteps&&objectReleases==partialReleases,"unknown state is not destroyed or rebuilt");
    Cleanup();diagnose=true;steps=0;names=0;
    Check(EnsureBuilder(&device)&&Ready()&&names==3,"existing optional names apply to the completed builder");
    Cleanup();Check(liveObjects==0&&events==0&&device.refs==1,"all fixture resources released");
    printf("NR BUILDER CPU: %d checks, %d failures; production EnsureBuilder, fake COM/events only, no GPU/SDK\n",checks,failures);
    return failures?1:0;
}
