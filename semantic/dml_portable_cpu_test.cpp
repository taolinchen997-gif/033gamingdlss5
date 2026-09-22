#include "dml_portable_device.h"
#include <cstdio>
#include <stdexcept>
struct MockDevice final:IDMLDevice1 {
 ULONG refs=1;unsigned graph=0,op=0;DML_EXECUTION_FLAGS flags{};bool* destroyed;
 explicit MockDevice(bool& d):destroyed(&d){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(!out)return E_POINTER;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n){*destroyed=true;delete this;}return n;}
 HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,IUnknown*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetName(PCWSTR)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DML_FEATURE,UINT,const void*,UINT,void*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateOperator(const DML_OPERATOR_DESC*,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CompileOperator(IDMLOperator*,DML_EXECUTION_FLAGS f,REFIID,void**)override{++op;flags=f;return E_ABORT;}
 HRESULT STDMETHODCALLTYPE CreateOperatorInitializer(UINT,IDMLCompiledOperator*const*,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateCommandRecorder(REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE CreateBindingTable(const DML_BINDING_TABLE_DESC*,REFIID,void**)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE Evict(UINT,IDMLPageable*const*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE MakeResident(UINT,IDMLPageable*const*)override{return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason()override{return DXGI_ERROR_DEVICE_REMOVED;}
 HRESULT STDMETHODCALLTYPE GetParentDevice(REFIID id,void** out)override{return QueryInterface(id,out);}
 HRESULT STDMETHODCALLTYPE CompileGraph(const DML_GRAPH_DESC*,DML_EXECUTION_FLAGS f,REFIID,void**)override{++graph;flags=f;return E_INVALIDARG;}
};
int main(){unsigned checks=0;auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("portable DML contract");};try{
 bool destroyed=false;auto* inner=new MockDevice(destroyed);auto* adapter=new yyworker::PortableDmlDevice(inner);check(inner->refs==2);inner->Release();
 for(auto id:{__uuidof(IUnknown),__uuidof(IDMLObject),__uuidof(IDMLDevice),__uuidof(IDMLDevice1)}){void* out=nullptr;check(adapter->QueryInterface(id,&out)==S_OK&&out==adapter);static_cast<IUnknown*>(out)->Release();}
 void* unknown=reinterpret_cast<void*>(1);check(adapter->QueryInterface(__uuidof(ID3D12Device),&unknown)==E_NOINTERFACE&&!unknown);check(adapter->QueryInterface(__uuidof(IUnknown),nullptr)==E_POINTER);
 for(unsigned flags=0;flags<8;++flags){
  check(adapter->CompileOperator(nullptr,DML_EXECUTION_FLAGS(flags),__uuidof(IDMLCompiledOperator),nullptr)==E_ABORT);
  check(unsigned(inner->flags)==(flags|unsigned(DML_EXECUTION_FLAG_DISABLE_META_COMMANDS)));
  check(adapter->CompileGraph(nullptr,DML_EXECUTION_FLAGS(flags),__uuidof(IDMLCompiledOperator),nullptr)==E_INVALIDARG);
  check(unsigned(inner->flags)==(flags|unsigned(DML_EXECUTION_FLAG_DISABLE_META_COMMANDS)));
 }
 check(inner->op==8&&inner->graph==8);check(adapter->GetDeviceRemovedReason()==DXGI_ERROR_DEVICE_REMOVED);
 void* parent=nullptr;check(adapter->GetParentDevice(__uuidof(IUnknown),&parent)==S_OK&&parent==inner);static_cast<IUnknown*>(parent)->Release();
 check(adapter->Release()==0&&destroyed);
 printf("PASS portable DirectML CPU mock: %u checks; compile flags, HRESULTs, COM identity/lifetime and parent forwarding; no GPU\n",checks);return 0;
 }catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}}
