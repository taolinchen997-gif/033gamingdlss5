#include "../runtime/src/grade_use_retirement.h"
#include "../src/command_lifetime.h"
#include <wrl/client.h>
#include <cstdio>
#include <thread>
#include <condition_variable>
#include <limits>
// CPU-only IUnknown refcounts: no graphics device, native queue, real SDK or DLL.
static unsigned checks=0,failures=0,destroyed=0;
static void check(bool yes,const char* label){++checks;if(!yes){++failures;printf("FAIL %s\n",label);}}
struct Object final:IUnknown{
 std::atomic<ULONG> refs{1};
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){++destroyed;delete this;}return n;}
};
struct Ledger{
 Microsoft::WRL::ComPtr<IUnknown> output;
 bool active=true,reset=false,discarded=false,recording=false,pinned=false,observed=true,fence=true;
 unsigned pending=0,queries=0;uint64_t done=7,value=7;
};
struct Ticket{Ledger* slot=nullptr;};
static bool completed(Ticket t){
 if(!t.slot)return false;auto& l=*t.slot;++l.queries;if(!l.active)return true;
 const bool gpu=l.fence&&l.done!=UINT64_MAX&&l.done>=l.value;
 // Same production ledger predicate; completion releases its own reference.
 if(!commandlife::MayRetire(l.reset,l.discarded,l.recording,l.pending,l.pinned,l.observed,gpu))return false;
 l.output.Reset();l.active=false;return true;
}
struct Api{bool(*completed)(Ticket)=nullptr;};
struct Source{Microsoft::WRL::ComPtr<IUnknown> output;};
using Use=k033::GradeUseRetirement<Source,Api,Ticket>;
struct Case{
 Ledger ledger;Use use;Object* raw=nullptr;unsigned before=destroyed;
 Case(){Microsoft::WRL::ComPtr<IUnknown> local;raw=new Object;local.Attach(raw);
  ledger.output=local;use.source.output=local;use.ticket={&ledger};use.api={completed};use.armed=true;}
};
int main(){
 {Case c;c.ledger.reset=true;
  check(c.raw->refs==2&&destroyed==c.before,"after frame both lease and Use keep backbuffer despite completion");
  // The old destroy path omitted this exact poll; no next present is needed.
  check(c.use.retire()==K033_OK,"destroy-phase poll accepts actual GPU plus reset proof");
  check(!c.ledger.output&&!c.use.source.output&&destroyed==c.before+1,"both outstanding backbuffer references are gone before Resize");
  const auto queries=c.ledger.queries;check(c.use.retire()==K033_OK&&c.ledger.queries==queries,"repeat retirement is idempotent");}
 for(unsigned unsafe=0;unsafe<8;++unsafe){Case c;c.ledger.reset=true;
  switch(unsafe){case 0:c.ledger.reset=false;break;case 1:c.ledger.done=6;break;case 2:c.ledger.observed=false;break;
   case 3:c.ledger.recording=true;break;case 4:c.ledger.pending=1;break;case 5:c.ledger.pinned=true;break;
   case 6:c.ledger.fence=false;break;case 7:c.ledger.done=UINT64_MAX;break;}
  check(c.use.retire()==K033_BUSY,"unsafe retirement remains pending");
  check(c.ledger.output&&c.use.source.output&&c.raw->refs==2&&destroyed==c.before,"missing proof cannot release either reference");
  c.ledger.reset=true;c.ledger.done=c.ledger.value;c.ledger.observed=true;c.ledger.recording=false;c.ledger.pending=0;c.ledger.pinned=false;c.ledger.fence=true;
  check(c.use.retire()==K033_OK&&destroyed==c.before+1,"later valid proof can release without forcing flags in production");}
 {Case c;c.ledger.discarded=true;check(c.use.retire()==K033_OK&&destroyed==c.before+1,"proven list/allocator discard also prevents replay");}
 {Case c;c.ledger.reset=true;
  std::mutex gate;std::condition_variable cv;bool entered=false,leave=false;
  std::thread holder([&]{std::unique_lock<std::mutex> held(c.use.mutex);std::unique_lock<std::mutex> lock(gate);entered=true;cv.notify_all();cv.wait(lock,[&]{return leave;});});
  {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[&]{return entered;});}
  check(c.use.retire()==K033_BUSY&&c.ledger.queries==0&&c.raw->refs==2,"Use mutex contention preserves references and proof state");
  {std::lock_guard<std::mutex> lock(gate);leave=true;}cv.notify_all();holder.join();
  check(c.use.retire()==K033_OK&&destroyed==c.before+1,"retry after contention collects confirmed complete lease");}
 {Use use;use.armed=true;use.api={completed};check(use.retire()==K033_BUSY,"armed missing ticket cannot pretend complete");}
 {Use use;use.api={completed};check(use.retire()==K033_OK&&use.finished(),"never recorded source needs no GPU certificate");}
 {Case c;c.use.armed=false;c.ledger.active=false;c.ledger.output.Reset();
  check(c.use.retire()==K033_OK&&destroyed==c.before+1,"confirmed cancellation releases extra Use reference");}
 printf("grade retirement CPU checks=%u failures=%u (fake IUnknown only)\n",checks,failures);return failures?1:0;
}
