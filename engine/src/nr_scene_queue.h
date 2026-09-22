#pragma once
#include <dxgi.h>
#include <wrl/client.h>
#include <utility>
namespace nrgame033 {
// The first GUID/version is the existing explicit-runtime owner contract.
inline constexpr GUID SceneOwnerGuid={0x39480332,0xc11e,0x4e8c,{0x93,0xf3,0x01,0x6b,0x8e,0x37,0x12,0x23}};
inline constexpr UINT SceneOwnerVersion=0x03320001u;
// Version 1: an owning IUnknown for this swapchain's actual game command queue.
inline constexpr GUID SceneGameQueueGuid={0x5b640338,0xac72,0x4cea,{0xb1,0x64,0x4f,0x17,0xda,0x8c,0x32,0x01}};
struct QueuePublication {HRESULT queue=E_POINTER,owner=E_POINTER;};
template<class Object,class Queue> QueuePublication PublishSceneQueue(Object* object,Queue* queue){
    QueuePublication result;if(!object)return result;
    // Optional NR metadata cannot suppress the required owner marker or turn
    // successful FG initialization into failure. DXGI owns the stored ref.
    if(queue)result.queue=object->SetPrivateDataInterface(SceneGameQueueGuid,queue);
    result.owner=object->SetPrivateData(SceneOwnerGuid,sizeof(SceneOwnerVersion),&SceneOwnerVersion);
    return result;
}
enum class SceneQueueResult {Unmanaged,Ready,Missing,Malformed,Changed};
template<class Object,class Queue,class Query,class Current>
SceneQueueResult ReadSceneQueue(Object* object,Queue& out,Query typed,Current current){
    out.Reset();if(!object)return SceneQueueResult::Missing;
    if(!current())return SceneQueueResult::Changed;
    UINT version=0,size=sizeof(version);
    const HRESULT owner=object->GetPrivateData(SceneOwnerGuid,&size,&version);
    if(!current())return SceneQueueResult::Changed;
    if(owner==DXGI_ERROR_NOT_FOUND)return SceneQueueResult::Unmanaged;
    if(FAILED(owner)||size!=sizeof(version)||version!=SceneOwnerVersion)return SceneQueueResult::Malformed;
    Microsoft::WRL::ComPtr<IUnknown> stored;
    size=sizeof(IUnknown*);
    IUnknown* raw=nullptr;
    const HRESULT read=object->GetPrivateData(SceneGameQueueGuid,&size,&raw);
    // Only a successful pointer-sized interface result transfers a reference.
    // Failed or short ordinary-data results do not grant pointer ownership.
    if(SUCCEEDED(read)&&size==sizeof(IUnknown*))stored.Attach(raw);
    if(!current())return SceneQueueResult::Changed;
    if(FAILED(read)||size!=sizeof(IUnknown*)||!stored)return SceneQueueResult::Missing;
    Queue candidate;
    if(!typed(stored.Get(),candidate)||!candidate)return SceneQueueResult::Malformed;
    // Release the metadata reference before the final epoch check. A failed
    // or reentrant QI never leaves an owned reference in the caller's output.
    stored.Reset();
    if(!current())return SceneQueueResult::Changed;
    out=std::move(candidate);return SceneQueueResult::Ready;
}
}
