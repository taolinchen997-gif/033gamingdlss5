// Shared fence rules, testable with CPU-only command queue doubles.
#pragma once
#include <cstdint>
namespace gpu033 {
inline bool Complete(uint64_t completed,uint64_t target){
    return completed!=UINT64_MAX && target!=UINT64_MAX && completed>=target;
}
template<class List,class Queue,class Fence,class BaseList>
uint64_t Submit(List* list,Queue* queue,Fence* fence,uint64_t& serial,BaseList* base){
    if(!list||!queue||!fence||!base||serial>=UINT64_MAX-1||list->Close()<0)return 0;
    BaseList* lists[]={base};queue->ExecuteCommandLists(1,lists);
    const uint64_t value=++serial;
    return queue->Signal(fence,value)<0?0:value;
}
}
