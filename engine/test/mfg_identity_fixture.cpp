#include "../src/mfg/module_identity.hpp"
extern "C" __declspec(dllexport) void* slGetPluginFunction(){return nullptr;}
extern "C" __declspec(dllexport) HMODULE ImplementationOwner(HMODULE adapter){
    return mfgunlock::identity::Owner(reinterpret_cast<const void*>(&ImplementationOwner),adapter);
}
