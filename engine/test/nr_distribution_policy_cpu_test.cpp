// Pure CPU; no Windows, graphics API, model or product DLL execution.
#include <cstdio>
#include <cmath>
#include "../src/nr_distribution_policy.h"
static int checks=0,failed=0;
static void check(bool ok,const char* label){++checks;if(!ok){++failed;std::printf("FAIL %s\n",label);}}
int main(){
 using namespace nrdistribution033;
 int deviceA=0,deviceB=0;
 Geometry stable{&deviceA,3840,2160,2560,1440,10};
 auto wanted=stable;
 for(bool activation:{false,true}){
  check(GeometryMatches(stable,wanted,false,activation),"identical geometry accepted");
  wanted.device=&deviceB;check(!GeometryMatches(stable,wanted,true,activation),"a ready bank from another device must never activate");wanted=stable;
  for(unsigned width:{1920u,2240u,2560u,2880u}){
   wanted.guideWidth=width;wanted.guideHeight=width*9/16;
   check(GeometryMatches(stable,wanted,true,activation),"full-frame NGX model survives render-subrect changes while initializing");
   check(GeometryMatches(stable,wanted,false,activation)==(width==2560),"presentation or guide-based model preserves exact guide extent check");
  }
  wanted=stable;wanted.width++;check(!GeometryMatches(stable,wanted,true,activation),"output width still matters");
  wanted=stable;wanted.height++;check(!GeometryMatches(stable,wanted,true,activation),"output height still matters");
  wanted=stable;wanted.format++;check(!GeometryMatches(stable,wanted,true,activation),"output format still matters");wanted=stable;
 }
 for(unsigned guide:{1920u,2560u,3840u})for(unsigned work:{1344u,1881u,2688u,3840u}){
  check(ExtraMotionFactor(false,work,guide)==1.f,"all NGX layers retain the game's motion encoding");
  check(std::abs(ExtraMotionFactor(true,work,guide)-float(work)/guide)<1e-6f,"RE presentation keeps guide-pixel to model-pixel scaling");
 }
 check(ExtraMotionFactor(true,3840,0)==1.f,"empty guide cannot divide by zero");
 for(auto s:{"Quadro RTX 4000","Quadro RTX 5000","NVIDIA RTX A4000","NVIDIA RTX 5000 Ada Generation","NVIDIA RTX 6000 Ada Generation","NVIDIA GeForce GTX 1080","AMD Radeon RX 7900","Intel Arc A770","NVIDIA GeForce RTX 40900","NVIDIA GeForce RTX 4000"})check(NvidiaGeneration(s)==0,"unrelated or workstation names are not GeForce generations");
 check(NvidiaGeneration("NVIDIA GeForce RTX 2060")==20,"GeForce 20 series");
 check(NvidiaGeneration("NVIDIA GeForce RTX 3080 Ti")==30,"GeForce 30 series Ti");
 check(NvidiaGeneration("NVIDIA GeForce RTX 4090")==40,"GeForce 40 series");
 check(NvidiaGeneration("NVIDIA GeForce RTX 5090")==50,"GeForce 50 series");
 check(NvidiaGeneration("nvidia geforce rtx 4060 Laptop GPU")==40,"case and laptop suffix");
 std::printf("Distribution policy CPU: %d checks, %d failures; no GPU/runtime execution\n",checks,failed);
 return failed?1:0;
}
