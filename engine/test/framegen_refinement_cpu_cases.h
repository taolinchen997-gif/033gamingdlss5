#include "../src/framegen_pacing_policy.h"
#include "../src/framegen_scene.h"
#include "../src/framegen_flow_math.inl"
#include "../src/framegen_static_policy.inl"
static void framegenRefinementChecks(){
    // A 3x presentation slot that overruns its own time must not push the real
    // image another interval into the future; the last image is never dropped.
    fgpace033::Batch batch;batch.Begin(100,105,10,3);
    check(batch.Due(0)==110&&batch.Due(2)==130,"batch retains real deadline");
    check(batch.Stale(0,125,false),"expired first generated image dropped");
    check(!batch.Stale(1,125,false),"second slot remains useful before real deadline");
    check(batch.Stale(1,131,false)&&!batch.Stale(2,900,true),"GPU overrun cannot discard real image");
    check(batch.Due(2)==130,"overrun never moves real deadline");
    batch.Begin(100,150,10,3);
    check(batch.Due(0)==150&&batch.Due(2)==170,"late first completion anchored once");
    check(fgpace033::Interval(48,24,3)==8,"removing NR layer immediately shortens stale pacing average");
    check(fgpace033::Interval(24,48,3)==8,"heavier frame does not extend smoothed interval");
    for(unsigned n:{2u,3u})for(uint64_t step=0;step<40;++step){
        batch.Begin(1000,1003,step,n);
        for(unsigned i=0;i<n;++i){check(batch.Due(i)>=batch.Due(0),"deadlines monotonic");
            check(!batch.Stale(i,UINT64_MAX,i==n-1)||i<n-1,"real remains presentable on arbitrary delay");}
    }
    // Same structure with changed local brightness should beat a wrong patch
    // with similar mean. This represents NR relighting between real frames.
    float a[9]={.1f,.4f,.2f,.8f,.3f,.9f,.5f,.2f,.7f},shift[9],wrong[9],same[9];
    for(int i=0;i<9;++i){shift[i]=a[i]+.2f;wrong[i]=a[8-i];same[i]=a[i];}
    check(FlowBlockCost(a,same)==0,"identical patch has zero error");
    check(FlowBlockCost(a,shift)<FlowBlockCost(a,wrong),"local relighting cannot beat structure matching");
    check(std::abs(FlowBlockCost(a,shift)-.03f)<1e-6,"absolute lightness kept as weak evidence");
    check(FlowConsistency(0)==1&&FlowConsistency(1)==.5f&&FlowConsistency(2)==0,"consistency measured in final pixels");
    // Identical textured frames have exact zero movement even when the local
    // error curve is asymmetric. Previously the zero-motion branch reset the
    // integer vector but the following parabola fit reintroduced movement.
    float stationary[9],leftPatch[9],rightPatch[9];int index=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        auto value=[&](int xx){return .025f*(xx+4)*(xx+4)+.002f*(y+3)*(y+3)*(y+3);};
        stationary[index]=value(x);leftPatch[index]=value(x-1);rightPatch[index]=value(x+1);++index;
    }
    const float zeroCost=FlowBlockCost(stationary,stationary);
    const float leftCost=FlowBlockCost(stationary,leftPatch),rightCost=FlowBlockCost(stationary,rightPatch);
    const float oldOffset=.5f*(leftCost-rightCost)/(leftCost+rightCost-2*zeroCost);
    check(zeroCost==0 && std::abs(oldOffset)>.01f,"asymmetric stationary fixture reproduces false subpixel drift");
    const float stationaryOffset=FlowFitSubpixel(zeroCost,1)?oldOffset:0;
    std::printf("Static flow regression: identical frames old_offset=%.6f flow pixels, gated_offset=%.6f\n",oldOffset,stationaryOffset);
    check(stationaryOffset==0,"identical textured frames preserve exactly zero displacement");
    for(float confidence:{.01f,.051f,.5f,1.0f}){
        check(!FlowFitSubpixel(0,confidence),"zero-motion evidence cannot be overwritten by refinement");
        check(!FlowFitSubpixel(.000009f,confidence),"existing near-static tolerance remains locked");
    }
    check(FlowFitSubpixel(.00001f,1),"motion above existing threshold retains subpixel fit");
    check(FlowFitSubpixel(leftCost,1),"translated detailed patch retains subpixel fit even at exact match");
    check(!FlowFitSubpixel(leftCost,.01f),"ambiguous motion remains rejected");
    const std::string shaderText=kFramegenFlowShader;
    const std::string gate="FlowFitSubpixel(zeroMotionCost,reliable)";
    const auto gate1=shaderText.find(gate),gate2=shaderText.find(gate,gate1+gate.size());
    check(gate1!=std::string::npos && gate2!=std::string::npos,"legacy and pyramid shaders both use the shared static gate");
    auto before=fgscene033::Revision();fgscene033::Enabled(true);fgscene033::Model(17);
    auto active=fgscene033::Revision();check(active>before,"applied NR enables a new FG epoch");
    for(int i=0;i<100;++i){fgscene033::Enabled(true);fgscene033::Model(17);}
    check(fgscene033::Revision()==active,"stable model does not prevent FG warmup");
    fgscene033::Model(18);check(fgscene033::Revision()==active+1,"layer change invalidates one old appearance");
    fgscene033::Enabled(false);check(fgscene033::Revision()==active+2,"F11 off invalidates old NR image");
    for(auto entry:{"pyramidDown","pyramidEstimate"}){
        ID3DBlob *code=nullptr,*error=nullptr;
        const auto hr=D3DCompile(kFramegenFlowShader,std::strlen(kFramegenFlowShader),"033 pyramid",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr)&&error)std::printf("%.*s\n",int(error->GetBufferSize()),static_cast<char*>(error->GetBufferPointer()));
        check(SUCCEEDED(hr),entry);if(code)code->Release();if(error)error->Release();
    }
}
