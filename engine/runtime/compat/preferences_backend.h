#pragma once
#include "../shared/preferences_mailbox.h"
#include "../shared/beta2_fg_settings.h"
namespace k033compat {
struct PreferencesState {
    uint64_t queued=0,saved=0;
    int load_result=K033_BYPASS,save_result=K033_BYPASS;
    uint32_t active=0,win32_error=0;
    uint64_t nr_queued=0,nr_saved=0;
    int nr_save_result=K033_BYPASS;
    uint64_t fg_queued=0,fg_saved=0;int fg_save_result=K033_BYPASS;
};
struct PreferencesBackend {
    virtual ~PreferencesBackend()=default;
    // Synchronous one-time load BEFORE game factories/device creation. Missing
    // file keeps the caller's shared defaults; invalid file is left untouched.
    virtual int load(K033_Settings&)=0;
    // Native production implementation has joined the shared session gate.
    // CPU test backends have no production settings/GPU lifetime to register.
    virtual int session_admission()const{return K033_OK;}
    // Called only after the process-owned component has finished Start. Native
    // implementation owns one no-window writer thread, no rendering/driver IO.
    virtual int activate()=0;
    // Never performs file IO or waits for the writer on a game callback.
    virtual int offer(const K033_Settings&)=0;
    // Nonblocking in-memory delivery of changes saved by another 033 process.
    virtual int receive(K033_Settings&)=0;
    virtual int load_nr(K033_NrSettings&){return K033_BYPASS;}
    virtual int offer_nr(const K033_NrSettings&){return K033_UNSUPPORTED;}
    virtual int receive_nr(K033_NrSettings&){return K033_BYPASS;}
    virtual int load_fg(K033_Beta2FgSettings&){return K033_BYPASS;}
    virtual int offer_fg(const K033_Beta2FgSettings&){return K033_UNSUPPORTED;}
    virtual int receive_fg(K033_Beta2FgSettings&){return K033_BYPASS;}
    virtual int inspect(PreferencesState&)=0;
};
std::unique_ptr<PreferencesBackend> make_preferences_backend();
// Implemented by the common-settings owner; called after successful CompatStart.
void preferences_ready()noexcept;
}
