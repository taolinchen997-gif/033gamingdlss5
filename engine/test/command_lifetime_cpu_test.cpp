#include "../src/command_lifetime.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <condition_variable>
#include <chrono>
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failures;printf("FAIL %s\n",label);}}
class FakeList final : public ID3D12Object {
    std::atomic<ULONG> refs{1};IUnknown* stored=nullptr;
    std::mutex dataMutex,missMutex;std::condition_variable missCv;
    unsigned misses=0;
public:
    bool failAttach=false,throwOnAttach=false,throwAfterStore=false;
    bool synchronizeFirstMisses=false,reenterOnAttach=false,missTimeout=false;
    unsigned replacements=0;
    unsigned installAttempts=0;
    std::weak_ptr<commandlife::State> attemptedState;
    std::shared_ptr<commandlife::State> reentered;
    ULONG StoredReferences(){std::lock_guard<std::mutex> lock(dataMutex);if(!stored)return 0;stored->AddRef();return stored->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(ID3D12Object))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{const auto value=--refs;if(!value){if(stored)stored->Release();delete this;}return value;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid,UINT* size,void* data) override{
        if(guid!=commandlife::Key)return E_FAIL;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            if(stored){if(!size||*size<sizeof(stored)||!data)return E_INVALIDARG;
                *size=sizeof(stored);stored->AddRef();memcpy(data,&stored,sizeof(stored));return S_OK;}
        }
        if(synchronizeFirstMisses){
            std::unique_lock<std::mutex> lock(missMutex);++misses;missCv.notify_all();
            if(misses<2 && !missCv.wait_for(lock,std::chrono::seconds(2),[&]{return misses>=2;}))missTimeout=true;
        }
        return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,const IUnknown* value) override{
        if(guid==commandlife::Key&&value){
            IUnknown* observed=nullptr;
            if(SUCCEEDED(const_cast<IUnknown*>(value)->QueryInterface(commandlife::Key,reinterpret_cast<void**>(&observed)))){
                ++installAttempts;attemptedState=static_cast<commandlife::Watch*>(observed)->state;observed->Release();
            }
        }
        if(throwOnAttach)throw std::bad_alloc();
        if(failAttach)return E_FAIL;if(guid!=commandlife::Key)return E_INVALIDARG;
        if(reenterOnAttach){reenterOnAttach=false;reentered=commandlife::Observe(this);}
        std::lock_guard<std::mutex> lock(dataMutex);
        auto* p=const_cast<IUnknown*>(value);if(p)p->AddRef();if(stored){++replacements;stored->Release();}stored=p;
        if(throwAfterStore)throw std::bad_alloc();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override{return S_OK;}
};
int main(){
    auto* list=new FakeList;
    auto life=commandlife::Observe(list);
    Check(life&&!life->discarded,"private interface keeps notification alive");
    Check(list->installAttempts==1&&list->StoredReferences()==1,"successful attach leaves exactly the native object's Watch reference");
    auto same=commandlife::Observe(list);
    Check(same==life&&!life->discarded,"repeated recording attaches once without falsely marking destroyed");
    Check(!commandlife::MayRetire(false,false,false,0,false,true,true),"GPU completed but live closed list remains replayable");
    Check(list->Release()==0,"observer does not AddRef or keep the command list alive");
    Check(life->discarded.load(),"final native Release notifies even without ReShade destruction or Reset");
    Check(!commandlife::MayRetire(false,life->discarded,false,0,false,true,false),"destroyed list with unfinished GPU work cannot retire");
    Check(commandlife::MayRetire(false,life->discarded,false,0,false,true,true),"destroyed list retires once observed GPU work completes");
    Check(!commandlife::MayRetire(false,true,false,0,false,false,true),"destruction never fabricates a missing submission observation");
    Check(!commandlife::MayRetire(false,true,false,0,true,true,true),"unknown queue/signal error stays pinned after destruction");
    Check(!commandlife::MayRetire(false,true,true,0,false,true,true),"in-progress recording cannot retire");
    Check(!commandlife::MayRetire(false,true,false,1,false,true,true),"submission callback in progress cannot retire");
    Check(commandlife::MayRetire(true,false,false,0,false,true,true),"successful reset path still retires normally");
    auto* rejected=new FakeList;rejected->failAttach=true;
    Check(!commandlife::Observe(rejected),"failed attachment yields no destruction proof; normal Reset path remains necessary");
    Check(rejected->installAttempts==1&&rejected->attemptedState.expired()&&rejected->StoredReferences()==0,"failed attach releases the initial Watch reference exactly once");
    rejected->Release();
    for(int i=0;i<500;++i){auto* next=new FakeList;auto token=commandlife::Observe(next);
        Check(token!=life&&!token->discarded,"reused list addresses receive independent lifetime tokens");
        next->Release();Check(token->discarded,"each object independently publishes destruction");}
    Check(!commandlife::Observe(nullptr),"null list declined");
    auto* retained=new FakeList;auto listState=commandlife::Observe(retained);
    Check(!commandlife::AllocatorFor(listState).Invalidated(),"unknown first allocator is not guessed");
    auto* unobserved=new FakeList;
    Check(!commandlife::Existing(unobserved),"lookup never installs an observer");unobserved->Release();
    auto* arena=new FakeList;auto arenaState=commandlife::Observe(arena);
    commandlife::BindAllocator(listState,arenaState);
    auto recording=commandlife::AllocatorFor(listState);
    commandlife::AllocatorReset(arenaState,E_FAIL);
    Check(!recording.Invalidated(),"failed allocator Reset does not invalidate a recording");
    commandlife::AllocatorReset(arenaState,S_OK);
    Check(!listState->discarded && recording.Invalidated(),"allocator Reset invalidates storage even while command list stays alive");
    Check(!commandlife::MayRetire(false,recording.Invalidated(),false,0,false,true,false),"allocator Reset cannot bypass unfinished GPU fence");
    Check(commandlife::MayRetire(false,recording.Invalidated(),false,0,false,true,true),"retained list can retire after allocator Reset and all GPU completion");
    Check(!commandlife::MayRetire(false,recording.Invalidated(),false,0,false,false,true),"allocator Reset cannot invent an unseen submission");
    commandlife::BindAllocator(listState,arenaState);
    auto nextRecording=commandlife::AllocatorFor(listState);
    Check(!nextRecording.Invalidated(),"new recording on reset allocator uses new serial");
    Check(recording.Invalidated(),"new binding cannot revive old storage");
    arena->Release();
    Check(nextRecording.Invalidated(),"allocator destruction invalidates storage without holding native allocator alive");
    auto* secondArena=new FakeList;auto secondState=commandlife::Observe(secondArena);
    commandlife::BindAllocator(listState,secondState);
    for(int i=0;i<1000;++i){
        auto oldRecording=commandlife::AllocatorFor(listState);
        Check(!oldRecording.Invalidated(),"allocator epochs begin replayable");
        commandlife::AllocatorReset(secondState,S_OK);
        Check(oldRecording.Invalidated(),"allocator reset advances epoch exactly for old recording");
        commandlife::BindAllocator(listState,secondState);
    }
    commandlife::BindAllocator(listState,{});
    Check(!commandlife::AllocatorFor(listState).state,"failed new association clears previous allocator instead of guessing");
    secondArena->Release();retained->Release();
    auto* collision=new FakeList;auto* otherOwner=new FakeList;
    collision->SetPrivateDataInterface(commandlife::Key,otherOwner);
    Check(!commandlife::Observe(collision),"foreign private interface under same key is never overwritten");
    IUnknown* untouched=nullptr;UINT bytes=sizeof(untouched);
    collision->GetPrivateData(commandlife::Key,&bytes,&untouched);
    Check(untouched==otherOwner,"failed observation preserves existing private interface ownership");
    untouched->Release();otherOwner->Release();collision->Release();

    // Exact missing-first-association counterexample from the retained-list
    // report. No list Reset/destruction is invented from completed GPU work.
    auto* firstUse=new FakeList;auto* initialAllocator=new FakeList;
    Check(!commandlife::Existing(firstUse),"old reset-before-first-NR ordering has no list Watch");
    auto firstLife=commandlife::Observe(firstUse);
    auto unknownRecording=commandlife::AllocatorFor(firstLife);
    Check(!unknownRecording.state,"old initial recording loses allocator association");
    Check(commandlife::BindObservedAllocator(firstUse,initialAllocator),"successful initial Create supplies exact allocator before a new recording");
    auto initialRecording=commandlife::AllocatorFor(firstLife);
    Check(initialRecording.state&&!initialRecording.Invalidated(),"new first recording captures live allocator serial");
    commandlife::AllocatorReset(commandlife::Existing(initialAllocator),E_FAIL);
    Check(!initialRecording.Invalidated(),"failed allocator Reset preserves first recording storage");
    commandlife::AllocatorReset(commandlife::Existing(initialAllocator),S_OK);
    Check(initialRecording.Invalidated()&&!firstLife->discarded,"successful allocator Reset proves old storage gone with list retained");
    Check(!unknownRecording.state&&!unknownRecording.Invalidated(),"later exact binding never repairs an old unknown snapshot by guessing");
    Check(!commandlife::MayRetire(false,initialRecording.Invalidated(),false,0,false,true,false),"exact initial association still waits for all GPU work");
    Check(commandlife::MayRetire(false,initialRecording.Invalidated(),false,0,false,true,true),"exact initial association unblocks completed retained-list lease");
    Check(!commandlife::MayRetire(false,initialRecording.Invalidated(),false,0,false,false,true),"initial association does not fabricate submission evidence");
    Check(commandlife::BindObservedAllocator(firstUse,initialAllocator),"host/native duplicate binding is accepted without another reset");
    auto rebound=commandlife::AllocatorFor(firstLife);
    Check(rebound.serial==1&&!rebound.Invalidated()&&initialRecording.Invalidated(),"binding itself never advances reset serial or revives old snapshot");
    auto* nextAllocator=new FakeList;
    Check(commandlife::BindObservedAllocator(firstUse,nextAllocator),"successful reset changes future recording allocator only");
    auto nextUse=commandlife::AllocatorFor(firstLife);
    commandlife::AllocatorReset(commandlife::Existing(initialAllocator),S_OK);
    Check(rebound.Invalidated()&&!nextUse.Invalidated(),"old allocator reset cannot invalidate replacement allocator recording");
    auto* failedAllocator=new FakeList;failedAllocator->failAttach=true;
    Check(!commandlife::BindObservedAllocator(firstUse,failedAllocator),"failed exact association reports failure");
    Check(!commandlife::AllocatorFor(firstLife).state&&nextUse.state,"failed new association clears only future binding, preserves old immutable snapshot");
    Check(commandlife::BindObservedAllocator(firstUse,nextAllocator),"successful retry restores a real future association");
    auto* throwingAllocator=new FakeList;throwingAllocator->throwOnAttach=true;bool caught=false;
    try{commandlife::BindObservedAllocator(firstUse,throwingAllocator);}catch(const std::bad_alloc&){caught=true;}
    Check(caught&&!commandlife::AllocatorFor(firstLife).state,"exception during new storage observation cannot leave previous allocator assigned");
    Check(throwingAllocator->installAttempts==1&&throwingAllocator->attemptedState.expired()&&throwingAllocator->StoredReferences()==0,
        "throwing attachment leaks neither Watch nor State");
    throwingAllocator->Release();
    auto* storedThenThrow=new FakeList;storedThenThrow->throwAfterStore=true;caught=false;
    try{commandlife::Observe(storedThenThrow);}catch(const std::bad_alloc&){caught=true;}
    auto storedAttempt=storedThenThrow->attemptedState;
    Check(caught&&storedThenThrow->installAttempts==1&&storedThenThrow->StoredReferences()==1&&!storedAttempt.expired(),
        "throw after native retention releases only the initial reference, preserving native ownership");
    storedThenThrow->Release();Check(storedAttempt.expired(),"native retained Watch is destroyed exactly once at final object release");
    firstUse->Release();initialAllocator->Release();nextAllocator->Release();failedAllocator->Release();
    Check(nextUse.Invalidated(),"native allocator destruction still releases its lifetime token without list ownership");

    // Both threads initially see the same allocator key absent. Production
    // Observe must serialize installation and repeat the lookup under its lock.
    auto* sharedAllocator=new FakeList;sharedAllocator->synchronizeFirstMisses=true;
    std::shared_ptr<commandlife::State> threadState[2];
    std::thread one([&]{threadState[0]=commandlife::Observe(sharedAllocator);});
    std::thread two([&]{threadState[1]=commandlife::Observe(sharedAllocator);});
    one.join();two.join();
    Check(!sharedAllocator->missTimeout,"concurrent first-lookups reached the controlled CPU barrier");
    Check(threadState[0]&&threadState[0]==threadState[1],"two concurrent initial observations share exactly one Watch");
    Check(sharedAllocator->replacements==0&&!threadState[0]->discarded,"initialization race cannot falsely discard a live allocator");
    sharedAllocator->Release();
    Check(threadState[0]->discarded&&threadState[1]->discarded,"one actual allocator destruction reaches both immutable users");
    auto* recursiveObject=new FakeList;recursiveObject->reenterOnAttach=true;
    auto recursiveState=commandlife::Observe(recursiveObject);
    Check(recursiveState&&!recursiveObject->reentered&&!recursiveState->discarded&&recursiveObject->replacements==0,
        "reentrant private-interface installation fails closed without lock recursion or replacement");
    recursiveObject->Release();
    printf("COMMAND LIFETIME CPU: %u checks, %u failures; fake COM object only, no GPU device\n",checks,failures);
    return failures?1:0;
}
