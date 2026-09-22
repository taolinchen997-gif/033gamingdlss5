#pragma once
#include <d3d12.h>
#include <atomic>
#include <memory>
#include <new>
#include <mutex>

// Private interface ownership observes native list destruction even when it
// bypasses ReShade's destroy callback. Never holds a reference to the list.
// The destructor only publishes a flag: GPU/resource release stays in Pump.
namespace commandlife {
inline constexpr GUID Key{0x691c4455,0x72e6,0x4d9b,{0xa8,0x82,0xbc,0x36,0xa1,0x19,0xa7,0x04}};
struct State;
struct AllocatorUse {
    std::shared_ptr<State> state;
    uint64_t serial=0;
    bool Invalidated() const;
};
struct State {
    std::atomic_bool discarded{false};
    std::atomic<uint64_t> resetSerial{0};
    std::mutex association;
    AllocatorUse allocator;
};
inline bool AllocatorUse::Invalidated() const {
    return state && (state->discarded.load(std::memory_order_acquire) ||
        state->resetSerial.load(std::memory_order_acquire)!=serial);
}
inline void BindAllocator(const std::shared_ptr<State>& list,const std::shared_ptr<State>& allocator) {
    if(!list)return;
    std::lock_guard<std::mutex> lock(list->association);
    list->allocator={allocator,allocator?allocator->resetSerial.load(std::memory_order_acquire):0};
}
inline AllocatorUse AllocatorFor(const std::shared_ptr<State>& list) {
    if(!list)return {};
    std::lock_guard<std::mutex> lock(list->association);return list->allocator;
}
inline void AllocatorReset(const std::shared_ptr<State>& allocator,HRESULT result) {
    if(allocator && SUCCEEDED(result))allocator->resetSerial.fetch_add(1,std::memory_order_release);
}
class Watch final : public IUnknown {
    std::atomic<ULONG> refs{1};
public:
    std::shared_ptr<State> state;
    explicit Watch(std::shared_ptr<State> value):state(std::move(value)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown) && id!=Key)return E_NOINTERFACE;
        *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {
        auto count=--refs;if(!count){state->discarded.store(true,std::memory_order_release);delete this;}return count;
    }
};
inline std::shared_ptr<State> Existing(ID3D12Object* object,bool* occupied=nullptr) {
    if(occupied)*occupied=false;
    if(!object)return {};
    IUnknown* existing=nullptr;UINT bytes=sizeof(existing);
    if(FAILED(object->GetPrivateData(Key,&bytes,&existing)) || !existing)return {};
    if(occupied)*occupied=true;
    IUnknown* owned=nullptr;const auto result=existing->QueryInterface(Key,reinterpret_cast<void**>(&owned));existing->Release();
    if(FAILED(result)||!owned)return {};
    auto state=static_cast<Watch*>(owned)->state;owned->Release();return state;
}
inline std::shared_ptr<State> Observe(ID3D12Object* object) {
    if(!object)return {};
    // Only first private-interface installation is serialized. Several lists
    // may share an allocator and be created on different game threads. Without
    // a second lookup under this lock, replacing a competing Watch would mark
    // still-live storage discarded. Existing observations stay lock-free here.
    static std::mutex installation;
    static thread_local bool observing=false;
    if(observing)return {}; // A foreign COM callback must not recursively install.
    struct ObservationScope {bool& active;explicit ObservationScope(bool& v):active(v){active=true;}~ObservationScope(){active=false;}} scope(observing);
    // Watch vtables must remain executable while a game retains its list.
    static const bool pinned=[](){HMODULE module=nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Observe),&module)!=FALSE;}();
    if(!pinned)return {};
    bool occupied=false;auto existing=Existing(object,&occupied);
    if(existing || occupied)return existing; // Never replace another owner's key.
    std::lock_guard<std::mutex> lock(installation);
    existing=Existing(object,&occupied);
    if(existing || occupied)return existing;
    auto state=std::make_shared<State>();auto* watch=new(std::nothrow) Watch(state);
    if(!watch)return {};
    struct InitialReference {Watch* value;~InitialReference(){value->Release();}} initial{watch};
    const auto result=object->SetPrivateDataInterface(Key,watch);
    return SUCCEEDED(result)?state:std::shared_ptr<State>{};
}
// The caller supplies only a proven successful CreateCommandList/Reset pair.
// Store lifetime tokens, never retain the native list or allocator COM object.
// Failure clears a prior association rather than lending it to new commands.
inline bool BindObservedAllocator(ID3D12Object* list,ID3D12Object* allocator) {
    auto life=Observe(list);if(!life)return false;
    BindAllocator(life,{}); // Also remain unknown if observation throws.
    auto storage=Observe(allocator);BindAllocator(life,storage);
    return storage && !storage->discarded.load(std::memory_order_acquire);
}
inline bool MayRetire(bool reset,bool destroyed,bool recording,unsigned pending,bool pinned,bool observed,bool complete) {
    return (reset||destroyed)&&!recording&&!pending&&!pinned&&observed&&complete;
}
}
