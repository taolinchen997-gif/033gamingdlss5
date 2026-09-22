// Compile every embedded shader in scale.h without creating a D3D12 device.
// This catches HLSL syntax/profile regressions that the normal C++ build cannot see,
// because the add-on normally calls D3DCompile only after a game starts.
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <string>

#include "../src/scale.h"
static void Log(const char *, ...) {}
#include "../src/exposure.h"

static bool CompileOne(const char *name, const char *source, size_t length, const char* entry="main")
{
    FILE* sourceFile=nullptr;const std::string sourcePath=std::string(name)+".hlsl";
    if(fopen_s(&sourceFile,sourcePath.c_str(),"wb")==0){std::fwrite(source,1,length,sourceFile);std::fclose(sourceFile);}
    ID3DBlob *code = nullptr, *errors = nullptr;
    const HRESULT hr = D3DCompile(source, length, name, nullptr, nullptr, entry, "cs_5_0",
                                  D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (errors != nullptr)
    {
        std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
        errors->Release();
    }
    if (code != nullptr) code->Release();
    std::printf("%s: %s\n", name, SUCCEEDED(hr) ? "OK" : "FAILED");
    return SUCCEEDED(hr);
}

int main()
{
    bool ok = true;
    ok &= CompileOne("033_scale", scale::kHlsl, sizeof(scale::kHlsl) - 1);
    ok &= CompileOne("033_resolve", scale::kHlslResolve, sizeof(scale::kHlslResolve) - 1);
    ok &= CompileOne("033_stack", scale::kHlslResolve, sizeof(scale::kHlslResolve) - 1, "stack_main");
    ok &= CompileOne("033_interp", scale::kHlslInterp, sizeof(scale::kHlslInterp) - 1);
    ok &= CompileOne("033_exposure", exposure::kHlsl, sizeof(exposure::kHlsl) - 1);
    return ok ? 0 : 1;
}
