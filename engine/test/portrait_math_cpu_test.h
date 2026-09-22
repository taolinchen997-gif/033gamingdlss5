#pragma once
#undef min
#undef max
// Run the production region/gain functions as C++ scalars. This deliberately
// does not emulate texture sampling, GPU dispatch, or the neural model.
namespace portraitmathcpu {
inline float PreviousGain(float y,float weight){
 float relief=.065f*std::sqrt((std::max)(y,0.f))*std::pow(std::clamp(1-y,0.f,1.f),3.f);
 return y>.003f?std::clamp((y+weight*relief)/y,1.f,1.16f):1.f;
}

struct float2 {float x,y;float2(float a,float b):x(a),y(b){}};
struct float4 {float2 xy,zw;float z;explicit float4(const float* p):xy(p[0],p[1]),zw(p[2],p[3]),z(p[2]){}};
inline float2 operator+(float2 a,float2 b){return {a.x+b.x,a.y+b.y};}
inline float2 operator-(float2 a,float2 b){return {a.x-b.x,a.y-b.y};}
inline float2 operator*(float2 a,float2 b){return {a.x*b.x,a.y*b.y};}
inline float2 operator*(float2 a,float b){return {a.x*b,a.y*b};}
inline float2 operator/(float2 a,float2 b){return {a.x/b.x,a.y/b.y};}
using std::max;using std::min;using std::clamp;using std::sqrt;using std::pow;
inline float2 max(float2 a,float2 b){return {max(a.x,b.x),max(a.y,b.y)};}
inline float2 max(float2 a,float b){return max(a,float2(b,b));}
inline float length(float2 a){return sqrt(a.x*a.x+a.y*a.y);}
inline float saturate(float a){return clamp(a,0.f,1.f);}
inline float smoothstep(float lo,float hi,float a){float t=saturate((a-lo)/(hi-lo));return t*t*(3-2*t);}
#define K033_PORTRAIT_MATH(...) __VA_ARGS__
#include "../src/portrait_math.inl"
#undef K033_PORTRAIT_MATH

// Baseline counterexample only. The corrected production shader has no colour
// veto: these exact old thresholds explain why an already detected face lost it.
inline float LegacySkinCue(float r,float g,float b){
 float sum=r+g+b;if(sum<.001f)return 0;r/=sum;g/=sum;b/=sum;
 return smoothstep(.31f,.36f,r)*(1-smoothstep(.52f,.60f,r))*
     smoothstep(.23f,.28f,g)*(1-smoothstep(.39f,.43f,g))*smoothstep(-.005f,.025f,g-b);
}
template<class Check> void Run(Check check,const std::vector<unsigned char>& rgba,unsigned w,unsigned h){
 for(unsigned variant=0;variant<3;++variant){
  auto image=rgba;
  if(variant)for(size_t i=0;i<image.size();i+=4){
   float y=.2126f*rgba[i]+.7152f*rgba[i+1]+.0722f*rgba[i+2];
   for(unsigned c=0;c<3;++c){const float factors[]={.82f,1.f,1.18f};
    image[i+c]=static_cast<unsigned char>(clamp(y*(variant==2?factors[c]:1.f),0.f,255.f));}
  }
  auto detected=portraitdetector::Detect(image,w,h,1,1);
  check(detected.count>0,"face fixture remains detected in original/gray/cool light");
  unsigned covered=0,oldZero=0,escaped=0;double oldSum=0,newSum=0,oldGain1=0,gain035=0,gain1=0,previousGain=0;bool bounded=true,features=true,amountZero=true;
  for(unsigned f=0;f<detected.count;++f){const auto& face=detected.faces[f];
   const float4 box(face.box),eye(face.eyes),mouth(face.mouth);
   for(float2 uv:{eye.xy,eye.zw,mouth.xy})features&=PortraitRegionWeight(uv,box,eye,mouth)==0;
  }
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){float mask=0;bool inside=false;
   const float2 uv((x+.5f)/w,(y+.5f)/h);
   for(unsigned f=0;f<detected.count;++f){const auto& face=detected.faces[f];
    mask=max(mask,PortraitRegionWeight(uv,float4(face.box),float4(face.eyes),float4(face.mouth)));
    inside|=uv.x>=face.box[0]&&uv.y>=face.box[1]&&uv.x<=face.box[0]+face.box[2]&&uv.y<=face.box[1]+face.box[3];
   }
   bounded&=std::isfinite(mask)&&mask>=0&&mask<=1;
   if(mask>.0001f){++covered;if(!inside)++escaped;size_t i=(size_t(y)*w+x)*4;
    float cue=LegacySkinCue(image[i]/255.f,image[i+1]/255.f,image[i+2]/255.f);
    oldSum+=mask*cue;newSum+=mask;if(cue==0)++oldZero;
    float luma=(.2126f*image[i]+.7152f*image[i+1]+.0722f*image[i+2])/255.f;
    // Zero local detail isolates the colour veto on this same detected image;
    // these are numerical gain probes, not an emulation of texture sampling.
    amountZero&=PortraitLumaGain(luma,0,mask*0)==1;
    previousGain+=PreviousGain(luma,mask)-1;
    oldGain1+=PortraitLumaGain(luma,0,mask*cue)-1;
    gain035+=PortraitLumaGain(luma,0,mask*.35f)-1;gain1+=PortraitLumaGain(luma,0,mask)-1;}
  }
  check(gain1>previousGain*3,"prelight response exceeds previous subtle gain on same face fixture");
  printf("  Previous v537 mean gain %.6f; prelight mean gain %.6f (same face mask)\n",1+previousGain/max(covered,1u),1+gain1/max(covered,1u));
  check(covered>20,"detected face retains a nonempty region");
  check(escaped==0&&bounded,"region stays finite and inside detected boxes");
  check(features,"eye and mouth centers remain protected");
  check(amountZero&&gain1>gain035&&gain035>0,"same detected image responds to 0/0.35/1 strengths with zero exact");
  if(variant==1)check(oldSum<newSum*.05,"baseline grayscale colour veto suppresses over 95 percent of region weight");
  if(variant==2)check(oldZero>20&&oldSum<newSum*.05&&oldGain1==0,"baseline cool-light veto zeros detected face pixels even at maximum strength");
  printf("FACE REGION %s: detected=%u covered=%u oldZero=%u outside=%u oldWeight=%.5f newWeight=%.5f\n",
    variant==0?"original":variant==1?"gray":"cool",detected.count,covered,oldZero,escaped,oldSum,newSum);
  printf("  Same NEW curve, obsolete colour veto amount=1 %.6f; current mask amount=0/0.35/1 1.000000/%.6f/%.6f\n",
    1+oldGain1/max(covered,1u),1+gain035/max(covered,1u),1+gain1/max(covered,1u));
 }
 check(PortraitMotionWeight(0,0)==0,"no detected region cannot be activated by static content");
 check(PortraitMotionWeight(.8f,0)==.8f&&PortraitMotionWeight(.8f,.045f)==.8f,"matching current content keeps its region weight");
 check(PortraitMotionWeight(.8f,.13f)==0&&PortraitMotionWeight(.8f,1)==0,"changed/occluded content still suppresses the region");
 check(PortraitMotionWeight(.8f,.08f)>0&&PortraitMotionWeight(.8f,.08f)<.8f,"motion rejection remains gradual");
 // Body-colour tests deliberately have NO face regions/landmarks. The same
 // scalar mask function below is compiled into the production HLSL.
 for(const auto& c:std::vector<std::vector<float>>{{.90f,.74f,.67f},{.76f,.53f,.40f},{.28f,.17f,.12f}}){
  const float skin=SkinColourWeight(c[0],c[1],c[2]);
  check(skin>.8f,"light/mid/dark skin-colour swatches receive body processing without a face");
  check(SkinCombinedWeight(skin,0,1,0)==skin,"no face or expired face never disables body colour");
  for(float light:{.2f,.5f,1.f})check(std::abs(SkinColourWeight(c[0]*light,c[1]*light,c[2]*light)-skin)<.0001f,
      "body cue is independent of absolute lighting brightness");
  float y=.2126f*c[0]+.7152f*c[1]+.0722f*c[2];
  check(y*PortraitLumaGain(y,0,skin)>y,"skin outside all face boxes has a positive brightness change");
 }
 for(const auto& c:std::vector<std::vector<float>>{{.6f,.6f,.6f},{0.f,0.f,0.f},{.1f,.2f,.8f},{.1f,.8f,.2f},{.9f,.9f,.1f},{.8f,.05f,.03f}})
  check(SkinColourWeight(c[0],c[1],c[2])<.001f,"neutral/black/blue/green/yellow/saturated red do not receive skin lift");
 check(SkinCombinedWeight(1,0,0,1)==0,"fresh eye/mouth protection also blocks global skin lift");
 check(SkinCombinedWeight(1,0,0,0)==1,"stale mismatched face cannot cut a hole in current body skin");
 check(SkinCombinedWeight(.4f,.8f,1,1)==.8f,"body and face weight use max, never double the gain");
 check(SkinCombinedWeight(0,.8f,1,1)==.8f,"recognized cool-lit face keeps the chromatic fallback override");
 printf("SKIN SCOPE: body-colour swatches processed without any face; landmarks only add protection; same-colour materials remain ambiguous\n");
 bool gainBounds=true,monotonic=true,zeroExact=true;
 for(float y:{0.f,.002f,.01f,.08f,.2f,.5f,.9f,1.f})for(float detail:{-.05f,-.01f,0.f,.01f,.05f}){
  float previous=1;
  for(float amount:{0.f,.1f,.35f,.5f,1.f}){float gain=PortraitLumaGain(y,detail,amount);
   gainBounds&=std::isfinite(gain)&&gain>=1&&gain<=1.48f;
   monotonic&=gain>=previous;previous=gain;if(amount==0)zeroExact&=gain==1;
  }
 }
 check(gainBounds&&zeroExact,"existing gain cap/black floor and zero-strength identity preserved");
 check(monotonic,"strength monotonically increases the bounded luminance adjustment");
 const float low=.295f,high=.305f;
 check(high*PortraitLumaGain(high,.005f,1)>low*PortraitLumaGain(low,-.005f,1),"fine texture polarity survives maximum relief");
 bool outputMonotonic=true;float previousOutput=0;
 for(unsigned i=0;i<=10000;++i){float y=i/10000.f;float output=y*PortraitLumaGain(y,0,1);
  outputMonotonic&=std::isfinite(output)&&output>=previousOutput&&output<=1;previousOutput=output;}
 check(outputMonotonic,"prelight tone curve preserves luminance ordering and highlight range");
 check(PortraitLumaGain(.5f,0,1)>1.2f&&PortraitLumaGain(.9f,0,1)<1.06f,"visible midtone lift with highlight rolloff");
 check(PortraitLumaGain(0,0,1)==1&&PortraitLumaGain(.002f,0,1)==1,"black floor stays untouched");
 printf("FACE GAIN flat y=0.2: strength 0/0.35/1 -> %.6f/%.6f/%.6f; same RGB gain, no hue rotation\n",
  PortraitLumaGain(.2f,0,0),PortraitLumaGain(.2f,0,.35f),PortraitLumaGain(.2f,0,1));
}
}
