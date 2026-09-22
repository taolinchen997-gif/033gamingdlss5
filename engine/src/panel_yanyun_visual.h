#pragma once
// 033 original vector ornament. Uses the existing ImGui draw list and font;
// no image loaders, GPU resources, extra windows or rendering hooks.
namespace studio033::visual {
inline int(__cdecl* artwork)(void**,float*)=nullptr;
inline void init_artwork(){
#if defined(K033_BETA2_RESHADE_HOST)
 if(!artwork){auto module=reshade::internal::get_reshade_module_handle();if(module)artwork=reinterpret_cast<decltype(artwork)>(GetProcAddress(module,"K033_GetYanYunArtwork"));}
#endif
}
// Pixel coordinates refer to the user's approved 1536 x 1024 finished visual.
// Baked labels/values are excluded. The live controls retain their real bindings.
inline bool approved_sprite(ImVec2 origin,float w,float h,float x,float y,float sw,float sh,float alpha=1.f){
 init_artwork();void* data=nullptr;float uv[4]{};if(!artwork||!artwork(&data,uv)||!data)return false;
 ImTextureRef texture;texture._TexData=static_cast<ImTextureData*>(data);
 const auto coord=[&](float px,float py){return ImVec2(uv[0]+(uv[2]-uv[0])*px/1536.f,uv[1]+(uv[3]-uv[1])*py/1024.f);};
 ImGui::GetWindowDrawList()->AddImage(texture,origin,ImVec2(origin.x+w,origin.y+h),coord(x,y),coord(x+sw,y+sh),IM_COL32(255,255,255,int(alpha*255)));return true;
}
inline void approved_frame(){
 const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();const float u=unit(),sx=yyappearance::light?777.f:22.f,sy=59.f,sw=735.f,sh=888.f,b=5.f;
 const float dx[]={0,b*u,size.x-b*u,size.x},dy[]={0,b*u,size.y-b*u,size.y};
 const float xx[]={sx,sx+b,sx+sw-b,sx+sw},yy[]={sy,sy+b,sy+sh-b,sy+sh};
 for(int row=0;row<3;++row)for(int col=0;col<3;++col)if(row!=1||col!=1)
  approved_sprite(ImVec2(p.x+dx[col],p.y+dy[row]),dx[col+1]-dx[col],dy[row+1]-dy[row],xx[col],yy[row],xx[col+1]-xx[col],yy[row+1]-yy[row]);
}
inline void bank_icon(float x,float y,float size,unsigned group){
 if(group){const float sx=group==1?(yyappearance::light?883.f:126.f):(yyappearance::light?1200.f:443.f);
  approved_sprite(ImVec2(x,y),size*1.4f,size,sx,302,group==1?52.f:67.f,40);return;}
 auto* d=ImGui::GetWindowDrawList();const auto c=ImGui::GetColorU32(accent());const ImVec2 p(x+size*.5f,y+size*.5f);
 d->AddCircle(p,size*.43f,c,24,1.2f);d->AddEllipse(p,ImVec2(size*.19f,size*.43f),c,0,24,1.1f);d->AddLine(ImVec2(x+size*.1f,p.y),ImVec2(x+size*.9f,p.y),c,1.1f);
}
inline bool segment(const char* label,bool selected,ImVec2 size){
 const auto p=ImGui::GetCursorScreenPos();auto* d=ImGui::GetWindowDrawList();const float u=unit();
 if(selected){
  const auto top=palette(ImVec4(.29f,.27f,.22f,1),ImVec4(.74f,.63f,.46f,1));
  const auto bottom=palette(ImVec4(.105f,.125f,.13f,1),ImVec4(.57f,.43f,.28f,1));
  const int first=d->VtxBuffer.Size;d->AddRectFilled(p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32_WHITE,14*u);
  for(int i=first;i<d->VtxBuffer.Size;++i){auto& v=d->VtxBuffer[i];const float z=std::clamp((v.pos.y-p.y)/size.y,0.f,1.f);v.col=ImGui::GetColorU32(ImVec4(top.x+(bottom.x-top.x)*z,top.y+(bottom.y-top.y)*z,top.z+(bottom.z-top.z)*z,1));}
  d->AddRect(p,ImVec2(p.x+size.x,p.y+size.y),ImGui::GetColorU32(accent()),14*u);
 }
 for(auto c:{ImGuiCol_Button,ImGuiCol_ButtonActive})ImGui::PushStyleColor(c,ImVec4(0,0,0,0));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
 ImGui::PushStyleColor(ImGuiCol_Text,selected?(yyappearance::light?ImVec4(1,1,1,1):accent()):ImGui::GetStyleColorVec4(ImGuiCol_Text));
 const bool result=ImGui::Button(label,size);ImGui::PopStyleColor(3);ImGui::PopStyleVar();return result;
}
inline ImU32 card_colour(){return ImGui::GetColorU32(palette(ImVec4(27/255.f,32/255.f,34/255.f,1),ImVec4(.98f,.984f,.988f,1)));}
inline void approved_card(ImVec2 p,float w,float h){
 auto* d=ImGui::GetWindowDrawList();const float u=unit();
 d->AddRectFilled(p,ImVec2(p.x+w,p.y+h),card_colour(),7*u);
 d->AddRect(p,ImVec2(p.x+w,p.y+h),ImGui::GetColorU32(ImGuiCol_Border),7*u,0,1);
}
inline void chevron(ImVec2 center,float radius,bool up,ImU32 colour){
 const float sign=up?-1.f:1.f;auto* d=ImGui::GetWindowDrawList();
 d->AddLine(ImVec2(center.x-radius,center.y-sign*radius*.5f),ImVec2(center.x,center.y+sign*radius*.5f),colour,1.1f*unit());
 d->AddLine(ImVec2(center.x,center.y+sign*radius*.5f),ImVec2(center.x+radius,center.y-sign*radius*.5f),colour,1.1f*unit());
}
// S23: the painted header strip (sprite 735 x 120) is no longer drawn; the panel
// header is the vector 033 lettering plus real controls (panel_studio.h Header).
}
