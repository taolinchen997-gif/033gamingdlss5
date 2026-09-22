#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include "preferences_backend.h"
#include "../shared/settings_codec.h"
#include "../shared/beta2_fg_codec.h"
#include "../shared/settings_session_native.h"
#include <atomic>
#include <string>
namespace k033compat {namespace {
class NativePreferences final:public PreferencesBackend {
    // Constructed before mailboxes, released after every member. Successful
    // activation retains this object for the complete process/writer lifetime.
    k033settings::NativeSessionGate sessions;
    k033settings::PreferencesMailbox mailbox;
    k033settings::NrPreferencesMailbox nr_mailbox;
    K033_NrSettings initial_nr=k033::nr_defaults(false);
    k033settings::FgPreferencesMailbox fg_mailbox;
    K033_Beta2FgSettings initial_fg=k033beta2::fg_defaults();
    std::wstring path;
    HANDLE event=nullptr,thread=nullptr;
    std::atomic<unsigned> active{0},error{0};
    int loaded=K033_BYPASS;
    bool bounded_file(bool& missing){
        WIN32_FILE_ATTRIBUTE_DATA data{};missing=false;
        if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)){
            DWORD e=GetLastError();missing=e==ERROR_FILE_NOT_FOUND;
            if(!missing)error=e;return missing;
        }
        if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||data.nFileSizeHigh||data.nFileSizeLow>256*1024){error=ERROR_INVALID_DATA;return false;}
        return true;
    }
    static DWORD WINAPI entry(void* self){static_cast<NativePreferences*>(self)->loop();return 0;}
    void refresh_external(){
        bool missing=false;K033_Settings settings{};K033_NrSettings nr{};K033_Beta2FgSettings fg{};
        try {if(bounded_file(missing)&&!missing&&k033settings::read_settings(path.c_str(),settings,nr)&&k033settings::read_fg(path.c_str(),fg)){
            mailbox.publish_external(settings);nr_mailbox.publish_external(nr);fg_mailbox.publish_external(fg);}}catch(...){}
    }
    void loop(){
        // This service is process-owned like the permanently pinned hook
        // component. It is not an external helper, and has no DllMain cleanup.
        for(;;){
            // Bound write frequency even when fsync is faster than UI edits.
            // Delay BEFORE taking a snapshot so all intervening edits coalesce.
            Sleep(250);
            K033_Settings value{};uint64_t ticket=0;
            if(mailbox.take(value,ticket)){
                bool missing=false,ok=false;
                try {ok=bounded_file(missing)&&k033settings::write_settings(path.c_str(),value);}catch(...){}
                if(!ok){DWORD e=GetLastError();error=e?e:ERROR_WRITE_FAULT;}else error=0;
                mailbox.complete(ticket,ok?K033_OK:K033_IO_ERROR);
                // Failed writes retry at most once per second. New edits are
                // coalesced, with the old successful revision remaining valid.
                if(!ok){Sleep(1000);continue;}
            }
            K033_NrSettings nr{};
            if(nr_mailbox.take(nr,ticket)){
                bool missing=false,ok=false;
                try {ok=bounded_file(missing)&&k033settings::write_nr(path.c_str(),nr);}catch(...){}
                if(!ok){DWORD e=GetLastError();error=e?e:ERROR_WRITE_FAULT;}else error=0;
                nr_mailbox.complete(ticket,ok?K033_OK:K033_IO_ERROR);
                if(!ok){Sleep(1000);continue;}
            }
            K033_Beta2FgSettings fg{};
            if(fg_mailbox.take(fg,ticket)){
                bool missing=false,ok=false;
                try{ok=bounded_file(missing)&&k033settings::write_fg(path.c_str(),fg);}catch(...){}
                if(!ok){DWORD e=GetLastError();error=e?e:ERROR_WRITE_FAULT;}else error=0;
                fg_mailbox.complete(ticket,ok?K033_OK:K033_IO_ERROR);
                if(!ok){Sleep(1000);continue;}
            }
            refresh_external(); // same schema/path across running game processes
            DWORD wait=WaitForSingleObject(event,1000);
            if(wait==WAIT_FAILED){error=GetLastError();active=3;return;}
        }
    }
public:
    // After activate succeeds this object is intentionally retained by the
    // process-owned component; do not destroy it from DLL_PROCESS_DETACH.
    ~NativePreferences(){if(thread)CloseHandle(thread);if(event)CloseHandle(event);}
    int load(K033_Settings& settings)override{
        int admitted=sessions.join();if(admitted!=K033_OK){error=sessions.error();return loaded=admitted;}
        path=sessions.folder()+L"\\settings.ini";
        bool missing=false;if(!bounded_file(missing))return loaded=K033_IO_ERROR;
        if(missing)return loaded=K033_BYPASS;
        K033_Settings candidate{};
        if(!k033settings::read_settings(path.c_str(),candidate,initial_nr)||!k033settings::read_fg(path.c_str(),initial_fg)){error=ERROR_INVALID_DATA;return loaded=K033_IO_ERROR;}
        settings=candidate;return loaded=K033_OK;
    }
    int activate()override{
        unsigned expected=0;if(!active.compare_exchange_strong(expected,1))return expected==2?K033_OK:K033_BUSY;
        if(!sessions.held()||path.empty()){active=3;return K033_IO_ERROR;}
        // Pin BEFORE handing our object/code to a thread. This is called after
        // the loader's own process-lifetime pin/Start, never in DllMain.
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&entry),&pinned)){
            error=GetLastError();active=3;return K033_IO_ERROR;}
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event){error=GetLastError();active=3;return K033_IO_ERROR;}
        thread=CreateThread(nullptr,0,entry,this,0,nullptr);
        if(!thread){error=GetLastError();CloseHandle(event);event=nullptr;active=3;return K033_IO_ERROR;}
        // Offers while starting stay in the mailbox. Publish the event only
        // after successful creation, without overwriting an early thread error.
        expected=1;active.compare_exchange_strong(expected,2);
        return active.load()==2?K033_OK:K033_IO_ERROR;
    }
    int offer(const K033_Settings& settings)override{
        if(!sessions.held())return K033_BUSY;
        int r=mailbox.offer(settings);
        if(r==K033_OK&&active.load()==2&&event)SetEvent(event);
        return r;
    }
    int receive(K033_Settings& settings)override{return mailbox.receive(settings);}
    int session_admission()const override{return sessions.held()?K033_OK:loaded;}
    int load_nr(K033_NrSettings& nr)override{if(loaded==K033_OK)nr=initial_nr;return loaded;}
    int offer_nr(const K033_NrSettings& nr)override{
        if(!sessions.held())return K033_BUSY;
        int r=nr_mailbox.offer(nr);if(r==K033_OK&&active.load()==2&&event)SetEvent(event);return r;
    }
    int receive_nr(K033_NrSettings& nr)override{return nr_mailbox.receive(nr);}
    int load_fg(K033_Beta2FgSettings& fg)override{if(loaded==K033_OK)fg=initial_fg;return loaded;}
    int offer_fg(const K033_Beta2FgSettings& fg)override{
        if(!sessions.held())return K033_BUSY;
        int r=fg_mailbox.offer(fg);if(r==K033_OK&&active.load()==2&&event)SetEvent(event);return r;
    }
    int receive_fg(K033_Beta2FgSettings& fg)override{return fg_mailbox.receive(fg);}
    int inspect(PreferencesState& state)override{
        int r=mailbox.status(state.queued,state.saved,state.save_result);if(r!=K033_OK)return r;
        r=nr_mailbox.status(state.nr_queued,state.nr_saved,state.nr_save_result);if(r!=K033_OK)return r;
        r=fg_mailbox.status(state.fg_queued,state.fg_saved,state.fg_save_result);if(r!=K033_OK)return r;
        state.load_result=loaded;state.active=active.load();state.win32_error=error.load();return K033_OK;
    }
};
}
std::unique_ptr<PreferencesBackend> make_preferences_backend(){return std::make_unique<NativePreferences>();}
}
