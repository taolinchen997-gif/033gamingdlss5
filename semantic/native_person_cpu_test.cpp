#include "ort_person_cpu.h"
#include <iostream>
#include <chrono>
static unsigned checks=0;
static void Check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
static std::vector<uint8_t> Read(const std::filesystem::path& p,size_t n){std::ifstream f(p,std::ios::binary|std::ios::ate);
 if(!f||f.tellg()!=std::streamoff(n))throw std::runtime_error("fixture length");std::vector<uint8_t> v(n);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);if(!f)throw std::runtime_error("fixture read");return v;}
int wmain(int argc,wchar_t** argv)try{
 if(argc!=4)throw std::runtime_error("explicit ORT DLL, model and fixture paths required");
 using namespace yanyunmask;
 CpuPersonModel model(argv[1],argv[2]);const std::filesystem::path dir=argv[3];
 auto rgba=Read(dir/L"source.rgba",size_t(2160)*1528*4),reference=Read(dir/L"coverage.u8",size_t(2160)*1528);
 SourceStamp stamp{12,3,100,5000,2160,1528,false};PersonResult result;std::string error;
 const auto start=std::chrono::steady_clock::now();
 Check(model.Run(rgba.data(),rgba.size(),2160*4,stamp,result,error),error.c_str());
 const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 size_t difference=0;for(size_t i=0;i<reference.size();++i)difference+=std::abs(int(reference[i])-result.mask.coverage[i]);
 Check(double(difference)/reference.size()<.02,"native result agrees with Python CPU reference");
 Check(Usable(result,stamp,5100),"age boundary accepted");Check(!Usable(result,stamp,5101),"over-age rejected");
 Check(!Usable(result,stamp,4999),"future source rejected");
 auto current=stamp;current.stream++;Check(!Usable(result,current,5050),"wrong stream rejected");
 current=stamp;current.generation++;Check(!Usable(result,current,5050),"history/reset generation rejected");
 current=stamp;current.width--;Check(!Usable(result,current,5050),"resize rejected");
 current=stamp;current.frame--;Check(!Usable(result,current,5050),"future frame rejected");
 current=stamp;current.flipY=true;Check(!Usable(result,current,5050),"orientation rejected");
 const auto before=result.mask.coverage;
 Check(!model.Run(rgba.data(),rgba.size()-1,2160*4,stamp,result,error)&&before==result.mask.coverage,"invalid input preserves prior complete result");
 for(unsigned y=0;y<1528/2;++y)for(unsigned x=0;x<2160*4;++x)std::swap(rgba[size_t(y)*2160*4+x],rgba[size_t(1527-y)*2160*4+x]);
 stamp.flipY=true;stamp.frame++;
 Check(model.Run(rgba.data(),rgba.size(),2160*4,stamp,result,error),"inverted source inference");
 Check(Usable(result,stamp,5050),"inverted-source provenance");
 difference=0;for(unsigned y=0;y<1528;++y)for(unsigned x=0;x<2160;++x)difference+=std::abs(int(reference[size_t(y)*2160+x])-result.mask.coverage[size_t(1527-y)*2160+x]);
 Check(double(difference)/reference.size()<.02,"source rows restored after upright inference");
 std::cout<<"PASS native model, orientation and source-age contract: "<<checks<<" checks; CPU preprocess+inference+decode ms="<<elapsed<<"; no GPU/game; not game-frame cost\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
