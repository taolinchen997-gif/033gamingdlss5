// COM identity, not interface address or adapter LUID, defines device ownership.
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
namespace identity033 {
using Microsoft::WRL::ComPtr;
// ReShade v6.8.0 source/com_utils.hpp: public, owning QueryInterface result.
inline constexpr GUID UnwrappedObject={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
inline ComPtr<IUnknown> Canonical(IUnknown* object) {
    ComPtr<IUnknown> current;
    if(!object||FAILED(object->QueryInterface(IID_PPV_ARGS(&current)))||!current)return {};
    std::array<ComPtr<IUnknown>,8> visited;
    for(auto& seen:visited){
        for(const auto& previous:visited)if(previous.Get()==current.Get())return {};
        seen=current;
        ComPtr<IUnknown> native;
        const auto hr=current->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(native.GetAddressOf()));
        if(hr==E_NOINTERFACE)return current;
        if(FAILED(hr)||!native)return {};
        ComPtr<IUnknown> next;
        if(FAILED(native->QueryInterface(IID_PPV_ARGS(&next)))||!next)return {};
        current=std::move(next);
    }
    return {}; // Broken/self-referential or excessively nested wrapper.
}
struct Result {
    bool equal=false,normalized=false;
    uintptr_t expected=0,actual=0;
    HRESULT query=E_POINTER;
};
inline Result Child(IUnknown* expectedIdentity,ID3D12DeviceChild* child) {
    Result result;result.expected=reinterpret_cast<uintptr_t>(expectedIdentity);
    if(!expectedIdentity||!child)return result;
    // ReShade 6.8.0's resource GetDevice hook immediately treats this result
    // as ID3D12Device (GetPrivateData). An IUnknown result can be a distinct
    // interface with a different vtable. Query the typed device first, then
    // normalize its COM identity; never weaken the same-device requirement.
    ComPtr<ID3D12Device> reported;
    result.query=child->GetDevice(IID_PPV_ARGS(&reported));
    if(FAILED(result.query)||!reported)return result;
    auto actual=Canonical(reported.Get());
    result.actual=reinterpret_cast<uintptr_t>(actual.Get());
    result.equal=actual&&actual.Get()==expectedIdentity;
    result.normalized=result.equal&&reported.Get()!=actual.Get();return result;
}
inline bool Equal(IUnknown* a,IUnknown* b){auto left=Canonical(a),right=Canonical(b);return left&&right&&left==right;}
}
