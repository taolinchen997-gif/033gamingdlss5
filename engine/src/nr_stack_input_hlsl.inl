#define K033_STACK_MATH(...) #__VA_ARGS__
#include "nr_stack_math.inl"
#undef K033_STACK_MATH
R"(
// Same-frame input conditioning for an additional NR model. sRGB model space.
// Luminance remains independent of chroma, so a fringe cannot turn into a dark
// outline merely because one colour channel exceeded the source neighbourhood.
float4 StackSample(Texture2D<float4> image,float2 uv) {
    uint w,h;image.GetDimensions(w,h);
    float4 value=float4(0.f,0.f,0.f,0.f);
    if(w<=dst_size.x && h<=dst_size.y){
        value=image.SampleLevel(lin,uv,0);
    }else{
        float2 p0=(uv-0.5f*inv_dst)*float2(w,h),p1=(uv+0.5f*inv_dst)*float2(w,h);
        float area=max((p1.x-p0.x)*(p1.y-p0.y),1e-6f);
        for(int y=(int)floor(p0.y);y<(int)ceil(p1.y);++y)
        for(int x=(int)floor(p0.x);x<(int)ceil(p1.x);++x){
            float weight=max(0.f,min(p1.x,x+1.f)-max(p0.x,float(x)))*max(0.f,min(p1.y,y+1.f)-max(p0.y,float(y)));
            value+=image.Load(int3(clamp(int2(x,y),int2(0,0),int2(w-1,h-1)),0))*weight;
        }
        value/=area;
    }
    return value;
}
groupshared float4 StackOriginal[100];
groupshared float4 StackPrior[100];
float4 StackInput(uint2 cell){
    uint center=cell.y*10+cell.x;
    float3 original[9],prior[9];
    [unroll]for(int iy=-1;iy<=1;++iy)[unroll]for(int ix=-1;ix<=1;++ix){
        uint at=(cell.y+iy)*10+cell.x+ix;
        original[(iy+1)*3+ix+1]=StackOriginal[at].rgb;
        prior[(iy+1)*3+ix+1]=StackPrior[at].rgb;
    }
    float skin=SkinWeight(max(original[4],0.f));
    return float4(StackCondition(original,prior,skin),StackOriginal[center].a);
}
void StackDispatch(uint2 id,uint2 threadId,uint2 groupId){
    uint lane=threadId.y*8+threadId.x;
    // One 8x8 tile plus a one-pixel halo. 200 texture reads per group instead
    // of 1152; all lanes take the barrier, including partial edge groups.
    for(uint i=lane;i<100;i+=64){
        int2 pixel=int2(groupId*8)+int2(i%10,i/10)-1;
        pixel=clamp(pixel,int2(0,0),int2(dst_size)-1);
        float2 uv=(float2(pixel)+0.5f)*inv_dst;
        StackOriginal[i]=StackSample(mdl_in,uv);StackPrior[i]=StackSample(mdl_out,uv);
    }
    GroupMemoryBarrierWithGroupSync();
    if(any(id>=dst_size))return;
    dst[id]=StackInput(threadId+1);
}
)"
