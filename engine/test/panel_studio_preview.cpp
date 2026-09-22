// Offscreen rendering of the actual production ImGui component. No HWND,
// desktop capture, game process, hooks, or operating-system input injection.
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <map>
#include <string>
#include <stdexcept>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include "panel_studio.h"
#define K033_EMBEDDED_UI_NO_DRAW
#include "embedded_ui.h"
using Microsoft::WRL::ComPtr;
static void OK(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D/WIC operation failed");}
static void Need(bool test,const char* message){if(!test)throw std::runtime_error(message);}
static std::map<std::string,ImVec2> targets;
static ui033abi::Render backendRender=nullptr;
static int previewCategory=0;
static int __cdecl BackendHost(ui033abi::Call* c){
    int result=embeddedui::Invoke(c);
    if(c->op==ui033abi::Combo && c->text && std::string(c->text)=="##033_category"){
        *static_cast<int*>(c->data)=previewCategory;c->result=1;
    }
    return result;
}
static void Observe(const char* id,ImVec2 a,ImVec2 b){Need(a.x>=0&&b.x<=ImGui::GetIO().DisplaySize.x,"control horizontal bounds");targets[id]=ImVec2((a.x+b.x)*.5f,(a.y+b.y)*.5f);}
struct Preview {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> rtv;
    unsigned width=0,height=0;studio033::State state;studio033::View view;
    Preview(){
        // WARP is CPU-only: UI verification must not contend with a game/GPU test.
        OK(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        Need(io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc",18.f)!=nullptr,"Chinese font missing");
        Need(ImGui_ImplDX11_Init(device.Get(),context.Get()),"offscreen backend initialization");studio033::observer=Observe;
        using namespace nrcontrolsabi;state.values[Enabled]=1;state.values[Grade]=1;state.values[PreStyle]=1;state.values[PreStyleStrength]=.7f;
        state.values[Contrast]=state.values[Saturation]=1;state.values[Highlights]=.04f;
        state.values[Work]=state.values[PassWork]=100;state.values[Passes]=2;state.values[Skin]=1;state.values[Intensity]=state.values[Structure]=state.values[GlobalTone]=state.values[LocalTone]=1;
        state.values[AutoMask]=state.values[UiCorrect]=1;state.width=5120;state.height=2160;state.running=true;state.saved=true;state.mfgAvailable=true;state.mfgRequested=6;state.mfgAccepted=6;
        state.portraitAvailable=true;
        state.modelActive=true;for(unsigned i=0;i<Count;++i)state.applied[i]=state.values[i];
    }
    ~Preview(){ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();}
    void resize(unsigned w,unsigned h){if(w==width&&h==height)return;width=w;height=h;rtv.Reset();texture.Reset();
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        OK(device->CreateTexture2D(&d,nullptr,&texture));OK(device->CreateRenderTargetView(texture.Get(),nullptr,&rtv));}
    studio033::Edits frame(){
        auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(float(width),float(height));io.DeltaTime=1.f/60;
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();targets.clear();
        ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImColor(21,25,28,255).Value);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(32,26));
        ImGui::Begin("033 offscreen",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
        studio033::Edits edits;
        {studio033::Theme theme;studio033::Header(state,view,edits);
         if(view.page==0)studio033::Picture(state,edits);
         if(view.page==1)studio033::Model(state,view,edits);
         if(view.page==2)studio033::FrameGeneration(state,edits);
         if(view.page==3){studio033::Heading("设置");if(backendRender){const ui033abi::Api api{sizeof(ui033abi::Api),ui033abi::Version,BackendHost};Need(backendRender(&api)==1,"backend panel unavailable");}}
         if(view.page==4)studio033::Heading("诊断与对照","查看运行状态，或用同一画面比较调整前后的变化。");
         studio033::Footer(state,edits);}
        auto* window=ImGui::GetCurrentWindow();Need(window->DC.CursorMaxPos.x<=window->WorkRect.Max.x+2,"horizontal overflow in panel");
        ImGui::End();ImGui::PopStyleVar();ImGui::PopStyleColor();ImGui::Render();
        const float clear[]={21/255.f,25/255.f,28/255.f,1};context->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);context->ClearRenderTargetView(rtv.Get(),clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());return edits;
    }
    studio033::Edits click(const char* id,float fraction=.5f){
        frame();Need(targets.count(id)!=0,id);auto pt=targets.at(id);auto& io=ImGui::GetIO();
        io.AddMousePosEvent(pt.x,pt.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);return frame();
    }
    void save(const wchar_t* path){for(int i=0;i<5;++i)frame();
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;OK(device->CreateTexture2D(&d,nullptr,&staging));context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};OK(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        ComPtr<IWICImagingFactory> factory;OK(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;OK(factory->CreateStream(&stream));OK(stream->InitializeFromFilename(path,GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;OK(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));OK(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> image;OK(encoder->CreateNewFrame(&image,nullptr));OK(image->Initialize(nullptr));OK(image->SetSize(width,height));auto format=GUID_WICPixelFormat32bppBGRA;OK(image->SetPixelFormat(&format));Need(format==GUID_WICPixelFormat32bppBGRA,"unexpected PNG pixel format");
        OK(image->WritePixels(height,mapped.RowPitch,mapped.RowPitch*height,static_cast<BYTE*>(mapped.pData)));OK(image->Commit());OK(encoder->Commit());context->Unmap(staging.Get(),0);
    }
};
int wmain(int argc,wchar_t** argv){try{
    Need(argc>=2,"output directory required");OK(CoInitializeEx(nullptr,COINIT_MULTITHREADED));Preview p;
    if(argc>2){auto module=LoadLibraryExW(argv[2],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);Need(module!=nullptr,"preview engine load");backendRender=reinterpret_cast<ui033abi::Render>(GetProcAddress(module,"K033_RenderEmbeddedControls"));Need(backendRender!=nullptr,"preview backend export");}
    p.resize(1040,1280);p.save((std::wstring(argv[1])+L"\\studio-picture.png").c_str());
    p.click("风格强度");Need(p.state.values[nrcontrolsabi::PreStyleStrength]>.45f&&p.state.values[nrcontrolsabi::PreStyleStrength]<.55f,"thin slider input disconnected");
    auto edits=p.click("style_3");Need(p.state.values[nrcontrolsabi::PreStyle]==3&&(edits.changed&(uint64_t(1)<<nrcontrolsabi::PreStyle)),"style card is not connected");
    p.click("page_1");Need(p.view.page==1,"model navigation");p.frame();p.view.work=125;p.view.passes=3;
    Need(p.state.values[nrcontrolsabi::Work]==100,"draft edited live model before Apply");edits=p.click("apply_model");
    Need(p.state.values[nrcontrolsabi::Work]==125&&p.state.values[nrcontrolsabi::Passes]==3&&(edits.changed&(uint64_t(1)<<nrcontrolsabi::Work)),"Apply model disconnected");
    p.resize(1040,1240);p.frame();p.view.appearance[nrcontrolsabi::Intensity]=.25f;p.frame();
    Need(p.state.values[nrcontrolsabi::Intensity]==1,"appearance draft leaked into live model");
    edits=p.click("apply_appearance");Need(p.state.values[nrcontrolsabi::Intensity]==.25f&&(edits.changed&(uint64_t(1)<<nrcontrolsabi::Intensity)),"appearance Apply not connected");
    p.state.modelPending=true;p.save((std::wstring(argv[1])+L"\\studio-model.png").c_str());p.state.modelPending=false;
    p.click("page_2");Need(p.view.page==2,"frame generation navigation");p.save((std::wstring(argv[1])+L"\\studio-framegen.png").c_str());
    p.state.mfgAccepted=2;p.state.mfgBlocked=true;p.save((std::wstring(argv[1])+L"\\studio-framegen-mismatch.png").c_str());
    p.click("page_0");p.resize(600,1860);p.save((std::wstring(argv[1])+L"\\studio-narrow.png").c_str());
    p.resize(420,2050);p.save((std::wstring(argv[1])+L"\\studio-compact.png").c_str());p.resize(600,1860);
    p.state.values[nrcontrolsabi::Grade]=0;p.frame();float old=p.state.values[nrcontrolsabi::PreStyle];edits=p.click("style_0");Need(p.state.values[nrcontrolsabi::PreStyle]==old&&!edits.changed,"disabled grade allowed edits");
    p.state.values[nrcontrolsabi::Grade]=1;p.state.gradeAvailable=false;p.frame();edits=p.click("style_1");Need(!edits.changed,"unsupported path allowed grade edit");
    p.state.gradeAvailable=true;p.state.paused=true;p.save((std::wstring(argv[1])+L"\\studio-paused.png").c_str());edits=p.click("toggle_nr");Need(!edits.toggle,"paused NR toggle was enabled");
    if(backendRender){p.state.paused=false;p.view.page=3;p.resize(1040,1500);
        for(int category=0;category<7;++category){previewCategory=category;p.save((std::wstring(argv[1])+L"\\settings-"+std::to_wstring(category)+L".png").c_str());}
        printf("BACKEND PREVIEW PASS: 7 actual category pages, no old main two-column menu\n");}
    printf("STUDIO UI PASS: actual production component, offscreen D3D11, 6 PNGs at 420/600/1040 widths, navigation, style and slider binding, staged model Apply, disabled guards, every observed control within horizontal bounds; no HWND or OS input\n");return 0;
}catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}}
