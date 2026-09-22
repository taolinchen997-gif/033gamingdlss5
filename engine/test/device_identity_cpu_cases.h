// Real COM QueryInterface/refcount calls; no D3D device creation or GPU work.
#include "../src/d3d12_identity.h"
#include "device_identity_mock.h"
namespace identitytest033 {
struct Node final:IUnknown {
    ULONG refs=1;IUnknown* identity=this;IUnknown* native=nullptr;
    DeviceView view{this};
    HRESULT unwrapFailure=E_NOINTERFACE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)){*out=identity;identity->AddRef();return S_OK;}
        if(id==__uuidof(ID3D12Device)){*out=static_cast<ID3D12Device*>(&view);AddRef();return S_OK;}
        if(id==identity033::UnwrappedObject){
            if(!native)return unwrapFailure;
            *out=native;native->AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
};
struct Child final:ID3D12DeviceChild {
    ULONG refs=1;IUnknown* device;bool fail=false;
    bool hookedResource=false;unsigned unsafeQueries=0,typedQueries=0;
    explicit Child(IUnknown* p):device(p){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(ID3D12Object)&&id!=__uuidof(ID3D12DeviceChild))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(fail)return E_FAIL;
        // ReShade 6.8.0's resource GetDevice hook uses the returned pointer as
        // ID3D12Device immediately. Model the incompatible IID as rejection,
        // not an actual wrong-vtable call or a hanging lock in this CPU test.
        if(hookedResource&&id!=__uuidof(ID3D12Device)){++unsafeQueries;return E_NOINTERFACE;}
        auto hr=device->QueryInterface(id,out);
        if(SUCCEEDED(hr)&&id==__uuidof(ID3D12Device)){
            ++typedQueries;
            if(hookedResource){UINT size=0;static_cast<ID3D12Device*>(*out)->GetPrivateData(identity033::UnwrappedObject,&size,nullptr);}
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{return S_OK;}
};
}
static void deviceIdentityChecks(){
    using namespace identitytest033;using identity033::Canonical;using identity033::Child;
    Node device,other,wrapper,outer,alias;wrapper.native=&device;outer.native=&wrapper;alias.identity=&device;
    identitytest033::Child direct(&device),wrapped(&wrapper),nested(&outer),foreign(&other),alternate(&alias);
    {
        auto expected=Canonical(&device);
        check(expected.Get()==&device,"native canonical identity acquired");
        check(Child(expected.Get(),&direct).equal,"same device accepted");
        // This is the actual mismatch shape observed when raw and proxy devices
        // are returned by different children. The old address test rejects it.
        identity033::ComPtr<IUnknown> oldPointer;wrapped.GetDevice(IID_PPV_ARGS(&oldPointer));
        check(oldPointer.Get()!=expected.Get(),"old direct address test rejects wrapper of same device");
        auto normalized=Child(expected.Get(),&wrapped);
        check(normalized.equal&&normalized.normalized,"same underlying wrapped device accepted");
        check(Child(expected.Get(),&nested).equal,"nested wrappers resolve to same native device");
        check(Child(expected.Get(),&alternate).equal,"alternate interface shares canonical IUnknown");
        check(identity033::Equal(&alias,&device),"interface address is not COM identity");
        check(!Child(expected.Get(),&foreign).equal,"different device still rejected even on same adapter");
        identitytest033::Child hooked(&device),hookedForeign(&other),hookedWrapped(&wrapper);
        hooked.hookedResource=hookedForeign.hookedResource=hookedWrapped.hookedResource=true;
        check(static_cast<IUnknown*>(&device)!=static_cast<IUnknown*>(&device.view),"device view differs from canonical IUnknown address");
        auto hookResult=Child(expected.Get(),&hooked);
        check(hookResult.equal&&hooked.unsafeQueries==0&&hooked.typedQueries==1,"hooked resource obtains a typed device before identity query");
        check(device.view.privateCalls==1,"hook reads private data through the correct device vtable");
        check(Child(expected.Get(),&hookedWrapped).equal,"hooked wrapped resource normalizes to native identity");
        check(!Child(expected.Get(),&hookedForeign).equal&&hookedForeign.unsafeQueries==0&&hookedForeign.typedQueries==1,"hooked different device is queried safely but rejected");
        foreign.fail=true;auto failed=Child(expected.Get(),&foreign);
        check(!failed.equal&&failed.query==E_FAIL&&failed.actual==0,"GetDevice failure is not accepted");
        check(!Child(expected.Get(),nullptr).equal&&!Child(nullptr,&direct).equal,"null ownership cannot pass");
        Node cycleA,cycleB;cycleA.native=&cycleB;cycleB.native=&cycleA;
        check(!Canonical(&cycleA),"cyclic wrapper chain rejected");
        check(cycleA.refs==1&&cycleB.refs==1,"cycle traversal balances references");
        Node self;self.native=&self;check(!Canonical(&self)&&self.refs==1,"self unwrap rejected without leaking");
        Node queryError;queryError.unwrapFailure=E_FAIL;
        check(!Canonical(&queryError)&&queryError.refs==1,"failed unwrap is not silently treated as native");
        std::array<Node,10> tooDeep;for(unsigned i=0;i+1<tooDeep.size();++i)tooDeep[i].native=&tooDeep[i+1];
        check(!Canonical(&tooDeep[0]),"depth limit fails closed");
        for(auto& node:tooDeep)check(node.refs==1,"bounded traversal releases intermediate references");
        for(int i=0;i<1000;++i)check(Child(expected.Get(),&wrapped).equal,"repeated frame identity stays consistent");
    }
    check(device.refs==1&&other.refs==1&&wrapper.refs==1&&outer.refs==1&&alias.refs==1,"all owned device references released");
    check(direct.refs==1&&wrapped.refs==1,"checking ownership does not retain command list/resources");
    check(!device.view.unexpectedCalls&&!other.view.unexpectedCalls&&!wrapper.view.unexpectedCalls,"identity validation invokes no GPU operation");
}
