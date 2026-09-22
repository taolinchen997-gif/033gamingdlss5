#include <Windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include "../runtime/src/scene_grade_shader.h"
#include "../src/nr_guide_normalize_shader.h"
// CPU shader compiler only. No D3D device, SDK runtime, queue or window.
int main(){
    ID3DBlob* normalized=nullptr;ID3DBlob* diagnostic=nullptr;
    const auto conversion=D3DCompile(nrnormalize033::Shader,sizeof(nrnormalize033::Shader)-1,"033_native_guides",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&normalized,&diagnostic);
    if(diagnostic){std::fwrite(diagnostic->GetBufferPointer(),1,diagnostic->GetBufferSize(),stderr);diagnostic->Release();}
    if(FAILED(conversion)||!normalized)return 4;
    std::printf("R11 actual native guide shader cs_5_0: OK bytes=%zu\n",normalized->GetBufferSize());normalized->Release();
    ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
    const HRESULT hr=D3DCompile(k033::scene_grade_source,std::strlen(k033::scene_grade_source),"033_scene_grade",nullptr,nullptr,
        "main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(errors){std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr)||!code)return 1;
    FILE* header=nullptr;if(fopen_s(&header,"033_scene_grade.h","wb")||!header){code->Release();return 2;}
    std::fputs("#pragma once\nstatic const unsigned char k033_scene_grade[]={\n",header);
    auto bytes=static_cast<const unsigned char*>(code->GetBufferPointer());
    for(size_t i=0;i<code->GetBufferSize();++i)std::fprintf(header,"%u,%s",unsigned(bytes[i]),(i%32==31)?"\n":"");
    std::fputs("\n};\n",header);const bool good=std::fclose(header)==0;
    std::printf("S54 scene grade shader cs_5_0: %s bytes=%zu\n",good?"OK":"FAILED",code->GetBufferSize());code->Release();return good?0:3;
}
