#include "yolo_mask_decode.h"
#include "baseline_decode.h"
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
// S27: the production decoder keeps out-scored people (person-first); the retained
// original is compared with that rule off, which must stay byte-identical.
static std::vector<float> Read(const std::filesystem::path& p,size_t n){std::vector<float> a(n);std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(a.data()),n*sizeof(float));if(!f||f.peek()!=EOF)throw std::runtime_error("tensor size");return a;}
int wmain(int argc,wchar_t** argv)try{
 if(argc!=2)return 2;const auto root=std::filesystem::path(argv[1]);auto det=Read(root/L"detection.f32",116*8400),proto=Read(root/L"prototype.f32",32*160*160);
 unsigned checks=0;
 auto compare=[&](unsigned w,unsigned h){DXL::SemanticMaskSnapshot a,b;const bool x=yanyunmask::Decode(det.data(),det.size(),proto.data(),proto.size(),w,h,3,100,a,nullptr,nullptr,false),y=baseline::Decode(det.data(),det.size(),proto.data(),proto.size(),w,h,3,100,b);
  if(x!=y||a.coverage!=b.coverage||a.groupIds!=b.groupIds)throw std::runtime_error("optimized decoder differs from retained original");++checks;};
 compare(640,453);compare(640,270);compare(2160,1528);
 std::fill(det.begin(),det.end(),0);det[0]=320;det[8400]=320;det[2*8400]=250;det[3*8400]=500;det[4*8400]=.9f;det[84*8400]=1;
 compare(640,270);det[5*8400]=.9f;compare(640,270);det[5*8400]=.91f;compare(640,270);det[4*8400]=.34f;compare(640,270);
 det[8]=NAN;compare(640,270);printf("PASS decoder exact equivalence: %u cases; captured tensors, full/small/ultrawide shapes, ties, other class wins, below threshold, nonfinite rejection\n",checks);return 0;
}catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
