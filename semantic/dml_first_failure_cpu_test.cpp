#include "dml_first_failure.h"
#include <stdexcept>
#include <iostream>
#define FAKE_OBJECT \
 ULONG refs=1; \
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;} \
 ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;} \
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;} \
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;} \
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,IUnknown*)override{return E_NOTIMPL;} \
 HRESULT STDMETHODCALLTYPE SetName(PCWSTR)override{return S_OK;}
struct FakeBinding:IDMLBindingTable{
 FAKE_OBJECT
 HRESULT* reason;bool fault=false;const DML_BINDING_DESC* last=nullptr;unsigned tempCalls=0;
 explicit FakeBinding(HRESULT* r):reason(r){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==__uuidof(yytrace::BindingAccess))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**)override{return E_NOTIMPL;}
 void STDMETHODCALLTYPE BindInputs(UINT,const DML_BINDING_DESC* p)override{last=p;}
 void STDMETHODCALLTYPE BindOutputs(UINT,const DML_BINDING_DESC* p)override{last=p;if(fault)*reason=DXGI_ERROR_DEVICE_REMOVED;}
 void STDMETHODCALLTYPE BindTemporaryResource(const DML_BINDING_DESC*)override{++tempCalls;}
 void STDMETHODCALLTYPE BindPersistentResource(const DML_BINDING_DESC*)override{}
 HRESULT STDMETHODCALLTYPE Reset(const DML_BINDING_TABLE_DESC*)override{return E_ABORT;}
};
struct FakeRecorder:IDMLCommandRecorder{
 FAKE_OBJECT
 IDMLBindingTable* received=nullptr;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{if(!p)return E_POINTER;*p=this;AddRef();return S_OK;}
 HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**)override{return E_NOTIMPL;}
 void STDMETHODCALLTYPE RecordDispatch(ID3D12CommandList*,IDMLDispatchable*,IDMLBindingTable* b)override{received=b;}
};
struct FakeDevice:IDMLDevice1{
 FAKE_OBJECT
 HRESULT reason=S_OK;unsigned reasonQueries=0;DML_EXECUTION_FLAGS compiledFlags{};FakeBinding* table=new FakeBinding(&reason);FakeRecorder* recorder=new FakeRecorder;bool* destroyed;bool failTable=false;
 explicit FakeDevice(bool& d):destroyed(&d){}~FakeDevice(){table->Release();recorder->Release();*destroyed=true;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p)override{if(!p)return E_POINTER;*p=this;AddRef();return S_OK;}
 HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DML_FEATURE,UINT,const void*,UINT,void*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateOperator(const DML_OPERATOR_DESC*,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CompileOperator(IDMLOperator*,DML_EXECUTION_FLAGS f,REFIID,void**)override{compiledFlags=f;return E_ABORT;}
 HRESULT STDMETHODCALLTYPE CreateOperatorInitializer(UINT,IDMLCompiledOperator*const*,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateCommandRecorder(REFIID,void** p)override{*p=recorder;recorder->AddRef();return S_OK;}
 HRESULT STDMETHODCALLTYPE CreateBindingTable(const DML_BINDING_TABLE_DESC*,REFIID,void** p)override{*p=nullptr;if(failTable)return E_INVALIDARG;*p=table;table->AddRef();return S_OK;}
 HRESULT STDMETHODCALLTYPE Evict(UINT,IDMLPageable*const*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE MakeResident(UINT,IDMLPageable*const*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason()override{++reasonQueries;return reason;}
 HRESULT STDMETHODCALLTYPE GetParentDevice(REFIID id,void** p)override{return QueryInterface(id,p);}
 HRESULT STDMETHODCALLTYPE CompileGraph(const DML_GRAPH_DESC*,DML_EXECUTION_FLAGS f,REFIID,void**)override{compiledFlags=f;return E_INVALIDARG;}
};
int main()try{unsigned checks=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("trace CPU assertion "+std::to_string(checks));};
 bool destroyed=false;auto* native=new FakeDevice(destroyed);auto journal=std::make_shared<yytrace::Journal>();auto* trace=new yytrace::Device(native,journal);native->Release();
 // Actual production wrapper must preserve the caller's native compiler flags,
 // including allowing metacommands when DISABLE_META_COMMANDS was not set.
 for(unsigned flags=0;flags<8;++flags){
  require(trace->CompileGraph(nullptr,DML_EXECUTION_FLAGS(flags),__uuidof(IDMLCompiledOperator),nullptr)==E_INVALIDARG);
  require(unsigned(native->compiledFlags)==flags);
  require(trace->CompileOperator(nullptr,DML_EXECUTION_FLAGS(flags),__uuidof(IDMLCompiledOperator),nullptr)==E_ABORT);
  require(unsigned(native->compiledFlags)==flags);
 }
 Microsoft::WRL::ComPtr<IDMLBindingTable> binding;require(trace->CreateBindingTable(nullptr,IID_PPV_ARGS(&binding))==S_OK);
 Microsoft::WRL::ComPtr<yytrace::BindingAccess> access;require(SUCCEEDED(binding.As(&access))&&access->Native()==native->table);
 Microsoft::WRL::ComPtr<IUnknown> a,b;require(SUCCEEDED(binding.As(&a)));require(SUCCEEDED(access.As(&b)));require(a==b);a.Reset();b.Reset();access.Reset();
 DML_BUFFER_BINDING empty{};DML_BINDING_DESC desc{DML_BINDING_TYPE_BUFFER,&empty};binding->BindInputs(1,&desc);require(native->table->last==&desc);
 require(binding->Reset(nullptr)==E_ABORT);require(!journal->frozen);
 Microsoft::WRL::ComPtr<IDMLCommandRecorder> recorder;require(SUCCEEDED(trace->CreateCommandRecorder(IID_PPV_ARGS(&recorder))));recorder->RecordDispatch(nullptr,nullptr,binding.Get());require(native->recorder->received==native->table);
 recorder->RecordDispatch(nullptr,nullptr,native->table);require(native->recorder->received==native->table);
 journal->Stop();const auto queries=native->reasonQueries;const auto beforeStopCount=journal->count;
 binding->BindInputs(1,&desc);binding->BindOutputs(1,&desc);binding->BindPersistentResource(nullptr);binding->Reset(nullptr);recorder->RecordDispatch(nullptr,nullptr,binding.Get());
 require(journal->count==beforeStopCount&&native->reasonQueries==queries);require(native->recorder->received==native->table);
 Microsoft::WRL::ComPtr<IDMLBindingTable> afterStop;require(SUCCEEDED(trace->CreateBindingTable(nullptr,IID_PPV_ARGS(&afterStop)))&&afterStop.Get()==native->table);afterStop.Reset();
 Microsoft::WRL::ComPtr<IDMLCommandRecorder> newRecorder;require(SUCCEEDED(trace->CreateCommandRecorder(IID_PPV_ARGS(&newRecorder))));newRecorder->RecordDispatch(nullptr,nullptr,binding.Get());require(native->recorder->received==native->table);newRecorder.Reset();
 journal->enabled=true;native->table->fault=true;binding->BindOutputs(1,&desc);require(journal->frozen);const auto count=journal->count;const auto e=journal->recent[(count-1)%96];
 require(e.call=="BindOutputs"&&e.before==S_OK&&e.after==DXGI_ERROR_DEVICE_REMOVED);binding->BindTemporaryResource(nullptr);require(native->table->tempCalls==1&&journal->count==count);
 require(trace->Release()==0);require(!destroyed);binding.Reset();require(!destroyed);recorder.Reset();require(destroyed);
 auto ring=std::make_shared<yytrace::Journal>();for(unsigned i=0;i<120;++i)ring->Add("bind","",S_OK,S_OK);
 require(ring->count==120&&!ring->frozen);require(ring->recent[(120-1)%96].ordinal==120&&ring->recent[(25-1)%96].ordinal==25);
 ring->Add("fault","",S_OK,DXGI_ERROR_DEVICE_REMOVED);ring->Add("late","",DXGI_ERROR_DEVICE_REMOVED,DXGI_ERROR_DEVICE_REMOVED);
 require(ring->count==121&&ring->frozen);const auto p=std::filesystem::current_path()/L"trace-fixture.log";std::filesystem::remove(p);
 const auto error=std::string(1500,'x')+"UNTRUNCATED_END";ring->Save(p,error.c_str(),S_OK,DXGI_ERROR_DEVICE_REMOVED);
 std::ifstream f(p,std::ios::binary);const std::string text((std::istreambuf_iterator<char>(f)),{});require(text.find("UNTRUNCATED_END")!=std::string::npos);require(text.find("121 fault")!=std::string::npos&&text.find("late")==std::string::npos);
 bool destroyed2=false;auto* native2=new FakeDevice(destroyed2);native2->failTable=true;auto* trace2=new yytrace::Device(native2,std::make_shared<yytrace::Journal>());native2->Release();void* failed=reinterpret_cast<void*>(1);
 require(trace2->CreateBindingTable(nullptr,__uuidof(IDMLBindingTable),&failed)==E_INVALIDARG&&!failed);trace2->Release();require(destroyed2);
 printf("PASS trace CPU mocks: %u checks; first failure retention, unchanged arguments, native unwrapping, COM lifetime, bounded ring and full error; no GPU\n",checks);return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
