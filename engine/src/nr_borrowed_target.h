#pragma once
// Called only while the engine's EndRendering callback owns its scene graph.
// Borrow the SDK's stored pointers without copying an intrusive_ptr: even a
// balanced temporary copy invokes the game's private release routine. The
// caller takes a public COM reference before returning from the callback.
namespace nrgame033 {
template<class Target> auto BorrowNativeTarget(Target* target)
    -> decltype(target->get_rtvs_ptr()[0].get()->get_texture_d3d12().get()->
                get_d3d12_resource_container()->get_native_resource()) {
    if(!target)return nullptr;
    const auto count=target->get_rtv_count();
    if(count!=1)return nullptr; // This semantic adapter expects one velocity attachment.
    auto* views=target->get_rtvs_ptr();if(!views)return nullptr;
    auto* view=views[0].get();if(!view)return nullptr;
    auto* texture=view->get_texture_d3d12().get();if(!texture)return nullptr;
    auto* resource=texture->get_d3d12_resource_container();
    return resource?resource->get_native_resource():nullptr;
}
}
