#pragma once
namespace k033beta2 {
// User intent may be read before resources exist. Only a successfully created
// universal owner may apply it; failures do not write a substitute preference.
template<class Settings>bool RestoreUniversalIntent(const Settings& settings,bool created,
    unsigned owners,unsigned error,bool universal_boot,bool can_generate){
    return created&&owners!=0&&error==0&&universal_boot&&can_generate&&settings.route==1;
}
}
