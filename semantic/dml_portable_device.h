// 033 owned DirectML adapter. Force the documented DirectCompute path on every
// compilation, while keeping our own DIRECT queue and its normal GPU timeout.
// No hooks, game device wrappers, global driver settings or fallback providers.
#pragma once
#include "third_party/onnxruntime/dml_provider_factory.h"
#include <wrl/client.h>
#include <atomic>
namespace yyworker {
class PortableDmlDevice final : public IDMLDevice1 {
 std::atomic<ULONG> refs_{1};Microsoft::WRL::ComPtr<IDMLDevice1> inner_;
 static DML_EXECUTION_FLAGS Flags(DML_EXECUTION_FLAGS f){return DML_EXECUTION_FLAGS(unsigned(f)|unsigned(DML_EXECUTION_FLAG_DISABLE_META_COMMANDS));}
public:
 explicit PortableDmlDevice(IDMLDevice1* device):inner_(device){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
  if(!out)return E_POINTER;*out=nullptr;
  if(id==__uuidof(IUnknown)||id==__uuidof(IDMLObject)||id==__uuidof(IDMLDevice)||id==__uuidof(IDMLDevice1)){*out=static_cast<IDMLDevice1*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release() override{const auto n=--refs_;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g,UINT* n,void* p) override{return inner_->GetPrivateData(g,n,p);}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g,UINT n,const void* p) override{return inner_->SetPrivateData(g,n,p);}
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g,IUnknown* p) override{return inner_->SetPrivateDataInterface(g,p);}
 HRESULT STDMETHODCALLTYPE SetName(PCWSTR n) override{return inner_->SetName(n);}
 HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DML_FEATURE f,UINT q,const void* i,UINT n,void* o) override{return inner_->CheckFeatureSupport(f,q,i,n,o);}
 HRESULT STDMETHODCALLTYPE CreateOperator(const DML_OPERATOR_DESC* d,REFIID id,void** p) override{return inner_->CreateOperator(d,id,p);}
 HRESULT STDMETHODCALLTYPE CompileOperator(IDMLOperator* op,DML_EXECUTION_FLAGS f,REFIID id,void** p) override{return inner_->CompileOperator(op,Flags(f),id,p);}
 HRESULT STDMETHODCALLTYPE CreateOperatorInitializer(UINT n,IDMLCompiledOperator* const* ops,REFIID id,void** p) override{return inner_->CreateOperatorInitializer(n,ops,id,p);}
 HRESULT STDMETHODCALLTYPE CreateCommandRecorder(REFIID id,void** p) override{return inner_->CreateCommandRecorder(id,p);}
 HRESULT STDMETHODCALLTYPE CreateBindingTable(const DML_BINDING_TABLE_DESC* d,REFIID id,void** p) override{return inner_->CreateBindingTable(d,id,p);}
 HRESULT STDMETHODCALLTYPE Evict(UINT n,IDMLPageable* const* p) override{return inner_->Evict(n,p);}
 HRESULT STDMETHODCALLTYPE MakeResident(UINT n,IDMLPageable* const* p) override{return inner_->MakeResident(n,p);}
 HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override{return inner_->GetDeviceRemovedReason();}
 HRESULT STDMETHODCALLTYPE GetParentDevice(REFIID id,void** p) override{return inner_->GetParentDevice(id,p);}
 HRESULT STDMETHODCALLTYPE CompileGraph(const DML_GRAPH_DESC* d,DML_EXECUTION_FLAGS f,REFIID id,void** p) override{return inner_->CompileGraph(d,Flags(f),id,p);}
};
}
