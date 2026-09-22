#include <Windows.h>
#include <cstdio>
#include <stdexcept>
#include <set>
#include <string>
#include "embedded_ui_abi.h"
#include "nr_controls_abi.h"
#include "render_core_abi.h"
static unsigned calls=0;
static int category=0;
static int children=0,tables=0,disabled=0,ids=0,indents=0,fonts=0,colors=0,trees=0;
static std::set<std::string> sections;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int __cdecl Host(ui033abi::Call* c){
 using namespace ui033abi;
 Require(c&&c->size==sizeof(*c)&&c->op<Count,"UI protocol");++calls;
 switch(c->op){
 case ViewMetrics:c->f[0]=1280;c->f[1]=720;c->f[2]=1.f/60;break;
 case GetFontSize:case GetTextLineHeight:c->f[0]=16;break;
 case GetWindowWidth:c->f[0]=600;break;
 case GetContentRegionAvail:case GetWindowSize:c->f[0]=600;c->f[1]=720;break;
 case TextSize:c->f[0]=80;c->f[1]=16;break;
 case Combo:if(c->text&&std::string(c->text)=="##033_category"){*static_cast<int*>(c->data)=category;c->result=1;}break;
 case CollapsingHeader:sections.insert(c->text);c->result=1;break;
 case TreeNode:++trees;c->result=1;break;
 case TreePop:Require(--trees>=0,"tree underflow");break;
 case BeginChild:++children;c->result=1;break;
 case EndChild:Require(--children>=0,"child underflow");break;
 case BeginTable:++tables;c->result=1;break;
 case EndTable:Require(--tables>=0,"table underflow");break;
 case BeginDisabled:++disabled;break;
 case EndDisabled:Require(--disabled>=0,"disabled underflow");break;
 case PushID:case PushIntID:++ids;break;
 case PopID:Require(--ids>=0,"ID underflow");break;
 case Indent:++indents;break;
 case Unindent:Require(--indents>=0,"indent underflow");break;
 case PushColor:++colors;break;
 case PopColor:colors-=c->i[0];Require(colors>=0,"colour underflow");break;
 case PushFontPx:++fonts;break;
 case PopFontPx:Require(--fonts>=0,"font underflow");break;
 default:break; // No simulated setting edits or external actions.
 }return 1;
}
template<class T>static T Get(HMODULE mod,const char* name){auto p=GetProcAddress(mod,name);Require(p!=nullptr,name);return reinterpret_cast<T>(p);}
int wmain(int argc,wchar_t** argv){try{
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 Require(argc==2,"engine path");auto engine=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);Require(engine!=nullptr,"load engine");
 const char* names[]={"K033_ReShadeEntry","K033_AfterUpscale","K033_GetNrControls","K033_GetRenderCore","K033_RenderEmbeddedControls","K033_RenderSupplementalFramegen","K033_GetImageFramegen","K033_GetUniversalFramegen","NVSDK_NGX_D3D12_CreateFeature","NVSDK_NGX_D3D12_EvaluateFeature"};
 for(auto name:names){auto p=Get<FARPROC>(engine,name);HMODULE owner=nullptr;Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(p),&owner)&&owner==engine,"split engine implementation");}
 Require(!GetModuleHandleW(L"renodx-dlss5.addon64")&&!GetModuleHandleW(L"dlss5-033.addon64"),"standalone renderer unexpectedly loaded");
 auto render=Get<ui033abi::Render>(engine,"K033_RenderEmbeddedControls");ui033abi::Api api{sizeof(api),ui033abi::Version,Host};
 auto bad=api;bad.version++;Require(render(&bad)==-1&&calls==0,"wrong version accepted");
 for(int frame=0;frame<7;++frame){category=frame;Require(render(&api)==1,"render embedded controls");Require(!(children||tables||disabled||ids||indents||fonts||colors||trees),"UI stack unbalanced");}
 auto supplement=Get<ui033abi::Render>(engine,"K033_RenderSupplementalFramegen");
 Require(supplement(&bad)==-1,"supplemental wrong version accepted");
 Require(supplement(&api)==1 && !(children||tables||disabled||ids||indents||fonts||colors||trees),"supplemental UI stack unbalanced");
 auto controls=Get<nrcontrolsabi::GetApi>(engine,"K033_GetNrControls")(nrcontrolsabi::Version);Require(controls!=nullptr,"NR controls");nrcontrolsabi::Snapshot snapshot;Require(controls->read(&snapshot)!=0,"NR state read");
 Require(!controls->set(nrcontrolsabi::Count,1)&&!controls->set(nrcontrolsabi::Work,201),"invalid NR input accepted");
 auto after=Get<k033core::AfterUpscale>(engine,"K033_AfterUpscale");Require(after(nullptr)==0,"null frame accepted");
 printf("ONE ENGINE: 10 implementation exports share one module; no standalone NR addon.\n");
 printf("Embedded UI: %u callbacks, %zu sections, 7 category pages, balanced stacks; ABI and invalid-frame checks passed.\n",calls,sections.size());
 for(const auto& name:sections)printf("section: %s\n",name.c_str());
 fflush(stdout);ExitProcess(0);
 }catch(const std::exception& e){fprintf(stderr,"FAIL: %s (Win32=%lu)\n",e.what(),GetLastError());fflush(stderr);ExitProcess(1);}}
