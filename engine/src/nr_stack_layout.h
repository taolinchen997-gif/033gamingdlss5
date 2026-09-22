#pragma once
namespace nrstack {
constexpr unsigned PassBase(unsigned pass){return 14+(pass-1)*12;}
// 0..13: encode/resolve/history/white; 14..47: three extra-layer blocks;
// 48..53: portrait thumbnail/enhance. Never rewrite recorded descriptors.
constexpr unsigned FinalBase=54, ClarityBase=FinalBase+4, DescriptorCount=ClarityBase+4;
static_assert(PassBase(3)+10<=48,"extra layers overlap portrait bindings");
}
