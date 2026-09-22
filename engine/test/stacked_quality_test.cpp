// Production GPU shaders; synthetic residuals separate compositor invariants
// from NVIDIA's subjective model quality, which still needs game testing.
#define NR_COLOR_TEST_NO_MAIN
#include "nr_color_roundtrip_test.cpp"
#include <limits>
static ComPtr<ID3D12Resource> Upload(Gpu& g,UINT w,UINT h,DXGI_FORMAT format,const void* values,UINT stride){
 auto t=g.texture(w,h,format,D3D12_RESOURCE_STATE_COPY_DEST);auto d=t->GetDesc();
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
 auto u=g.buffer(bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* ptr=nullptr;D3D12_RANGE empty{0,0};OK(u->Map(0,&empty,&ptr));
 for(UINT y=0;y<h;++y)memcpy(static_cast<char*>(ptr)+fp.Footprint.RowPitch*y,static_cast<const char*>(values)+stride*y,stride);u->Unmap(0,nullptr);
 g.begin();D3D12_TEXTURE_COPY_LOCATION a{},b{};a.pResource=t.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;b.pResource=u.Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp;
 g.cmd->CopyTextureRegion(&a,0,0,0,&b,nullptr);scale::Barrier(g.cmd.Get(),t.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);g.wait();return t;
}
static std::vector<Pixel> Refine(Gpu& g,const std::vector<Pixel>& prior,UINT w,UINT h,const std::vector<Pixel>& low,const std::vector<Pixel>& answer,UINT lw,UINT lh,const scale::MotionSharpen* motion=nullptr){
 auto a=Upload(g,w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,prior.data(),w*sizeof(Pixel));
 auto b=Upload(g,lw,lh,DXGI_FORMAT_R32G32B32A32_FLOAT,low.data(),lw*sizeof(Pixel));
 auto c=Upload(g,lw,lh,DXGI_FORMAT_R32G32B32A32_FLOAT,answer.data(),lw*sizeof(Pixel));
 auto out=g.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto d=out->GetDesc();
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.dev->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto read=g.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 g.begin();scale::DispatchResolve(g.b,g.dev.Get(),g.cmd.Get(),a.Get(),b.Get(),c.Get(),d.Format,out.Get(),w,h,1,1,3,1,1,0,nullptr,3,0,0,1,0,203,motion,nullptr,1.0f,0.0f,UINT(16));
 scale::Barrier(g.cmd.Get(),out.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=out.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
 g.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);g.wait();void* ptr=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},empty{0,0};OK(read->Map(0,&range,&ptr));std::vector<Pixel> result(w*h);
 for(UINT y=0;y<h;++y)memcpy(result.data()+w*y,static_cast<char*>(ptr)+fp.Footprint.RowPitch*y,w*sizeof(Pixel));read->Unmap(0,&empty);return result;
}
static float Luma(Pixel p){return .2126f*p.r+.7152f*p.g+.0722f*p.b;}
int main(){try{
 Gpu g;unsigned checks=0,failures=0;auto check=[&](bool ok,const char* s){++checks;if(!ok){++failures;printf("FAIL %s\n",s);}};
 constexpr UINT w=32,h=24,lw=16,lh=12;
 for(float level:{.08f,.25f,.6f,1.f})for(float shift:{-.04f,.04f}){
  std::vector<Pixel> prior(w*h,{.55f*level,.4f*level,.27f*level,.31f});
  for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){float d=(x%2?.008f:-.008f)*level;prior[y*w+x].r+=d;prior[y*w+x].g+=d;prior[y*w+x].b+=d;}
  std::vector<Pixel> low(lw*lh,{.4f,.3f,.2f,1}),answer=low;for(auto& p:answer){p.r+=shift*level;p.g+=shift*level;p.b+=shift*level;}
  auto out=Refine(g,prior,w,h,low,answer,lw,lh);bool same=true;for(size_t i=0;i<out.size();++i)same &= std::abs(out[i].r-prior[i].r-shift*level)<1e-6f && std::abs(out[i].g-prior[i].g-shift*level)<1e-6f && std::abs(out[i].b-prior[i].b-shift*level)<1e-6f && out[i].a==prior[i].a;
  check(same,"low-resolution layer preserves prior fine detail AND full broad neural shading");
 }
 std::vector<Pixel> prior(w*h,{.45f,.34f,.23f,.37f}),low(lw*lh,{.4f,.3f,.2f,1}),answer=low;
 for(UINT y=0;y<lh;++y)for(UINT x=0;x<lw;++x)answer[y*lw+x].r+=(x%2?.07f:-.07f);
 auto detail=Refine(g,prior,w,h,low,answer,lw,lh);float maxEdit=0;bool bounded=true,chroma=true;
 for(size_t i=0;i<detail.size();++i){auto p=detail[i];maxEdit=std::max(maxEdit,std::abs(p.r-prior[i].r));bounded &= std::isfinite(p.r)&&p.r>=0 && p.r<=1&&p.a==prior[i].a;chroma &=std::abs(p.r/p.g-prior[i].r/prior[i].g)<1e-5f;}
 check(maxEdit>1e-5f && bounded,"real non-DC detail edit reaches output but is bounded");check(!chroma,"real neural colour changes must reach the output");
 auto invalid=answer;for(auto& p:invalid)p.r=std::numeric_limits<float>::quiet_NaN();auto safe=Refine(g,prior,w,h,low,invalid,lw,lh);
 check(!memcmp(safe.data(),prior.data(),prior.size()*sizeof(Pixel)),"nonfinite extra model falls back to first pass");
 std::vector<float> velocities(w*h*2,0);scale::MotionSharpen motion;motion.format=DXGI_FORMAT_R32G32_FLOAT;motion.extent[0]=w;motion.extent[1]=h;motion.scale[0]=motion.scale[1]=1;motion.still=motion.moving=0;
 std::vector<Pixel> edited=prior;for(auto& p:edited){p.r+=.03f;p.g+=.02f;}
 auto baseline=g.run(prior,w,h,0,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&edited,1,3,1);
 float previousDifference=100;
 for(float speed:{0.f,12.f,32.f}){
  for(size_t i=0;i<velocities.size();i+=2)velocities[i]=speed;
  auto mv=Upload(g,w,h,motion.format,velocities.data(),w*2*sizeof(float));motion.texture=mv.Get();
  auto out=g.run(prior,w,h,0,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&edited,1,3,1,&motion,nullptr,0);
  check(!memcmp(out.data(),baseline.data(),out.size()*sizeof(Pixel)),"movement must not fade the model into the original source");
  if(speed==0)check(!memcmp(baseline.data(),out.data(),out.size()*sizeof(Pixel)),"static image unchanged by motion guard");
  if(speed==32){check(std::abs(out[0].r-prior[0].r)>.001f,"fast motion retains nonzero neural rendering");auto protectedDetail=Refine(g,prior,w,h,low,answer,lw,lh,&motion);check(!memcmp(protectedDetail.data(),detail.data(),detail.size()*sizeof(Pixel)),"fast motion retains full extra-layer edit");}
 }
 for(float level:{.12f,.4f,1.f}){
  std::vector<Pixel> skin(w*h,{.45f*level,.34f*level,.23f*level,.4f}),dark=skin;for(auto& p:dark){p.r*=.6f;p.g*=.6f;p.b*=.6f;}
  auto off=g.run(skin,w,h,0,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&dark,1,3,1,nullptr,nullptr,0);
  auto on=g.run(skin,w,h,0,1,1,1,0,DXGI_FORMAT_R32G32B32A32_FLOAT,false,&dark,1,3,1,nullptr,nullptr,1);
  check(Luma(on[0])<.94f*Luma(skin[0]) && std::abs(Luma(on[0])-Luma(off[0]))<1e-5f,"skin colour protection preserves meaningful neural shadow edits");
 }
 // Full-resolution composition must reproduce a supplied neural answer, including
 // broad facial shading and colour changes, not merely remain finite.
 auto fullAnswer=prior;
 for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto& p=fullAnswer[y*w+x];p.r+=.03f*float(x)/w;p.g-=.02f*float(y)/h;p.b+=.025f;}
 auto fullOut=Refine(g,prior,w,h,prior,fullAnswer,w,h);float error=0;
 for(size_t i=0;i<fullOut.size();++i)for(int c=0;c<3;++c)error=std::max(error,std::abs((&fullOut[i].r)[c]-(&fullAnswer[i].r)[c]));
 check(error<1e-6f,"full-resolution layer reproduces the full RGB neural answer");
 auto identity=Refine(g,prior,w,h,prior,prior,w,h);
 check(!memcmp(identity.data(),prior.data(),prior.size()*sizeof(Pixel)),"identity remains exact");
 OK(g.dev->GetDeviceRemovedReason());printf("neural contribution and motion: %u checks, %u failures\n",checks,failures);return failures?1:0;
}catch(const std::exception& e){printf("ERROR %s\n",e.what());return 2;}}
