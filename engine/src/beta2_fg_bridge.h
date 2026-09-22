#pragma once
#include "../runtime/shared/beta2_fg_settings.h"
namespace k033beta2 {
// Implemented in the sole shared-settings TU; no file IO in these calls.
bool ReadFg(K033_Beta2FgSettings&);
bool OfferFg(const K033_Beta2FgSettings&);
bool SetFgRoute(unsigned route,bool automatic);
bool SetUniversalMultiplier(unsigned multiplier);
bool SetUniversalEnabled(bool enabled);
bool GetNativeOption(const char* key,int& value);
bool SetNativeOption(const char* key,int value);
}
