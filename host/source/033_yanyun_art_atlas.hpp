#pragma once
// CPU font-atlas decoration. The existing ImGui backend owns upload, fencing
// and destruction. Never creates an independent GPU resource or queue.
#include <unordered_map>
#include <algorithm>
#include <cmath>
namespace yanyunart {
inline std::unordered_map<ImFontAtlas*,ImFontAtlasRectId> rectangles;
inline bool Install(ImFontAtlas* atlas,const unsigned char* rgba,int width,int height){
 rectangles.erase(atlas);if(!rgba||width<2||height<2||width>4096||height>4096)return false;
 const int step=1,w=width,h=height;
 atlas->TexPixelsUseColors=true;atlas->TexDesiredFormat=ImTextureFormat_RGBA32;
 ImFontAtlasRect rect;const auto id=atlas->AddCustomRect(w,h,&rect);if(id==ImFontAtlasRectId_Invalid)return false;
 auto* texture=atlas->TexData;if(!texture||texture->BytesPerPixel!=4)return false;
 for(int y=0;y<h;++y){auto* dst=static_cast<unsigned char*>(texture->GetPixelsAt(rect.x,rect.y+y));
  for(int x=0;x<w;++x)for(int c=0;c<4;++c){unsigned value=0;
   for(int j=0;j<step;++j)for(int i=0;i<step;++i)value+=rgba[((y*step+j)*width+x*step+i)*4+c];
   dst[x*4+c]=static_cast<unsigned char>(value/(step*step));}}
 // Key only the known icon rectangles when composing the existing font atlas.
 // The source PNG remains byte-identical; header/typography are never touched.
 // Use the four corners to estimate each icon's original matte, then unmatte
 // fringe pixels. This preserves the original illustration, rather than drawing
 // a replacement icon or loading another texture in the render path.
 if(width==1536&&height==1024){
  struct Crop {int x,y,w,h;};
  const Crop crops[]={{126,302,52,40},{443,302,67,40},{883,302,52,40},{1200,302,67,40},
                      {57,807,20,22},{57,856,20,22},{811,807,20,22},{811,856,20,22}};
  for(auto r:crops){float bg[3]{};
   for(int c=0;c<3;++c)bg[c]=(rgba[(r.y*width+r.x)*4+c]+rgba[(r.y*width+r.x+r.w-1)*4+c]+
       rgba[((r.y+r.h-1)*width+r.x)*4+c]+rgba[((r.y+r.h-1)*width+r.x+r.w-1)*4+c])*.25f;
   for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x){
    auto* dst=static_cast<unsigned char*>(texture->GetPixelsAt(rect.x+x,rect.y+y));
    float distance=0;for(int c=0;c<3;++c)distance=std::max(distance,std::fabs(float(dst[c])-bg[c]));
    const float alpha=std::clamp((distance-13.f)/25.f,0.f,1.f);
    if(alpha>0)for(int c=0;c<3;++c)dst[c]=static_cast<unsigned char>(std::clamp(bg[c]+(dst[c]-bg[c])/alpha,0.f,255.f));
    dst[3]=static_cast<unsigned char>(255*alpha);
   }
  }
 }
 rectangles[atlas]=id;return true;
}
inline int Get(void** data,float* uv){
 if(!data||!uv||!ImGui::GetCurrentContext())return 0;
 auto* atlas=ImGui::GetIO().Fonts;auto found=rectangles.find(atlas);if(found==rectangles.end())return 0;
 ImFontAtlasRect rect;if(!atlas->GetCustomRect(found->second,&rect)||!atlas->TexData)return 0;
 *data=atlas->TexData;uv[0]=rect.uv0.x;uv[1]=rect.uv0.y;uv[2]=rect.uv1.x;uv[3]=rect.uv1.y;return 1;
}
}
