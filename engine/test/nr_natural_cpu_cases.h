namespace naturalcpu {
template<class Check>void Run(Check check){
 using namespace stackcpu;
 for(float a:{0.f,.25f,.5f,1.f}){
  float last=-1;
  for(int i=0;i<=16000;++i){float y=i*.001f;
   float gain=NaturalLumaGain(y,a),target=y*gain;
   check(std::isfinite(target)&&target>=last,"natural grade preserves luminance order from black through HDR");
   check(gain>=.3999f&&gain<=1.6001f,"bounded natural grade cannot create excessive exposure");
   if(a==0)check(gain==1.f&&NaturalChromaScale(y,.3f,a)==1.f,"zero natural strength is exact no-op");
   check(NaturalChromaScale(y,1.f,a)==1.f,"skin cue suppresses extra chroma without recolouring skin");
   if(y==0)check(target==0,"true black remains true black");
   if(y>2)check(target>1,"HDR superwhite is not clipped to SDR white");
   last=target;
  }
 }
 // User-visible range requirements, measured on the shared production curve.
 check(NaturalLumaGain(.35f,1.f)>1.10f,"full strength provides meaningful midtone contrast");
 check(NaturalLumaGain(.06f,1.f)<.85f,"full strength offers a usable shadow-depth range");
 check(NaturalChromaScale(.3f,0.f,1.f)>1.20f,"full strength offers a usable non-skin chroma range");
 check(NaturalLumaGain(10.f,1.f)<.80f,"full strength provides meaningful HDR highlight rolloff");
 check(NaturalLumaGain(1.f,1.f)==1.f,"diffuse white anchor preserved");
 check(NaturalChromaScale(0.f,0.f,1.f)==1.f,"dark noise is not saturated by new grade");
}
}
