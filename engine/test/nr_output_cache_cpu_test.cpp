#include "../src/nr_output_cache.h"
#include "../src/portrait_age_policy.h"
#include <cstdio>
#include <set>
struct Resource { int id; };
static unsigned checks=0,failures=0;
static void Check(bool ok,const char* what){++checks;if(!ok){++failures;printf("FAIL %s\n",what);}}
int main(){
    using namespace nroutput;
    int device=1,otherDevice=2;
    Geometry a{&device,5120,2160,2970,1253,100,1,10};
    Check(Compatible(a,a),"HDR/SDR share identical model geometry");
    for(int change=0;change<8;++change){auto b=a;
        switch(change){case 0:b.device=&otherDevice;break;case 1:++b.width;break;case 2:++b.height;break;
        case 3:++b.guideWidth;break;case 4:++b.guideHeight;break;case 5:++b.work;break;
        case 6:b.full=0;break;case 7:++b.modelFormat;break;}
        Check(!Compatible(a,b),"incompatible geometry/precision never qualifies as cached");
    }
    int serial=0,allocations=0,releases=0;std::set<Resource*> live;
    auto allocate=[&](){++allocations;auto* p=new Resource{++serial};live.insert(p);return p;};
    auto release=[&](Resource* p){Check(live.erase(p)==1,"release only owned resource once");++releases;delete p;};
    auto create=[&](Resource*& x,Resource*& y,int){x=allocate();y=allocate();return true;};
    Resource *full=allocate(),*result=allocate(),*spareFull=nullptr,*spareResult=nullptr;
    auto originalFull=full;auto originalResult=result;int format=10,spareFormat=0;
    auto sw=[&](int desired,auto creator){return Switch(full,result,format,spareFull,spareResult,spareFormat,desired,creator,release);};
    Check(sw(10,create)==Result::Unchanged&&allocations==2,"same format has no allocation");
    auto failFirst=[](Resource*&,Resource*&,int){return false;};
    Check(sw(28,failFirst)==Result::Failed&&live.size()==2,"first allocation failure leaves old pair live");
    auto failSecond=[&](Resource*& x,Resource*&,int){x=allocate();return false;};
    Check(sw(28,failSecond)==Result::Failed&&live.size()==2&&releases==1,"partial failure frees only unsubmitted allocation");
    Check(full==originalFull&&result==originalResult&&format==10&&!spareFull&&!spareResult,
        "failed switch is atomic and retains active format");
    Check(sw(28,create)==Result::Allocated&&format==28&&spareFull==originalFull&&spareResult==originalResult,
        "SDR installs output pair without retiring HDR resources or model");
    const auto plateau=allocations;const auto releasedBefore=releases;
    // Old command lists can remain unreleased throughout every toggle. This
    // output-only transaction must never ask to retire their model/resources.
    for(int i=0;i<10000;++i){int desired=i%2?28:10;
        Check(sw(desired,create)==Result::Cached&&format==desired&&live.size()==4,
            "repeated HDR/SDR toggles use exactly two retained output pairs");
    }
    Check(allocations==plateau&&releases==releasedBefore,"no repeated GPU allocation or early release after warm-up");
    Check(sw(24,create)==Result::Capacity&&format==28&&live.size()==4,"third format is bounded and requires normal rebuild");
    Check(!Compatible(a,Geometry{&device,5120,2160,2970,1253,100,1,2}),"32-bit model precision cannot reuse 16-bit model");
    release(full);release(result);release(spareFull);release(spareResult);
    Check(live.empty(),"all pairs have explicit bank ownership and can retire together");
    using portraitage::Weight;
    Check(Weight(1000,0)==0&&Weight(1000,1001)==0,"invalid or future capture cannot produce face weight");
    Check(Weight(1470,1000)==1,"400ms safe readback plus 70ms CPU detection retains static face");
    Check(Weight(1700,1000)==1&&Weight(1850,1000)==.5f,"old face result fades instead of abrupt disappearance");
    Check(Weight(2000,1000)==0&&Weight(2500,1000)==0,"one-second age bound cannot use an indefinitely stale face");
    float previous=1;for(unsigned age=0;age<=1100;++age){auto weight=Weight(1000+age,1000);
        Check(weight>=0&&weight<=previous,"age weight monotonic and bounded");previous=weight;}
    printf("HDR OUTPUT / FACE AGE CPU: %u checks, %u failures; no GPU device or runtime loaded\n",checks,failures);
    return failures?1:0;
}
