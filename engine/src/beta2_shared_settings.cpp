// 033 test build 2 reuses S53's single shared settings writer/session gate.
// Compile this separately from dlss5_033.cpp: old and shared pregrade have the
// same source, but are intentionally not redefined in one translation unit.
#include "beta2_shared_settings.h"
#include "beta2_fg_bridge.h"
#include "../runtime/compat/preferences_backend.h"
#include <atomic>
#include <mutex>
namespace k033beta2 {namespace {
std::mutex prepare_mutex;
std::atomic<int> admission{K033_BUSY};
std::atomic<k033compat::PreferencesBackend*> backend{nullptr};
K033_Settings initial_grade{};
K033_NrSettings initial_nr{};
std::mutex fg_mutex;
K033_Beta2FgSettings current_fg{};
}
int Prepare() noexcept {
 try {std::lock_guard<std::mutex> lock(prepare_mutex);
  if(backend.load(std::memory_order_acquire))return K033_OK;
  auto p=k033compat::make_preferences_backend();
  auto grade=k033::defaults();auto nr=k033::nr_defaults(false);
  int result=p->load(grade);
  if(result<0||p->session_admission()!=K033_OK){admission=result==K033_OK?K033_BUSY:result;return admission;}
  p->load_nr(nr);K033_Beta2FgSettings fg{};p->load_fg(fg);
  result=p->activate();if(result!=K033_OK){admission=result;return result;}
  initial_grade=grade;initial_nr=nr;current_fg=fg;
  // The reused worker pins its containing module and lives for this process.
  backend.store(p.release(),std::memory_order_release);admission=K033_OK;return K033_OK;
 }catch(...){admission=K033_IO_ERROR;return K033_IO_ERROR;}
}
int Initial(K033_Settings& grade,K033_NrSettings& nr) noexcept {
 if(!backend.load(std::memory_order_acquire))return admission.load();grade=initial_grade;nr=initial_nr;return K033_OK;
}
int OfferGrade(const K033_Settings& s) noexcept {try{auto p=backend.load();return p?p->offer(s):admission.load();}catch(...){return K033_IO_ERROR;}}
int OfferNr(const K033_NrSettings& s) noexcept {try{auto p=backend.load();return p?p->offer_nr(s):admission.load();}catch(...){return K033_IO_ERROR;}}
int ReceiveGrade(K033_Settings& s) noexcept {try{auto p=backend.load();return p?p->receive(s):admission.load();}catch(...){return K033_IO_ERROR;}}
int ReceiveNr(K033_NrSettings& s) noexcept {try{auto p=backend.load();return p?p->receive_nr(s):admission.load();}catch(...){return K033_IO_ERROR;}}
int Inspect(SettingsStatus& s) noexcept {try{s={};s.admission=admission.load();auto p=backend.load();if(!p)return s.admission;
 k033compat::PreferencesState v{};int r=p->inspect(v);if(r!=K033_OK)return r;
 s.load_result=v.load_result;s.save_result=v.save_result;s.nr_save_result=v.nr_save_result;s.active=v.active;s.win32_error=v.win32_error;
 s.queued=v.queued;s.saved=v.saved;s.nr_queued=v.nr_queued;s.nr_saved=v.nr_saved;
 s.fg_queued=v.fg_queued;s.fg_saved=v.fg_saved;s.fg_save_result=v.fg_save_result;return K033_OK;
 }catch(...){return K033_IO_ERROR;}}
namespace {
bool RefreshFg(k033compat::PreferencesBackend* p){
 if(!p)return false;K033_Beta2FgSettings external;
 if(p->receive_fg(external)==K033_OK)current_fg=external;return true;
}
template<class Edit>bool EditFg(Edit edit) noexcept {try{
 auto p=backend.load();if(!p)return false;
 std::unique_lock<std::mutex> lock(fg_mutex,std::try_to_lock);if(!lock.owns_lock())return false;
 RefreshFg(p);auto value=current_fg;if(!edit(value)||!fg_valid(value))return false;
 if(p->offer_fg(value)!=K033_OK)return false;current_fg=value;return true;
 }catch(...){return false;}}
uint32_t* NativeField(K033_Beta2FgSettings& s,const char* key){
 if(!key)return nullptr;
 if(!std::strcmp(key,"ForceMultiplier"))return &s.native_multiplier;
 if(!std::strcmp(key,"已打开"))return &s.native_enabled;
 if(!std::strcmp(key,"MaxCount"))return &s.max_count;
 if(!std::strcmp(key,"TemporalFix"))return &s.temporal_fix;
 if(!std::strcmp(key,"ForceFlipMeteringOff"))return &s.force_flip_meter_off;
 if(!std::strcmp(key,"RaiseFrameCeiling"))return &s.raise_ceiling;
 if(!std::strcmp(key,"ForceOTAPlugins"))return &s.force_ota;
 return nullptr;
}
}
bool ReadFg(K033_Beta2FgSettings& value){try{
 auto p=backend.load();if(!p)return false;
 std::unique_lock<std::mutex> lock(fg_mutex,std::try_to_lock);if(!lock.owns_lock()||!RefreshFg(p))return false;
 value=current_fg;return true;
 }catch(...){return false;}}
bool OfferFg(const K033_Beta2FgSettings& value){return EditFg([&](K033_Beta2FgSettings& s){s=value;return true;});}
bool SetFgRoute(unsigned route,bool automatic){return EditFg([&](K033_Beta2FgSettings& s){s.route=route;s.automatic=automatic?1u:0u;return true;});}
bool SetUniversalMultiplier(unsigned multiplier){return EditFg([&](K033_Beta2FgSettings& s){s.universal_multiplier=multiplier;return true;});}
bool SetUniversalEnabled(bool enabled){return EditFg([&](K033_Beta2FgSettings& s){s.universal_enabled=enabled?1u:0u;return true;});}
bool GetNativeOption(const char* key,int& value){K033_Beta2FgSettings s;if(!ReadFg(s))return false;auto field=NativeField(s,key);if(!field)return false;value=int(*field);return true;}
bool SetNativeOption(const char* key,int value){return EditFg([&](K033_Beta2FgSettings& s){auto field=NativeField(s,key);if(!field||value<0)return false;*field=uint32_t(value);return true;});}
}
extern "C" __declspec(dllexport) int __cdecl K033_Beta2PrepareSettings(){return k033beta2::Prepare();}
