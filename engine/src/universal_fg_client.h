#pragma once
#include "universal_fg_abi.h"
namespace ufg033client {
inline const ufg033abi::Api* Get(){
    HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Get),&module))return nullptr;
    auto get=reinterpret_cast<ufg033abi::GetApi>(GetProcAddress(module,"K033_GetUniversalFramegen"));if(!get)return nullptr;
    auto* api=get(ufg033abi::Version);return api&&api->size==sizeof(*api)&&api->version==ufg033abi::Version?api:nullptr;
}
}
