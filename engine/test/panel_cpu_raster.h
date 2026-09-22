#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Offline evidence only: rasterize ImGui's actual draw triangles + CPU font atlas
// to a local BMP. No HWND/device/backend/graphics API/worker or model invocation.
// The wrapper fixes the working directory to this worktree's engine/build.
inline void panel_cpu_raster(const char* filename,ImGuiWindow* window) {
    const int width=int(window->Size.x),height=int(window->Size.y);
    const int left=int(window->Pos.x),top=int(window->Pos.y);
    std::vector<unsigned char> output(size_t(width)*height*4,255);
    for(size_t p=0;p<output.size();p+=4){output[p]=28;output[p+1]=27;output[p+2]=25;}
    unsigned char* atlas=nullptr;int aw=0,ah=0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&atlas,&aw,&ah);
    auto edge=[](ImVec2 a,ImVec2 b,ImVec2 p){return (p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);};
    for(auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& command:list->CmdBuffer){
        if(command.UserCallback)continue;
        for(unsigned index=0;index+2<command.ElemCount;index+=3){
            const auto& a=list->VtxBuffer[int(command.VtxOffset)+list->IdxBuffer[int(command.IdxOffset+index)]];
            const auto& b=list->VtxBuffer[int(command.VtxOffset)+list->IdxBuffer[int(command.IdxOffset+index+1)]];
            const auto& c=list->VtxBuffer[int(command.VtxOffset)+list->IdxBuffer[int(command.IdxOffset+index+2)]];
            float area=edge(a.pos,b.pos,c.pos);if(std::abs(area)<.0001f)continue;
            int x0=std::max({left,int(std::floor(std::min({a.pos.x,b.pos.x,c.pos.x}))),int(std::ceil(command.ClipRect.x))});
            int y0=std::max({top,int(std::floor(std::min({a.pos.y,b.pos.y,c.pos.y}))),int(std::ceil(command.ClipRect.y))});
            int x1=std::min({left+width,int(std::ceil(std::max({a.pos.x,b.pos.x,c.pos.x}))),int(command.ClipRect.z)});
            int y1=std::min({top+height,int(std::ceil(std::max({a.pos.y,b.pos.y,c.pos.y}))),int(command.ClipRect.w)});
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
                const ImVec2 p(float(x)+.5f,float(y)+.5f);
                float u=edge(b.pos,c.pos,p)/area,v=edge(c.pos,a.pos,p)/area,w=1-u-v;
                if(u<0||v<0||w<0)continue;
                const float fx=(u*a.uv.x+v*b.uv.x+w*c.uv.x)*aw-.5f,fy=(u*a.uv.y+v*b.uv.y+w*c.uv.y)*ah-.5f;
                const int ix=int(std::floor(fx)),iy=int(std::floor(fy));const float sx=fx-ix,sy=fy-iy;float texel[4]{};
                for(int j=0;j<2;++j)for(int i=0;i<2;++i){const auto* sample=atlas+(size_t(std::clamp(iy+j,0,ah-1))*aw+std::clamp(ix+i,0,aw-1))*4;const float weight=(i?sx:1-sx)*(j?sy:1-sy);for(int channel=0;channel<4;++channel)texel[channel]+=sample[channel]*weight;}
                float alpha=(u*((a.col>>24)&255)+v*((b.col>>24)&255)+w*((c.col>>24)&255))/255*texel[3]/255;
                auto* pixel=output.data()+(size_t(height-1-(y-top))*width+(x-left))*4;
                for(int channel=0;channel<3;++channel){
                    int shift=8*channel;float color=(u*((a.col>>shift)&255)+v*((b.col>>shift)&255)+w*((c.col>>shift)&255))*texel[channel]/255;
                    pixel[2-channel]=static_cast<unsigned char>(color*alpha+pixel[2-channel]*(1-alpha));
                }
            }
        }
    }
    unsigned char header[54]={'B','M'};
    auto word=[&](int offset,uint32_t value){for(int i=0;i<4;++i)header[offset+i]=static_cast<unsigned char>(value>>(i*8));};
    word(2,54+uint32_t(output.size()));word(10,54);word(14,40);word(18,uint32_t(width));word(22,uint32_t(height));header[26]=1;header[28]=32;
    FILE* file=nullptr;fopen_s(&file,filename,"wb");CHECK(file!=nullptr);if(!file)return;
    CHECK(std::fwrite(header,1,sizeof(header),file)==sizeof(header));
    CHECK(std::fwrite(output.data(),1,output.size(),file)==output.size());std::fclose(file);
    std::printf("CPU ImGui raster: %s (%d x %d), no GPU\n",filename,width,height);
}
