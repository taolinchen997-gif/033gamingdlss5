// CPU-only fixture. No graphics/device libraries, game capture or driver calls.
#include "yolo_mask_decode.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <chrono>
static unsigned checks=0;
static void Check(bool b,const char* text) {++checks;if(!b)throw std::runtime_error(text);}
template<class T> static std::vector<T> Read(const std::string& path,size_t count) {
 std::ifstream f(path,std::ios::binary|std::ios::ate);
 if(!f||f.tellg()!=std::streamoff(count*sizeof(T)))throw std::runtime_error("input length: "+path);
 std::vector<T> v(count);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),v.size()*sizeof(T));
 if(!f)throw std::runtime_error("input read");return v;
}
static void Write(const std::string& path,const std::vector<uint8_t>& v) {
 std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size());
 if(!f)throw std::runtime_error("output write");
}
int main(int argc,char** argv) try {
 if(argc!=2)throw std::runtime_error("one explicit fixture directory required");
 const std::string dir=argv[1];
 auto det=Read<float>(dir+"/detection.f32",116*8400),proto=Read<float>(dir+"/prototype.f32",32*160*160);
 DXL::SemanticMaskSnapshot mask;
 const auto start=std::chrono::steady_clock::now();
 Check(yanyunmask::Decode(det.data(),det.size(),proto.data(),proto.size(),2160,1528,1,100,mask),"real decode");
 const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 Check(mask.Valid(100)&&mask.width==2160&&mask.height==1528,"coherent real snapshot");
 size_t people=0;for(size_t i=0;i<mask.coverage.size();++i){
  Check(mask.groupIds[i]==0||mask.groupIds[i]==255,"only people or background");
  people+=mask.groupIds[i]==0&&mask.coverage[i]<128;
 }
 Check(people>100&&people<mask.coverage.size()/2,"nonempty bounded people in retained fixture");
 Write(dir+"/coverage.u8",mask.coverage);Write(dir+"/group.u8",mask.groupIds);
 const auto original=mask;
 Check(!yanyunmask::Decode(det.data(),det.size()-1,proto.data(),proto.size(),2160,1528,2,101,mask),"bad shape rejected");
 det[0]=std::numeric_limits<float>::quiet_NaN();
 Check(!yanyunmask::Decode(det.data(),det.size(),proto.data(),proto.size(),2160,1528,2,101,mask),"nonfinite rejected");
 Check(mask.coverage==original.coverage&&mask.version==1,"failure preserves complete prior snapshot");
 std::vector<uint8_t> rgba(size_t(640)*453*4,0x55);
 Check(!yanyunmask::Compose(rgba.data(),rgba.size()-1,640*4,640,453,mask,100,1,1,2),"short buffer rejected");
 Check(!yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,601,1,1,2),"stale mask rejected");
 Check(!yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,99,1,1,2),"future timestamp rejected");
 Check(!yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,100,2,1,2),"invalid strength rejected");
 Check(std::all_of(rgba.begin(),rgba.end(),[](uint8_t c){return c==0x55;}),"invalid compose never partially writes");
 Check(yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,100,1,1,2),"compose real mask");
 for(size_t i=0;i<rgba.size()/4;++i)Check(rgba[i*4+1]==255&&rgba[i*4+2]==255&&rgba[i*4+3]==255,"GBA remains exact background");
 Write(dir+"/control-640x453.rgba",rgba);
 Check(yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,100,0,1,8),"equal all-one compose");
 Check(std::all_of(rgba.begin(),rgba.end(),[](uint8_t c){return c==255;}),"all one exact");
 Check(yanyunmask::Compose(rgba.data(),rgba.size(),640*4,640,453,mask,100,1,0,8),"equal all-zero compose");
 Check(std::all_of(rgba.begin(),rgba.end(),[](uint8_t c){return c==0;}),"all zero exact");
 std::cout<<"PASS "<<checks<<" checks; people pixels="<<people<<"; actual DXL decode CPU ms="<<ms<<"; no GPU/game execution\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
