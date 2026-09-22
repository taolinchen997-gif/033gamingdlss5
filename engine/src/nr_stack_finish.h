#pragma once
namespace nrstack {
// Full-resolution scratch remains owned by the current model bank and frame lease.
// A cached equal-size multi-layer bank has pass_input but may not have refined[0].
// Selecting one layer must reuse that existing full-resolution texture.
template<class Resource>
Resource* FinalScratch(int passes,bool anyScaled,Resource* refined,Resource* passInput){
    return (anyScaled || (passes==1 && refined))?refined:passInput;
}

// Same recorded operations in production and CPU state/alias checks. Scratch
// is an existing full-size input texture retained by the current frame lease.
// This does not prove execution/fence behaviour on a real D3D12 queue.
template<class Resource,class Read,class Condition,class Write>
Resource* Finish(int passes,Resource* result,Resource* scratch,Read read,Condition condition,Write write){
    if(passes<1)return result;
    read(result);
    condition(result,scratch);
    write(result);
    return scratch; // UAV; the ordinary final resolve owns the next transition.
}
}
