#include "../src/gpu_submission.h"
#include "../src/framegen_policy.h"
#include <cstdio>
struct List {int status=0,closes=0;int Close(){++closes;return status;}};
struct Fence {};
struct Queue {int status=0,executed=0,signals=0;uint64_t last=0;
 void ExecuteCommandLists(unsigned n,List**){executed+=n;}
 int Signal(Fence*,uint64_t value){++signals;last=value;return status;}};
int main(){unsigned checks=0,failures=0;auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}};
 List list;Queue queue;Fence fence;uint64_t serial=0;
 list.status=-1;check(gpu033::Submit(&list,&queue,&fence,serial,&list)==0&&queue.executed==0&&queue.signals==0,"failed close never executes invalid command list");
 list.status=0;queue.status=-1;check(gpu033::Submit(&list,&queue,&fence,serial,&list)==0&&queue.executed==1&&serial==1,"failed signal never reports a submitted output");
 queue.status=0;check(gpu033::Submit(&list,&queue,&fence,serial,&list)==2&&queue.last==2,"retry does not reuse uncertain signal value");
 check(!gpu033::Complete(UINT64_MAX,2),"device removal sentinel is not GPU completion");
 check(!gpu033::Complete(1,2)&&gpu033::Complete(2,2),"event wakeup alone cannot prove completion");
 serial=UINT64_MAX-1;check(!gpu033::Submit(&list,&queue,&fence,serial,&list)&&queue.executed==2,"reserved sentinel cannot be signalled or wrapped");
 framegen033::Pool pool;auto a=pool.Acquire(0);check(bool(a)&&pool.Submit(a,5),"in-flight FG slot tracked");
 check(!pool.Idle(4)&&pool.Idle(5),"FG retirement requires real fence value");
 check(!pool.Idle(UINT64_MAX)&&!pool.Acquire(UINT64_MAX),"lost device cannot recycle FG descriptors or release surfaces");
 auto b=pool.Acquire(5);check(!pool.Submit(b,UINT64_MAX)&&pool.Cancel(b),"sentinel rejected without losing cancellable recording");
 printf("QUEUE SAFETY CPU: %u checks, %u failures; no graphics device or driver calls\n",checks,failures);return failures?1:0;
}
