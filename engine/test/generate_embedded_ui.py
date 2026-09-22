from pathlib import Path
root=Path(__file__).resolve().parent.parent
core=root/'third_party/OptiScaler033/OptiScaler'
ops=[]; wrappers=[]; cases=[]
def add(name,signature,args,fields,host,result='void',op=None):
    op=op or name
    ops.append(op)
    tail='return;' if result=='void' else ('return c.result!=0;' if result=='bool' else 'return c.f[0];')
    wrappers.append(f'inline {result} {name}({signature}){{if(!active)'+ ('{ImGui::'+name+'('+args+');return;}' if result=='void' else 'return ImGui::'+name+'('+args+');')+f' Call c{{}};c.op={op};{fields}Send(c);{tail}}}')
    cases.append(f'case {op}: {host}; break;')
def plain(n,sig='',args='',fields='',hostargs=None,result='void',op=None):
    h=f'ImGui::{n}({hostargs if hostargs is not None else args})'
    if result=='bool':h='c.result='+h
    elif result=='float':h='c.f[0]='+h
    add(n,sig,args,fields,h,result,op)
for n in ['EndCombo','EndChild','EndTable','TableNextColumn','TableNextRow','EndDisabled','EndTooltip','PopID','PopItemWidth','TreePop','Spacing','Separator','CloseCurrentPopup']:
    plain(n,result='bool' if n=='TableNextColumn' else 'void')
plain('BeginTooltip',result='bool')
for n in ['Indent','Unindent']:
    plain(n,'float width=0','width','c.f[0]=width;','c.f[0]')
for n in ['PushItemWidth','SetNextItemWidth','SetCursorPosX']:
    plain(n,'float value','value','c.f[0]=value;','c.f[0]')
for n in ['GetWindowWidth','GetCursorPosX','GetFontSize','GetTextLineHeight']:
    plain(n,result='float')
plain('BeginDisabled','bool disabled=true','disabled','c.i[0]=disabled;','c.i[0]!=0')
plain('IsItemHovered','ImGuiHoveredFlags flags=0','flags','c.i[0]=flags;','c.i[0]',result='bool')
plain('IsWindowFocused','ImGuiFocusedFlags flags=0','flags','c.i[0]=flags;','c.i[0]',result='bool')
plain('TableSetColumnIndex','int index','index','c.i[0]=index;','c.i[0]',result='bool')
plain('SameLine','float offset=0,float spacing=-1','offset,spacing','c.f[0]=offset;c.f[1]=spacing;','c.f[0],c.f[1]')
plain('Button','const char* text,const ImVec2& size=ImVec2(0,0)','text,size','c.text=text;c.f[0]=size.x;c.f[1]=size.y;','Label(c.text),ImVec2(c.f[0],c.f[1])',result='bool')
plain('BeginCombo','const char* text,const char* preview,ImGuiComboFlags flags=0','text,preview,flags','c.text=text;c.text2=preview;c.i[0]=flags;','Label(c.text),Label(c.text2,false),c.i[0]',result='bool')
plain('Selectable','const char* text,bool selected=false,ImGuiSelectableFlags flags=0,const ImVec2& size=ImVec2(0,0)','text,selected,flags,size','c.text=text;c.i[0]=selected;c.i[1]=flags;c.f[0]=size.x;c.f[1]=size.y;','Label(c.text),c.i[0]!=0,c.i[1],ImVec2(c.f[0],c.f[1])',result='bool')
plain('Checkbox','const char* text,bool* value','text,value','c.text=text;c.data=value;','Label(c.text),static_cast<bool*>(c.data)',result='bool')
plain('CheckboxFlags','const char* text,unsigned int* value,unsigned int mask','text,value,mask','c.text=text;c.data=value;c.i[0]=mask;','Label(c.text),static_cast<unsigned int*>(c.data),static_cast<unsigned int>(c.i[0])',result='bool')
plain('RadioButton','const char* text,bool activeValue','text,activeValue','c.text=text;c.i[0]=activeValue;','Label(c.text),c.i[0]!=0',result='bool')
plain('RadioButton','const char* text,int* value,int buttonValue','text,value,buttonValue','c.text=text;c.data=value;c.i[0]=buttonValue;','Label(c.text),static_cast<int*>(c.data),c.i[0]',result='bool',op='RadioInt')
plain('TreeNode','const char* text','text','c.text=text;','Label(c.text)',result='bool')
plain('CollapsingHeader','const char* text,ImGuiTreeNodeFlags flags=0','text,flags','c.text=text;c.i[0]=flags;','Label(c.text),c.i[0]',result='bool')
plain('BeginChild','const char* text,const ImVec2& size=ImVec2(0,0),ImGuiChildFlags childFlags=0,ImGuiWindowFlags flags=0','text,size,childFlags,flags','c.text=text;c.f[0]=size.x;c.f[1]=size.y;c.i[0]=childFlags;c.i[1]=flags;','c.text,ImVec2(c.f[0],c.f[1]),c.i[0],c.i[1]',result='bool')
plain('BeginTable','const char* text,int columns,ImGuiTableFlags flags=0,const ImVec2& size=ImVec2(0,0),float innerWidth=0','text,columns,flags,size,innerWidth','c.text=text;c.i[0]=columns;c.i[1]=flags;c.f[0]=size.x;c.f[1]=size.y;c.f[2]=innerWidth;','c.text,c.i[0],c.i[1],ImVec2(c.f[0],c.f[1]),c.f[2]',result='bool')
plain('TableSetupColumn','const char* text,ImGuiTableColumnFlags flags=0,float width=0,ImGuiID id=0','text,flags,width,id','c.text=text;c.i[0]=flags;c.i[1]=id;c.f[0]=width;','Label(c.text),c.i[0],c.f[0],static_cast<ImGuiID>(c.i[1])')
for n,t,lo,hi,fmt in [('SliderInt','int','int','int','%d'),('SliderFloat','float','float','float','%.3f')]:
    plain(n,f'const char* text,{t}* value,{lo} low,{hi} high,const char* format="{fmt}",ImGuiSliderFlags flags=0','text,value,low,high,format,flags','c.text=text;c.text2=format;c.data=value;c.f[0]=float(low);c.f[1]=float(high);c.i[0]=flags;',f'Label(c.text),static_cast<{t}*>(c.data),static_cast<{t}>(c.f[0]),static_cast<{t}>(c.f[1]),c.text2,c.i[0]',result='bool')
plain('InputInt','const char* text,int* value,int step=1,int fast=100,ImGuiInputTextFlags flags=0','text,value,step,fast,flags','c.text=text;c.data=value;c.i[0]=step;c.i[1]=fast;c.i[2]=flags;','Label(c.text),static_cast<int*>(c.data),c.i[0],c.i[1],c.i[2]',result='bool')
plain('InputFloat','const char* text,float* value,float step=0,float fast=0,const char* format="%.3f",ImGuiInputTextFlags flags=0','text,value,step,fast,format,flags','c.text=text;c.data=value;c.f[0]=step;c.f[1]=fast;c.text2=format;c.i[0]=flags;','Label(c.text),static_cast<float*>(c.data),c.f[0],c.f[1],c.text2,c.i[0]',result='bool')
plain('InputScalar','const char* text,ImGuiDataType type,void* value,const void* step=nullptr,const void* fast=nullptr,const char* format=nullptr,ImGuiInputTextFlags flags=0','text,type,value,step,fast,format,flags','c.text=text;c.text2=format;c.data=value;c.extra=step;c.extra2=fast;c.i[0]=type;c.i[1]=flags;','Label(c.text),c.i[0],c.data,c.extra,c.extra2,c.text2,c.i[1]',result='bool')
plain('Combo','const char* text,int* value,const char* const items[],int count,int height=-1','text,value,items,count,height','c.text=text;c.data=value;c.extra=items;c.i[0]=count;c.i[1]=height;','Label(c.text),static_cast<int*>(c.data),static_cast<const char* const*>(c.extra),c.i[0],c.i[1]',result='bool')
plain('ColorEdit3','const char* text,float value[3],ImGuiColorEditFlags flags=0','text,value,flags','c.text=text;c.data=value;c.i[0]=flags;','Label(c.text),static_cast<float*>(c.data),c.i[0]',result='bool')
plain('PushID','const char* text','text','c.text=text;','c.text')
plain('PushID','int id','id','c.i[0]=id;','c.i[0]',op='PushIntID')
plain('Dummy','const ImVec2& size','size','c.f[0]=size.x;c.f[1]=size.y;','ImVec2(c.f[0],c.f[1])')
for n in ['GetContentRegionAvail','GetCursorScreenPos','GetItemRectMin','GetItemRectMax','GetWindowPos','GetWindowSize']:
    ops.append(n);wrappers.append(f'inline ImVec2 {n}(){{if(!active)return ImGui::{n}();Call c{{}};c.op={n};Send(c);return ImVec2(c.f[0],c.f[1]);}}')
    cases.append(f'case {n}:{{auto p=ImGui::{n}();c.f[0]=p.x;c.f[1]=p.y;break;}}')
ops+=['TextPlain','TextMuted','TextWrap','TextColor','Tooltip','SeparatorLabel','TextSize','PushColor','PopColor','PushFontPx','PopFontPx']
for n,op in [('Text','TextPlain'),('TextDisabled','TextMuted'),('TextWrapped','TextWrap'),('SetTooltip','Tooltip')]:
    wrappers.append(f'inline void {n}(const char* format,...){{char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){{ImGui::{n}("%s",b);return;}}Call c{{}};c.op={op};c.text=b;Send(c);}}')
    cases.append(f'case {op}:ImGui::{n}("%s",Label(c.text,false));break;')
wrappers.append('inline void TextColored(const ImVec4& color,const char* format,...){char b[8192];va_list a;va_start(a,format);vsnprintf(b,sizeof(b),format,a);va_end(a);if(!active){ImGui::TextColored(color,"%s",b);return;}Call c{};c.op=TextColor;c.text=b;c.f[0]=color.x;c.f[1]=color.y;c.f[2]=color.z;c.f[3]=color.w;Send(c);}')
cases.append('case TextColor:ImGui::TextColored(ImVec4(c.f[0],c.f[1],c.f[2],c.f[3]),"%s",Label(c.text,false));break;')
wrappers.append('inline void SeparatorText(const char* text){if(!active){ImGui::SeparatorText(text);return;}Call c{};c.op=SeparatorLabel;c.text=text;Send(c);}')
wrappers.append('inline void SeparatorTextEx(ImGuiID id,const char* text,const char* end,float width){if(!active){ImGui::SeparatorTextEx(id,text,end,width);return;}SeparatorText(text);}')
cases.append('case SeparatorLabel:ImGui::SeparatorText(Label(c.text,false));break;')
wrappers.append('inline ImVec2 CalcTextSize(const char* text,const char* end=nullptr,bool hide=false,float wrap=-1){if(!active)return ImGui::CalcTextSize(text,end,hide,wrap);Call c{};c.op=TextSize;c.text=text;c.text2=end;c.i[0]=hide;c.f[0]=wrap;Send(c);return ImVec2(c.f[0],c.f[1]);}')
cases.append('case TextSize:{auto p=ImGui::CalcTextSize(c.text,c.text2,c.i[0]!=0,c.f[0]);c.f[0]=p.x;c.f[1]=p.y;break;}')
plain('SmallButton','const char* text','text','c.text=text;','Label(c.text)',result='bool')
plain('IsItemDeactivatedAfterEdit',result='bool')
plain('PushTextWrapPos','float value=0','value','c.f[0]=value;','c.f[0]')
plain('PopTextWrapPos')
plain('TextUnformatted','const char* text,const char* end=nullptr','text,end','c.text=text;c.text2=end;','c.text,c.text2')
plain('Begin','const char* text,bool* opened=nullptr,ImGuiWindowFlags flags=0','text,opened,flags','c.text=text;c.data=opened;c.i[0]=flags;','Label(c.text),static_cast<bool*>(c.data),c.i[0]',result='bool')
plain('End')
plain('SetNextWindowPos','const ImVec2& pos,ImGuiCond cond=0,const ImVec2& pivot=ImVec2(0,0)','pos,cond,pivot','c.f[0]=pos.x;c.f[1]=pos.y;c.f[2]=pivot.x;c.f[3]=pivot.y;c.i[0]=cond;','ImVec2(c.f[0],c.f[1]),c.i[0],ImVec2(c.f[2],c.f[3])')
plain('SetNextWindowSize','const ImVec2& size,ImGuiCond cond=0','size,cond','c.f[0]=size.x;c.f[1]=size.y;c.i[0]=cond;','ImVec2(c.f[0],c.f[1]),c.i[0]')
plain('SetWindowFocus')
plain('TextLinkOpenURL','const char* text,const char* url=nullptr','text,url','c.text=text;c.text2=url;','Label(c.text),c.text2')
ops.append('PlotSamples')
wrappers.append('inline void PlotLines(const char* text,float(*get)(void*,int),void* data,int count,int offset=0,const char* overlay=nullptr,float low=FLT_MAX,float high=FLT_MAX,ImVec2 size=ImVec2(0,0)){if(!active){ImGui::PlotLines(text,get,data,count,offset,overlay,low,high,size);return;} if(count<0||count>4096)return;float samples[4096];for(int i=0;i<count;++i)samples[i]=get(data,i);Call c{};c.op=PlotSamples;c.text=text;c.text2=overlay;c.data=samples;c.i[0]=count;c.i[1]=offset;c.f[0]=low;c.f[1]=high;c.f[2]=size.x;c.f[3]=size.y;Send(c);}')
cases.append('case PlotSamples:ImGui::PlotLines(c.text,static_cast<float*>(c.data),c.i[0],c.i[1],c.text2,c.f[0],c.f[1],ImVec2(c.f[2],c.f[3]));break;')
ops.append('ViewMetrics')
cases.append('case ViewMetrics:{const auto& io=ImGui::GetIO();c.f[0]=io.DisplaySize.x;c.f[1]=io.DisplaySize.y;c.f[2]=io.DeltaTime;break;}')
# Colours are translated by name, not by ABI-dependent ImGui enum ordinals.
colors=['Text','WindowBg','Border','FrameBg','PlotLines','Button','ButtonHovered','ButtonActive']
wrappers.append('inline void PushStyleColor(ImGuiCol idx,const ImVec4& color){if(!active){ImGui::PushStyleColor(idx,color);return;}Call c{};c.op=PushColor;switch(idx){'+''.join(f'case ImGuiCol_{n}:c.i[0]={i};break;' for i,n in enumerate(colors))+'default:c.i[0]=0;}c.f[0]=color.x;c.f[1]=color.y;c.f[2]=color.z;c.f[3]=color.w;Send(c);}')
wrappers.append('inline void PushStyleColor(ImGuiCol idx,ImU32 color){PushStyleColor(idx,ImGui::ColorConvertU32ToFloat4(color));} inline void PopStyleColor(int count=1){if(!active){ImGui::PopStyleColor(count);return;}Call c{};c.op=PopColor;c.i[0]=count;Send(c);}')
cases.append('case PushColor:{static constexpr ImGuiCol ids[]={'+','.join('ImGuiCol_'+n for n in colors)+'};if(c.i[0]<0||c.i[0]>=8)return 0;ImGui::PushStyleColor(ids[c.i[0]],ImVec4(c.f[0],c.f[1],c.f[2],c.f[3]));break;}')
cases.append('case PopColor:ImGui::PopStyleColor(c.i[0]);break;')
wrappers.append('inline void PushFontSize(float size){if(!active){ImGui::PushFontSize(size);return;}Call c{};c.op=PushFontPx;c.f[0]=size;Send(c);} inline void PopFontSize(){if(!active){ImGui::PopFontSize();return;}Call c{};c.op=PopFontPx;Send(c);} inline void PopFont(){if(!active){ImGui::PopFont();return;}PopFontSize();}')
cases+=['case PushFontPx:ImGui::PushFont(nullptr,c.f[0]);break;','case PopFontPx:ImGui::PopFont();break;']
abi='''// Borrowed POD calls only. The ReShade module owns all ImGui objects and allocation.
#pragma once
#include <cstdint>
namespace ui033abi {
constexpr uint32_t Version=1;
enum Op:uint32_t {'''+','.join(ops)+''',Count};
struct Call {uint32_t size=sizeof(Call),op=0;const char* text=nullptr;const char* text2=nullptr;void* data=nullptr;const void* extra=nullptr;const void* extra2=nullptr;int32_t i[8]={};float f[8]={};int32_t result=0;};
struct Api {uint32_t size=sizeof(Api),version=Version;int(__cdecl* invoke)(Call*)=nullptr;};
inline bool Valid(const Api* p){return p&&p->size==sizeof(Api)&&p->version==Version&&p->invoke;}
using Render=int(__cdecl*)(const Api*);
}
'''
(root/'src/embedded_ui_abi.h').write_text(abi,encoding='utf-8')
wrapper='''#pragma once
#include "../../../../src/embedded_ui_abi.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstdarg>
#include <cstdio>
#include <cfloat>
namespace Ui033 {
using namespace ImGui;
using namespace ui033abi;
inline thread_local const Api* active=nullptr;
inline thread_local ImGuiStyle isolatedStyle;
inline thread_local ImGuiIO isolatedIO;
inline void Send(Call& c){if(active)active->invoke(&c);}
struct Scope{const Api* previous;Scope(const Api* api):previous(active){active=api;}~Scope(){active=previous;}};
inline ImGuiStyle& GetStyle(){return active?isolatedStyle:ImGui::GetStyle();}
inline ImGuiIO& GetIO(){if(!active)return ImGui::GetIO();Call c{};c.op=ViewMetrics;Send(c);isolatedIO.DisplaySize=ImVec2(c.f[0],c.f[1]);isolatedIO.DeltaTime=c.f[2];return isolatedIO;}
'''+ '\n'.join(wrappers)+'\n}\n'
# Op enumerators must not collide with the wrapper function names.
for op in ops:wrapper=wrapper.replace('c.op='+op+';', 'c.op=ui033abi::'+op+';')
(core/'integration/EmbeddedUi033.h').write_text(wrapper,encoding='utf-8')
host='''#pragma once
#include "embedded_ui_abi.h"
#include "embedded_ui_labels.h"
namespace embeddedui {

static int __cdecl Invoke(ui033abi::Call* input){
 if(!input||input->size!=sizeof(*input)||input->op>=ui033abi::Count)return 0;
 auto& c=*input;using namespace ui033abi;
 switch(c.op){
'''+ '\n'.join(cases)+'''
 default:return 0;
 }return 1;
}
#ifndef K033_EMBEDDED_UI_NO_DRAW
static void Draw(){
 const auto* core=rendercore::Api();if(!core)return;
 HMODULE mod=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(core->claim),&mod))return;
 auto render=reinterpret_cast<ui033abi::Render>(GetProcAddress(mod,"K033_RenderEmbeddedControls"));
 if(!render){ImGui::TextDisabled("当前核心尚不支持统一面板，需要配套更新。");return;}
 static const ui033abi::Api api{sizeof(ui033abi::Api),ui033abi::Version,Invoke};
 const int result=render(&api);
 if(result==0)ImGui::TextDisabled("033 扩展功能正在初始化。");
}
#endif
}
'''
(root/'src/embedded_ui.h').write_text(host,encoding='utf-8')
print(f'Generated {len(ops)} borrowed UI operations')
