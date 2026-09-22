// Opt-in standalone GPU experiment only. Never enabled by a game config.
static void TwoPassProbe(Gpu& g,ID3D12Resource* depth,ID3D12Resource* motion){
    const unsigned w=hostnr::model_w(),h=hostnr::model_h();
    if(!w||!h||!hostnr::s_feat)throw std::runtime_error("two-pass needs initialized first NR feature");
    const auto& cfg=carrier::cfg;DWORD seh=0;
    g.begin();void* second=nrfwd::create(g.dev.Get(),g.cmd.Get(),w,h,cfg.preset,cfg.intensity,cfg.style,cfg.local_structure,cfg.local_tone,cfg.skin_structure,cfg.auto_mask,cfg.ui_correct,&seh,cfg.global_tone);
    if(!second||seh)throw std::runtime_error("independent second NR feature create failed");g.wait();
    auto result=g.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ComPtr<ID3D12QueryHeap> heap;D3D12_QUERY_HEAP_DESC desc{};desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=3;
    OK(g.dev->CreateQueryHeap(&desc,IID_PPV_ARGS(&heap)));auto times=g.buffer(3*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    UINT64 frequency=0;OK(g.queue->GetTimestampFrequency(&frequency));std::vector<double> firstMs,secondMs;
    auto evaluate=[&](void* feature,ID3D12Resource* input,ID3D12Resource* output,int reset){DWORD exception=0;
        const auto r=nrfwd::evaluate(g.cmd.Get(),feature,input,depth,motion,output,w,h,64,48,0,reset,cfg.intensity,cfg.style,cfg.local_structure,cfg.local_tone,cfg.skin_structure,cfg.auto_mask,64.f,48.f,&exception,nullptr,cfg.global_tone);
        if(r!=NVSDK_NGX_Result_Success || exception)throw std::runtime_error("two-pass evaluate failed");};
    for(unsigned frame=0;frame<20;++frame){
        g.begin();scale::Barrier(g.cmd.Get(),hostnr::s_small,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        evaluate(hostnr::s_feat,hostnr::s_small,hostnr::s_out,frame==0);
        scale::UavBarrier(g.cmd.Get(),hostnr::s_out);g.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        scale::Barrier(g.cmd.Get(),hostnr::s_out,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        evaluate(second,hostnr::s_out,result.Get(),frame==0);scale::UavBarrier(g.cmd.Get(),result.Get());
        g.cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2);
        g.cmd->ResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,3,times.Get(),0);
        scale::Barrier(g.cmd.Get(),hostnr::s_out,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        scale::Barrier(g.cmd.Get(),hostnr::s_small,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);g.wait();
        UINT64* values=nullptr;D3D12_RANGE range{0,3*sizeof(UINT64)},empty{0,0};OK(times->Map(0,&range,reinterpret_cast<void**>(&values)));
        if(frame>=4){firstMs.push_back(double(values[1]-values[0])*1000./frequency);secondMs.push_back(double(values[2]-values[1])*1000./frequency);}times->Unmap(0,&empty);
    }
    auto read=[&](ID3D12Resource* texture){auto rd=texture->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.dev->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto rb=g.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);g.begin();
        scale::Barrier(g.cmd.Get(),texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;g.cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        scale::Barrier(g.cmd.Get(),texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);g.wait();
        void* memory=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},empty{0,0};OK(rb->Map(0,&range,&memory));std::vector<Pixel> pixels(w*h);
        for(unsigned y=0;y<h;++y)memcpy(pixels.data()+y*w,static_cast<char*>(memory)+y*fp.Footprint.RowPitch,w*sizeof(Pixel));rb->Unmap(0,&empty);return pixels;};
    auto first=read(hostnr::s_out),last=read(result.Get());double meanDifference=0;float peak=0;
    for(size_t i=0;i<first.size();++i)for(unsigned c=0;c<3;++c){float a=(&first[i].r)[c],b=(&last[i].r)[c];
        if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("two-pass nonfinite pixels");meanDifference+=std::abs(double(a)-b);peak=std::max(peak,std::abs(b));}
    meanDifference/=first.size()*3;std::sort(firstMs.begin(),firstMs.end());std::sort(secondMs.begin(),secondMs.end());
    printf("TWO_PASS_PROBE independent features, 20 sequential frames, %ux%u, finite output: first_GPU_median=%.3fms second_GPU_median=%.3fms mean_pixel_change=%.7f peak=%.5f PASS (no face/beauty/game-latency claim)\n",w,h,firstMs[firstMs.size()/2],secondMs[secondMs.size()/2],meanDifference,peak);
    nrfwd::release(second,false);OK(g.dev->GetDeviceRemovedReason());
}
