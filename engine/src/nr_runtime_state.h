#pragma once
#include "../sdk/reshade-6.8.0/include/reshade_api_device.hpp"

namespace nrruntimestate033 {
// Only use on the runtime's own DX12 immediate list after finishing effects.
// Raw D3D12 NR calls bypass ReShade's cached descriptor heaps/root signatures.
// In ReShade 6.8, a zero-table API bind explicitly reapplies its heaps and
// invalidates both cached root signatures. The subsequent overlay/effect pass
// binds its own layout and arguments normally. No flush, GPU wait or new queue.
// Source: crosire/reshade v6.8.0, d3d12_impl_command_list.cpp:455-519.
template<class CommandList> class Bindings {
    CommandList* commands;
public:
    explicit Bindings(CommandList* value) : commands(value) {}
    ~Bindings() {
        commands->bind_descriptor_tables(reshade::api::shader_stage::all,
                                         reshade::api::pipeline_layout{},0,0,nullptr);
    }
    Bindings(const Bindings&)=delete;
    Bindings& operator=(const Bindings&)=delete;
};

template<class CommandList,class Work>
auto Run(CommandList* commands,Work&& work) -> decltype(work()) {
    Bindings<CommandList> restore(commands);
    return work();
}
}
