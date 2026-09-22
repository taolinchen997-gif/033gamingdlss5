// 033 independent CPU-only conversion audit. No GPU API/library or inference.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>
#include "readback_pixels.h"

// Exact source from yanyun_semantic_worker.h as inspected 2026-09-21.
inline float LegacyHalf(uint16_t bits){
 const int e=(bits>>10)&31;const unsigned m=bits&1023;
 const float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),e-25);
 return bits&32768?-v:v;
}
// IEEE binary16 -> binary32. Preserve the legacy canonical NaN/sign semantics.
inline float BitHalf(uint16_t half) noexcept {
 const uint32_t sign=uint32_t(half&0x8000u)<<16;
 const uint32_t exponent=(half>>10)&31u;
 uint32_t mantissa=half&1023u, word=sign;
 if(exponent==0){
  if(mantissa){
   uint32_t biased=113;
   while((mantissa&1024u)==0){mantissa<<=1;--biased;}
   word|=(biased<<23)|((mantissa&1023u)<<13);
  }
 }else if(exponent==31){word|=0x7f800000u|(mantissa?0x00400000u:0u);}
 else{word|=((exponent+112u)<<23)|(mantissa<<13);}
 float out;std::memcpy(&out,&word,sizeof(out));return out;
}
inline bool Byte(float v,uint8_t& out) noexcept {
 if(!std::isfinite(v))return false;
 out=uint8_t(std::clamp(v,0.f,1.f)*255.f+.5f);return true;
}
struct Old {static bool Convert(uint16_t h,uint8_t& o){return Byte(LegacyHalf(h),o);}};
struct Bits {static bool Convert(uint16_t h,uint8_t& o){return Byte(BitHalf(h),o);}};
struct Lookup {static bool Convert(uint16_t h,uint8_t& o){const int16_t v=yyworker::HalfReadbackByteTable()[h];if(v<0)return false;o=uint8_t(v);return true;}};

struct Frame {
 std::string name;unsigned width=0,height=0,pitch=0;std::vector<uint16_t> data;
};
template<class Converter> bool ConvertFrame(const Frame& f,std::vector<uint8_t>& rgba){
 if constexpr(std::is_same_v<Converter,Lookup>){
  try{auto out=yyworker::ConvertReadbackPixels({f.data.data(),f.data.size()*2,0,f.pitch,f.width,f.height,yyworker::ReadbackPixelFormat::Float16});rgba=std::move(out.rgba);return true;}
  catch(const std::exception&){return false;}
 }
 const double ratio=640.0/(std::max)(f.width,f.height);
 const unsigned iw=(std::max)(1u,unsigned(f.width*ratio+.5)),ih=(std::max)(1u,unsigned(f.height*ratio+.5));
 rgba.assign(size_t(iw)*ih*4,255);
 // Preserve the actual parent sampling/row-pitch/RGB traversal, including uint64 division.
 for(unsigned y=0;y<ih;++y){const unsigned sy=(std::min)(f.height-1,unsigned(uint64_t(y)*f.height/ih));
  auto* row=reinterpret_cast<const uint8_t*>(f.data.data())+size_t(sy)*f.pitch;
  for(unsigned x=0;x<iw;++x){const unsigned sx=(std::min)(f.width-1,unsigned(uint64_t(x)*f.width/iw));
   for(unsigned c=0;c<3;++c){if(!Converter::Convert(reinterpret_cast<const uint16_t*>(row)[sx*4+c],rgba[(size_t(y)*iw+x)*4+c]))return false;}
  }
 }
 return true;
}
std::vector<uint8_t> Read(const std::filesystem::path& p,size_t size){
 std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(size))throw std::runtime_error("fixture bytes");
 std::vector<uint8_t> out(size);f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size());if(!f)throw std::runtime_error("fixture read");return out;
}
std::array<uint16_t,256> PixelToHalf(){
 std::array<uint16_t,256> out{};
 for(unsigned b=0;b<256;++b){double best=std::numeric_limits<double>::infinity();
  for(unsigned h=0;h<=0x3c00;++h){const double error=std::abs(double(LegacyHalf(uint16_t(h)))-double(b)/255.0);
   if(error<best||(error==best&&(h&1)==0)){best=error;out[b]=uint16_t(h);}}
 }
 return out;
}
Frame Pixels(const std::string& name,const std::vector<uint8_t>& source,unsigned sourceHeight,unsigned height,const std::array<uint16_t,256>& map){
 if(source.size()!=size_t(640)*sourceHeight*4||height>sourceHeight)throw std::runtime_error("source crop");
 Frame f;f.name=name;f.width=640;f.height=height;f.pitch=(640*8+255)&~255u;f.data.resize(size_t(f.pitch/2)*height,0x3555);
 for(unsigned y=0;y<height;++y)for(unsigned x=0;x<f.width;++x)for(unsigned c=0;c<4;++c)f.data[size_t(y)*(f.pitch/2)+x*4+c]=map[source[(size_t(y)*640+x)*4+c]];
 return f;
}
volatile uint64_t sink=0;
struct Timing {double min=0,median=0,p90=0,max=0;};
template<class Converter> Timing Bench(const Frame& f,unsigned samples){
 std::vector<double> ms;
 for(unsigned n=0;n<samples+3;++n){std::vector<uint8_t> out;const auto a=std::chrono::steady_clock::now();const bool ok=ConvertFrame<Converter>(f,out);const auto b=std::chrono::steady_clock::now();
  if(!ok)throw std::runtime_error("unexpected nonfinite benchmark input");
  sink+=out[(size_t(n)*7919)%out.size()];if(n>=3)ms.push_back(std::chrono::duration<double,std::milli>(b-a).count());
 }
 std::sort(ms.begin(),ms.end());return{ms.front(),ms[ms.size()/2],ms[size_t(.9*(ms.size()-1))],ms.back()};
}
void PrintTiming(std::ostream& o,const Timing& t){o<<"{\"min_ms\":"<<t.min<<",\"median_ms\":"<<t.median<<",\"p90_ms\":"<<t.p90<<",\"max_ms\":"<<t.max<<"}";}
unsigned BoundaryChecks(){
 unsigned checks=0;const auto check=[&](bool condition){++checks;if(!condition)throw std::runtime_error("readback boundary mismatch");};
 using namespace yyworker;
 Frame sample;sample.width=5;sample.height=3;sample.pitch=256;sample.data.resize(size_t(sample.pitch/2)*sample.height,0x3555);
 for(unsigned y=0;y<sample.height;++y)for(unsigned x=0;x<sample.width;++x)for(unsigned c=0;c<4;++c)sample.data[size_t(y)*(sample.pitch/2)+x*4+c]=uint16_t(0x3000+unsigned(y*600+x*120+c*20));
 std::vector<uint8_t> expected;check(ConvertFrame<Old>(sample,expected));
 const size_t needed=size_t(sample.height-1)*sample.pitch+sample.width*8;
 std::vector<uint8_t> unaligned(needed+3);std::memcpy(unaligned.data()+3,sample.data.data(),needed);
 ReadbackPixelView view{unaligned.data(),unaligned.size(),3,sample.pitch,sample.width,sample.height,ReadbackPixelFormat::Float16};
 const auto actual=ConvertReadbackPixels(view);check(actual.width==640&&actual.height==384&&actual.rgba==expected);
 const auto rejected=[&](ReadbackPixelView bad){try{ConvertReadbackPixels(bad);return false;}catch(const std::exception&){return true;}};
 auto bad=view;bad.data=nullptr;check(rejected(bad));bad=view;bad.width=0;check(rejected(bad));bad=view;bad.height=0;check(rejected(bad));bad=view;bad.width=8193;check(rejected(bad));
 bad=view;bad.height=8193;check(rejected(bad));bad=view;bad.format=static_cast<ReadbackPixelFormat>(99);check(rejected(bad));bad=view;bad.rowPitch=sample.width*8-1;check(rejected(bad));
 bad=view;bad.offset=bad.bytes+1;check(rejected(bad));bad=view;--bad.bytes;check(rejected(bad));bad=view;bad.rowPitch=(std::numeric_limits<size_t>::max)();check(rejected(bad));
 // Float32 path: padded rows, unaligned offset, clamp boundary neighbours,
 // negative/HDR/subnormal values, and ignored alpha. Build independent bytes.
 const unsigned fw=640,fh=3;const size_t fpitch=fw*16+256,foffset=1;
 std::vector<uint8_t> storage(foffset+(fh-1)*fpitch+fw*16,0xaa),fexpected(size_t(fw)*fh*4,255);
 const float values[]={-100.f,-0.f,0.f,(std::numeric_limits<float>::denorm_min)(),std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f),1.f,1.001f,65504.f};
 for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x)for(unsigned c=0;c<4;++c){const float v=c==3?NAN:values[(y*11+x*3+c)%std::size(values)];std::memcpy(storage.data()+foffset+y*fpitch+x*16+c*4,&v,4);if(c<3)check(Byte(v,fexpected[(size_t(y)*fw+x)*4+c]));}
 ReadbackPixelView fv{storage.data(),storage.size(),foffset,fpitch,fw,fh,ReadbackPixelFormat::Float32};check(ConvertReadbackPixels(fv).rgba==fexpected);
 for(float special:{INFINITY,-INFINITY,NAN,-NAN}){std::memcpy(storage.data()+foffset,&special,4);check(rejected(fv));}
 return checks;
}
int wmain(int argc,wchar_t** argv)try{
 if(argc!=4)throw std::runtime_error("explicit real640x270 RGBA, real640x453 RGBA, output JSON required");
 const auto tableStart=std::chrono::steady_clock::now();const auto& bytes=yyworker::HalfReadbackByteTable();
 const double tableMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tableStart).count();
 unsigned floatBitsMismatch=0,byteMismatch=0,rejectionMismatch=0,finite=0,nan=0,inf=0,subnormal=0,negative=0,signedZero=0;
 for(unsigned h=0;h<65536;++h){const float a=LegacyHalf(uint16_t(h)),b=BitHalf(uint16_t(h));uint32_t wa,wb;std::memcpy(&wa,&a,4);std::memcpy(&wb,&b,4);floatBitsMismatch+=(wa!=wb);
  finite+=std::isfinite(a);nan+=std::isnan(a);inf+=std::isinf(a);subnormal+=((h&0x7c00)==0&&(h&1023)!=0);negative+=bool(h&0x8000);signedZero+=((h&0x7fff)==0);
  uint8_t old=17,bits=17,lut=17;const bool ao=Old::Convert(uint16_t(h),old),bo=Bits::Convert(uint16_t(h),bits),lo=Lookup::Convert(uint16_t(h),lut);
  rejectionMismatch+=(ao!=bo||ao!=lo);byteMismatch+=(ao&&(old!=bits||old!=lut));
 }
 if(floatBitsMismatch||byteMismatch||rejectionMismatch)throw std::runtime_error("exhaustive half mismatch");
 const unsigned boundaryChecks=BoundaryChecks();
 const auto map=PixelToHalf();const auto small=Read(argv[1],640*270*4),large=Read(argv[2],640*453*4);
 std::vector<Frame> frames;frames.push_back(Pixels("real_rgba_derived_half_640x270",small,270,270,map));frames.push_back(Pixels("real_rgba_derived_half_crop_640x449",large,453,449,map));
 Frame stress=frames.back();stress.name="finite_hdr_negative_denormal_640x449";
 const uint16_t values[]={0,0x8000,1,0x03ff,0x0400,0x3555,0x3800,0x3bff,0x3c00,0x4000,0x7bff,0xbc00,0xc000,0x8001,0xfbff};
 for(size_t i=0;i<stress.data.size();++i)stress.data[i]=values[(i*13+i/97)%std::size(values)];frames.push_back(std::move(stress));
 // Every NaN and infinity rejection was checked above. Also check that a frame
 // rejects them in an RGB channel while ignoring alpha exactly like production.
 unsigned frameRejects=0,alphaIgnored=0;
 for(uint16_t special:{uint16_t(0x7c00),uint16_t(0xfc00),uint16_t(0x7e00),uint16_t(0xfe00),uint16_t(0x7c01),uint16_t(0xfc01)}){
  auto f=frames.front();std::vector<uint8_t>a,b,c;f.data[3]=special;const bool alpha=ConvertFrame<Old>(f,a)&&ConvertFrame<Bits>(f,b)&&ConvertFrame<Lookup>(f,c)&&a==b&&b==c;alphaIgnored+=alpha;
  f.data[4]=special;const bool reject=!ConvertFrame<Old>(f,a)&&!ConvertFrame<Bits>(f,b)&&!ConvertFrame<Lookup>(f,c);frameRejects+=reject;if(!alpha||!reject)throw std::runtime_error("frame nonfinite handling mismatch");
 }
 const auto output=std::filesystem::path(argv[3]);if(std::filesystem::exists(output))throw std::runtime_error("refuse overwrite");std::ofstream json(output,std::ios::binary);json<<std::setprecision(12);
 json<<"{\"cpu_only\":true,\"half_patterns\":65536,\"float_bit_mismatches\":"<<floatBitsMismatch<<",\"byte_mismatches\":"<<byteMismatch<<",\"rejection_mismatches\":"<<rejectionMismatch
 <<",\"finite\":"<<finite<<",\"nan\":"<<nan<<",\"infinity\":"<<inf<<",\"subnormal\":"<<subnormal<<",\"negative\":"<<negative<<",\"signed_zeros\":"<<signedZero
 <<",\"nonfinite_rgb_frames_rejected\":"<<frameRejects<<",\"nonfinite_alpha_frames_accepted\":"<<alphaIgnored<<",\"boundary_checks\":"<<boundaryChecks<<",\"lookup_bytes\":"<<sizeof(bytes)<<",\"lookup_initialization_ms\":"<<tableMs<<",\"samples_per_variant\":31,\"frames\":[";
 for(size_t i=0;i<frames.size();++i){const auto& f=frames[i];std::vector<uint8_t>a,b,c;if(!ConvertFrame<Old>(f,a)||!ConvertFrame<Bits>(f,b)||!ConvertFrame<Lookup>(f,c)||a!=b||a!=c)throw std::runtime_error("frame bytes differ");
  const auto old=Bench<Old>(f,31),bits=Bench<Bits>(f,31),lut=Bench<Lookup>(f,31);
  if(i)json<<",";json<<"{\"name\":\""<<f.name<<"\",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"rgb_conversions\":"<<size_t(f.width)*f.height*3<<",\"all_rgba_bytes_equal\":true,\"legacy\":";PrintTiming(json,old);json<<",\"bit_exact\":";PrintTiming(json,bits);json<<",\"lookup\":";PrintTiming(json,lut);json<<"}";
  std::cout<<f.name<<" old_median_ms="<<old.median<<" bit_median_ms="<<bits.median<<" lookup_median_ms="<<lut.median<<" rgba_exact=1\n";
 }
 json<<"],\"passed\":true}\n";if(!json)throw std::runtime_error("result write");
 std::cout<<"PASS exhaustive=65536 finite="<<finite<<" nan="<<nan<<" infinity="<<inf<<" subnormal="<<subnormal<<" float_bits/bytes/rejection_mismatch=0; no GPU/game\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
