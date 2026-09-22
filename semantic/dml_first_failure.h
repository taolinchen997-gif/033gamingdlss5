// 033 bounded first-inference journal. Disabled after the first valid result.
// Native arguments, native dispatchables, and normal queue timeout are retained.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "third_party/onnxruntime/dml_provider_factory.h"
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <mutex>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace yytrace {
inline std::string Hex(HRESULT hr){char b[16];std::snprintf(b,sizeof b,"0x%08lX",static_cast<unsigned long>(hr));return b;}
struct Entry { uint64_t ordinal=0; std::string call,details; HRESULT before=S_OK,after=S_OK,result=S_OK; };
struct Journal {
 std::atomic<bool> enabled{true};
 std::mutex mutex; std::array<Entry,96> recent{}; uint64_t count=0; bool frozen=false;
 bool Enabled()const noexcept{return enabled.load(std::memory_order_relaxed);}
 void Stop()noexcept{enabled.store(false,std::memory_order_relaxed);}
 void Add(std::string call,std::string details,HRESULT before,HRESULT after,HRESULT result=S_OK){
  std::lock_guard<std::mutex> lock(mutex);if(frozen||!Enabled())return;
  const auto n=++count;recent[(n-1)%recent.size()]={n,std::move(call),details.substr(0,3072),before,after,result};
  if(FAILED(after))frozen=true;
 }
 void Save(const std::filesystem::path& path,const char* error,HRESULT d3d,HRESULT dml){
  std::lock_guard<std::mutex> lock(mutex);std::ofstream f(path,std::ios::binary|std::ios::app);
  f<<"\nD3D12="<<Hex(d3d)<<" DML="<<Hex(dml)<<" calls="<<count<<" first_removed_frozen="<<frozen<<"\n";
  const auto first=count>recent.size()?count-recent.size()+1:1;
  for(auto n=first;n<=count;++n){const auto& e=recent[(n-1)%recent.size()];
   f<<e.ordinal<<" "<<e.call<<" before="<<Hex(e.before)<<" after="<<Hex(e.after)<<" hr="<<Hex(e.result)<<" "<<e.details<<"\n";}
  // Error text was previously truncated at 250 bytes. Preserve up to 16 KiB.
  f<<"FULL_ERROR_BEGIN\n"<<std::string(error?error:"",std::min<size_t>(error?std::strlen(error):0,16384))<<"\nFULL_ERROR_END\n";
 }
};
struct Context {
 Microsoft::WRL::ComPtr<IDMLDevice> native;std::shared_ptr<Journal> journal;
 Context(IDMLDevice* d,std::shared_ptr<Journal> j):native(d),journal(std::move(j)){}
 bool Enabled()const noexcept{return journal->Enabled();}
 HRESULT Before()const{return Enabled()?native->GetDeviceRemovedReason():S_OK;}
 void After(const char* call,const std::string& details,HRESULT before,HRESULT hr=S_OK)const{if(Enabled())journal->Add(call,details,before,native->GetDeviceRemovedReason(),hr);}
};
inline std::string Table(const DML_BINDING_TABLE_DESC* desc){
 if(!desc)return "null table";std::ostringstream s;s<<"dispatch="<<desc->Dispatchable<<" descriptors="<<desc->SizeInDescriptors;
 if(desc->Dispatchable){const auto p=desc->Dispatchable->GetBindingProperties();s<<" required_descriptors="<<p.RequiredDescriptorCount<<" temporary="<<p.TemporaryResourceSize<<" persistent="<<p.PersistentResourceSize;}
 return s.str();
}
inline void Buffer(std::ostringstream& s,const DML_BUFFER_BINDING& b){
 s<<"{resource="<<b.Buffer<<" offset="<<b.Offset<<" bytes="<<b.SizeInBytes;
 if(b.Buffer){const auto d=b.Buffer->GetDesc();D3D12_HEAP_PROPERTIES p{};D3D12_HEAP_FLAGS flags{};const auto hr=b.Buffer->GetHeapProperties(&p,&flags);
  s<<" dimension="<<d.Dimension<<" width="<<d.Width<<" resource_flags="<<unsigned(d.Flags)<<" heap_hr="<<Hex(hr)<<" heap_type="<<p.Type<<" cpu_page="<<p.CPUPageProperty<<" pool="<<p.MemoryPoolPreference;}
 s<<"}";
}
inline std::string Bindings(UINT n,const DML_BINDING_DESC* p){
 std::ostringstream s;s<<"count="<<n;if(!p){s<<" null";return s.str();}
 for(UINT i=0;i<std::min(n,8u);++i){s<<" ["<<i<<" type="<<p[i].Type<<" ";
  if(p[i].Desc&&p[i].Type==DML_BINDING_TYPE_BUFFER)Buffer(s,*static_cast<const DML_BUFFER_BINDING*>(p[i].Desc));
  if(p[i].Desc&&p[i].Type==DML_BINDING_TYPE_BUFFER_ARRAY){const auto& a=*static_cast<const DML_BUFFER_ARRAY_BINDING*>(p[i].Desc);s<<"array="<<a.BindingCount;
   if(a.Bindings)for(UINT j=0;j<std::min(a.BindingCount,8u);++j)Buffer(s,a.Bindings[j]);}
  s<<"]";}
 return s.str();
}

// Private access IID, used only to unwrap our binding before native RecordDispatch.
struct __declspec(uuid("647957cd-8fcb-4dea-85d2-3eeced41f7be")) BindingAccess : IUnknown {virtual IDMLBindingTable* STDMETHODCALLTYPE Native()=0;};
#define YYTRACE_OBJECT_METHODS \
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;} \
 ULONG STDMETHODCALLTYPE Release() override{const auto n=--refs_;if(!n)delete this;return n;} \
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g,UINT* n,void* p)override{return inner_->GetPrivateData(g,n,p);} \
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g,UINT n,const void* p)override{return inner_->SetPrivateData(g,n,p);} \
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g,IUnknown* p)override{return inner_->SetPrivateDataInterface(g,p);} \
 HRESULT STDMETHODCALLTYPE SetName(PCWSTR n)override{return inner_->SetName(n);}

class Binding final:public IDMLBindingTable,public BindingAccess {
 std::atomic<ULONG> refs_{1};Microsoft::WRL::ComPtr<IDMLBindingTable> inner_;std::shared_ptr<Context> c_;
public:
 Binding(IDMLBindingTable* p,std::shared_ptr<Context> c):inner_(p),c_(std::move(c)){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{
  if(!p)return E_POINTER;*p=nullptr;
  if(id==__uuidof(BindingAccess))*p=static_cast<BindingAccess*>(this);
  else if(id==__uuidof(IUnknown)||id==__uuidof(IDMLObject)||id==__uuidof(IDMLDeviceChild)||id==__uuidof(IDMLBindingTable))*p=static_cast<IDMLBindingTable*>(this);
  else return E_NOINTERFACE;AddRef();return S_OK;
 }
 YYTRACE_OBJECT_METHODS
 IDMLBindingTable* STDMETHODCALLTYPE Native()override{return inner_.Get();}
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** p)override{return inner_->GetDevice(id,p);}
 void STDMETHODCALLTYPE BindInputs(UINT n,const DML_BINDING_DESC* p)override{if(!c_->Enabled()){inner_->BindInputs(n,p);return;}const auto b=c_->Before();const auto d=Bindings(n,p);inner_->BindInputs(n,p);c_->After("BindInputs",d,b);}
 void STDMETHODCALLTYPE BindOutputs(UINT n,const DML_BINDING_DESC* p)override{if(!c_->Enabled()){inner_->BindOutputs(n,p);return;}const auto b=c_->Before();const auto d=Bindings(n,p);inner_->BindOutputs(n,p);c_->After("BindOutputs",d,b);}
 void STDMETHODCALLTYPE BindTemporaryResource(const DML_BINDING_DESC* p)override{if(!c_->Enabled()){inner_->BindTemporaryResource(p);return;}const auto b=c_->Before();const auto d=Bindings(p?1:0,p);inner_->BindTemporaryResource(p);c_->After("BindTemporaryResource",d,b);}
 void STDMETHODCALLTYPE BindPersistentResource(const DML_BINDING_DESC* p)override{if(!c_->Enabled()){inner_->BindPersistentResource(p);return;}const auto b=c_->Before();const auto d=Bindings(p?1:0,p);inner_->BindPersistentResource(p);c_->After("BindPersistentResource",d,b);}
 HRESULT STDMETHODCALLTYPE Reset(const DML_BINDING_TABLE_DESC* p)override{if(!c_->Enabled())return inner_->Reset(p);const auto b=c_->Before();const auto d=Table(p);const auto hr=inner_->Reset(p);c_->After("BindingReset",d,b,hr);return hr;}
};
class Recorder final:public IDMLCommandRecorder {
 std::atomic<ULONG> refs_{1};Microsoft::WRL::ComPtr<IDMLCommandRecorder> inner_;std::shared_ptr<Context> c_;
public:
 Recorder(IDMLCommandRecorder* p,std::shared_ptr<Context> c):inner_(p),c_(std::move(c)){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{
  if(!p)return E_POINTER;*p=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(IDMLObject)&&id!=__uuidof(IDMLDeviceChild)&&id!=__uuidof(IDMLCommandRecorder))return E_NOINTERFACE;
  *p=static_cast<IDMLCommandRecorder*>(this);AddRef();return S_OK;
 }
 YYTRACE_OBJECT_METHODS
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** p)override{return inner_->GetDevice(id,p);}
 void STDMETHODCALLTYPE RecordDispatch(ID3D12CommandList* list,IDMLDispatchable* op,IDMLBindingTable* table)override{
  Microsoft::WRL::ComPtr<BindingAccess> access;auto* native=table;
  if(table&&SUCCEEDED(table->QueryInterface(IID_PPV_ARGS(&access))))native=access->Native();
  if(!c_->Enabled()){inner_->RecordDispatch(list,op,native);return;}
  const auto b=c_->Before();std::ostringstream s;s<<"list="<<list<<" dispatch="<<op<<" native_binding="<<native;
  inner_->RecordDispatch(list,op,native);c_->After("RecordDispatch",s.str(),b);
 }
};
class Device final:public IDMLDevice1 {
 std::atomic<ULONG> refs_{1};Microsoft::WRL::ComPtr<IDMLDevice1> inner_;std::shared_ptr<Context> c_;
public:
 Device(IDMLDevice1* p,std::shared_ptr<Journal> j):inner_(p),c_(std::make_shared<Context>(p,std::move(j))){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{
  if(!p)return E_POINTER;*p=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(IDMLObject)&&id!=__uuidof(IDMLDevice)&&id!=__uuidof(IDMLDevice1))return E_NOINTERFACE;
  *p=static_cast<IDMLDevice1*>(this);AddRef();return S_OK;
 }
 YYTRACE_OBJECT_METHODS
 HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DML_FEATURE f,UINT q,const void* i,UINT n,void* o)override{return inner_->CheckFeatureSupport(f,q,i,n,o);}
 HRESULT STDMETHODCALLTYPE CreateOperator(const DML_OPERATOR_DESC* d,REFIID id,void** p)override{const auto b=c_->Before();const auto hr=inner_->CreateOperator(d,id,p);c_->After("CreateOperator","type="+std::to_string(d?d->Type:-1),b,hr);return hr;}
 HRESULT STDMETHODCALLTYPE CompileOperator(IDMLOperator* op,DML_EXECUTION_FLAGS f,REFIID id,void** p)override{const auto b=c_->Before();const auto hr=inner_->CompileOperator(op,f,id,p);c_->After("CompileOperator","flags="+std::to_string(f),b,hr);return hr;}
 HRESULT STDMETHODCALLTYPE CreateOperatorInitializer(UINT n,IDMLCompiledOperator*const* ops,REFIID id,void** p)override{const auto b=c_->Before();const auto hr=inner_->CreateOperatorInitializer(n,ops,id,p);c_->After("CreateOperatorInitializer","count="+std::to_string(n),b,hr);return hr;}
 HRESULT STDMETHODCALLTYPE CreateCommandRecorder(REFIID id,void** p)override{
  // Always unwrap any cached first-inference binding, even after tracing stops.
  if(id!=__uuidof(IDMLCommandRecorder)||!p)return inner_->CreateCommandRecorder(id,p);
  const auto b=c_->Before();Microsoft::WRL::ComPtr<IDMLCommandRecorder> native;const auto hr=inner_->CreateCommandRecorder(IID_PPV_ARGS(&native));
  c_->After("CreateCommandRecorder","",b,hr);*p=nullptr;if(SUCCEEDED(hr))*p=static_cast<IDMLCommandRecorder*>(new Recorder(native.Get(),c_));return hr;
 }
 HRESULT STDMETHODCALLTYPE CreateBindingTable(const DML_BINDING_TABLE_DESC* d,REFIID id,void** p)override{
  if(id!=__uuidof(IDMLBindingTable)||!p||!c_->Enabled())return inner_->CreateBindingTable(d,id,p);
  const auto b=c_->Before();const auto details=Table(d);Microsoft::WRL::ComPtr<IDMLBindingTable> native;const auto hr=inner_->CreateBindingTable(d,IID_PPV_ARGS(&native));
  c_->After("CreateBindingTable",details,b,hr);*p=nullptr;if(SUCCEEDED(hr))*p=static_cast<IDMLBindingTable*>(new Binding(native.Get(),c_));return hr;
 }
 HRESULT STDMETHODCALLTYPE Evict(UINT n,IDMLPageable*const* p)override{return inner_->Evict(n,p);}
 HRESULT STDMETHODCALLTYPE MakeResident(UINT n,IDMLPageable*const* p)override{return inner_->MakeResident(n,p);}
 HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason()override{return inner_->GetDeviceRemovedReason();}
 HRESULT STDMETHODCALLTYPE GetParentDevice(REFIID id,void** p)override{return inner_->GetParentDevice(id,p);}
 HRESULT STDMETHODCALLTYPE CompileGraph(const DML_GRAPH_DESC* d,DML_EXECUTION_FLAGS f,REFIID id,void** p)override{const auto b=c_->Before();const auto hr=inner_->CompileGraph(d,f,id,p);c_->After("CompileGraph","flags="+std::to_string(f),b,hr);return hr;}
};
#undef YYTRACE_OBJECT_METHODS
}
