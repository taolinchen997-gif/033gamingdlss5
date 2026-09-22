#include "nr_borrowed_target.h"
#include "nr_game_input_abi.h"
#include <cstdio>
#include <initializer_list>
// Copying an SDK intrusive pointer performs game refcount writes and invokes a
// private release function. Make those operations impossible in this fixture.
template<class T> struct Borrowed {
    T* ptr=nullptr;
    Borrowed()=default;Borrowed(const Borrowed&)=delete;
    Borrowed& operator=(const Borrowed&)=delete;
    T* get()const{return ptr;}
};
struct Resource {int value=123;int* get_native_resource(){return &value;}};
struct Texture {Resource* resource=nullptr;auto* get_d3d12_resource_container(){return resource;}};
struct View {Borrowed<Texture> texture;auto& get_texture_d3d12(){return texture;}};
struct Target {
    unsigned count=1;Borrowed<View>* views=nullptr;
    auto get_rtv_count(){return count;}auto* get_rtvs_ptr(){return views;}
};
static unsigned checks=0,failed=0;
static void Check(bool ok,const char* label){++checks;if(!ok){++failed;printf("FAIL %s\n",label);}}
int main(){
    using namespace nrgame033;
    Target target;View view;Texture texture;Resource resource;Borrowed<View> views[1];
    Check(!BorrowNativeTarget(static_cast<Target*>(nullptr)),"absent target");
    Check(!BorrowNativeTarget(&target),"absent views");
    target.views=views;Check(!BorrowNativeTarget(&target),"absent view");
    views[0].ptr=&view;Check(!BorrowNativeTarget(&target),"absent texture");
    view.texture.ptr=&texture;Check(!BorrowNativeTarget(&target),"absent resource");
    texture.resource=&resource;
    Check(BorrowNativeTarget(&target)==&resource.value,"borrow exact native pointer with no engine add_ref/release");
    for(unsigned count:{0u,2u,8u,0xffffffffu}){target.count=count;
        Check(!BorrowNativeTarget(&target),"incompatible attachment count rejected");}
    target.count=1;for(unsigned n=0;n<1000;++n)
        if(BorrowNativeTarget(&target)!=&resource.value){++failed;break;}
    Check(resource.value==123&&texture.resource==&resource&&view.texture.ptr==&texture&&views[0].ptr==&view,
          "repeated read leaves game-owned graph untouched");
    Adapter a;a.id=Re4Tdb71;a.source=ReEngineScene;a.sceneSchema=71;
    Check(WaitingForRenderer(a)&&!Supported(a),"boot waits before renderer callback");
    a.compatible=RendererReady;
    Check(!WaitingForRenderer(a)&&Supported(a),"callback readiness starts NR without requiring guide/model success");
    a.compatible=Initializing;
    Check(WaitingForRenderer(a),"device reset returns to boot gate");
    a.compatible=RendererReady;Check(Supported(a),"next callback recovers automatically");
    a.compatible=Unavailable;
    Check(!WaitingForRenderer(a)&&!Supported(a),"failed adapter does not park colour fallback forever");
    a.version=1;a.compatible=0;Check(!WaitingForRenderer(a),"legacy not-compatible flag never means wait");
    a.compatible=1;Check(Supported(a),"legacy ready adapter retained");
    a.version=99;a.compatible=0;Check(!WaitingForRenderer(a)&&!Supported(a),"unknown descriptor does not block NR");
    a.version=2;a.sceneSchema=74;Check(!WaitingForRenderer(a),"other games do not inherit RE4 startup gate");
    printf("Native input borrowing/startup CPU checks=%u failures=%u; no game or GPU execution\n",checks,failed);
    return failed?1:0;
}
