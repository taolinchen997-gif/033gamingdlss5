#include "../src/portrait_detector.h"
#include <d3dcompiler.h>
#include <fstream>
#include <cstdio>
#include <cmath>
#include "portrait_math_cpu_test.h"
static const char* shader=
#include "../src/portrait_input_hlsl.inl"
;
static std::vector<unsigned char> Resize(const std::vector<unsigned char>& p,unsigned w,unsigned h,unsigned nw,unsigned nh){
 std::vector<unsigned char> out(size_t(nw)*nh*4);
 for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x){
  float sx=(x+.5f)*w/nw-.5f,sy=(y+.5f)*h/nh-.5f;int ix=int(floor(sx)),iy=int(floor(sy));float tx=sx-ix,ty=sy-iy;
  for(unsigned c=0;c<4;++c){auto at=[&](int a,int b){return p[(size_t((std::clamp)(b,0,int(h)-1))*w+(std::clamp)(a,0,int(w)-1))*4+c];};
   out[(size_t(y)*nw+x)*4+c]=(unsigned char)((1-ty)*((1-tx)*at(ix,iy)+tx*at(ix+1,iy))+ty*((1-tx)*at(ix,iy+1)+tx*at(ix+1,iy+1))+.5f);
  }
 }return out;
}
int main(int argc,char** argv){if(argc!=2)return 2;
 std::ifstream in(argv[1],std::ios::binary);unsigned w=0,h=0;in.read((char*)&w,4);in.read((char*)&h,4);
 if(!w||!h||w>640||h>640)return 2;std::vector<unsigned char> rgba(size_t(w)*h*4);in.read((char*)rgba.data(),rgba.size());if(!in)return 2;
 unsigned checks=0,failures=0;auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;printf("FAIL %s\n",name);}};
 auto large=portraitdetector::Detect(rgba,w,h,1,1);check(large.count>0,"existing upstream face fixture recognized");if(!large.count)return 1;
 // Compose a face in a wide game-shaped frame. The old longest-edge 320
 // path sees less than 18 pixels, even though the source face is not remote.
 unsigned improved=0;double maxMs=0;
 for(unsigned target:{26u,30u,34u}){
  auto box=large.faces[0].box;unsigned sh=(std::max)(1u,unsigned(target/box[3]));unsigned sw=(std::max)(1u,unsigned(double(sh)*w/h));
  auto resized=Resize(rgba,w,h,sw,sh);std::vector<unsigned char> canvas(size_t(640)*270*4,20);
  if(sw>=640||sh>=270)return 2;
  for(unsigned y=0;y<sh;++y)memcpy(canvas.data()+(size_t(90+y)*640+300-sw/2)*4,resized.data()+size_t(y)*sw*4,sw*4);
  auto now=portraitdetector::Detect(canvas,640,270,1,1);
  auto old=portraitdetector::Detect(Resize(canvas,640,270,320,135),320,135,1,1);
  if(now.count&&!old.count)++improved;maxMs=(std::max)(maxMs,now.ms);
  printf("Wide frame: target face height=%u, before=%u, after=%u, CPU=%.2f ms\n",target,old.count,now.count,now.ms);
 }
 check(improved>=2,"two or more small-face scales missed by old path are now detected");
 check(portraitdetector::Detect(std::vector<unsigned char>(640*270*4,0),640,270,1,1).count==0,"blank wide frame does not fabricate a face");
 check(portraitdetector::Detect({},641,270,1,1).count==0,"oversized detection input rejected");
 portraitmathcpu::Run(check,rgba,w,h);
 for(const char* entry:{"Thumbnail","Enhance"}){ID3DBlob* code=nullptr,*err=nullptr;auto hr=D3DCompile(shader,strlen(shader),entry,nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&err);
  if(err){fwrite(err->GetBufferPointer(),1,err->GetBufferSize(),stderr);err->Release();}if(code)code->Release();check(SUCCEEDED(hr),entry);}
 printf("PORTRAIT CPU: %u checks, %u failures, max inference %.2f ms; CPU inference and HLSL compile only\n",checks,failures,maxMs);return failures?1:0;
}
