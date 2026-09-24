#define NOMINMAX
#include "../src/yanyun_recipe_store.h"
#include "../src/yanyun_exclusive.h"
#include "../src/sr_model_abi.h"
#include "../src/yanyun_tuning.h"
#include "../src/yanyun_hotkey_store.h"
#include "../src/yanyun_fg_check.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <fstream>
#pragma comment(lib,"shell32.lib")
static unsigned checks=0;
static void Check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
struct FixtureLocation {
 static int resolve(std::wstring& folder,DWORD& failure){wchar_t cwd[32768];auto n=GetCurrentDirectoryW(32768,cwd);if(!n||n>=32768)return K033_IO_ERROR;
  folder=std::wstring(cwd)+L"\\recipe-fixture-"+std::to_wstring(GetCurrentProcessId());failure=0;return K033_OK;}
};
int main(){try{
 using namespace yanyunrecipe;
 float values[Count]{};for(unsigned i=0;i<Count;++i)values[i]=definitions[i].minimum;
 for(auto id:{Intensity,Structure,LocalTone,GlobalTone,Contrast,Saturation,WhiteTrim})values[id]=1;
 values[Work]=values[PassWork]=values[PassWork3]=100;values[White]=3.16f;values[Guard]=4;values[DiffuseWhite]=203;
 const auto base=CaptureValues(values);Check(Valid(base),"base capture");
 auto r=MakePreset(base,Restore);Check(Valid(r),"restore valid");Check(Get(r,Character,Intensity)!=Get(r,Scene,Intensity),"groups distinct");
 Check(Get(r,Whole,White)==3.16f&&Get(r,Whole,DiffuseWhite)==203,"HDR preserved");
 auto real=MakePreset(base,Realistic);Check(Get(real,Scene,Warmth)<0&&Get(real,Character,Warmth)<0,"cold preset");
 for(auto kind:{Custom,Realistic,Restore}){r=MakePreset(base,kind);auto code=Encode(r);Check(!code.empty()&&code.size()<MaxCode,"encoded bounded");Recipe copy;
  Check(Decode(code,copy)==DecodeResult::Ok&&!std::memcmp(&r,&copy,sizeof r),"exact round trip");
  for(size_t i=0;i<code.size();++i){auto corrupt=code;corrupt[i]=corrupt[i]=='a'?'b':'a';auto before=copy;
   Check(Decode(corrupt,copy)!=DecodeResult::Ok,"corrupt code rejected");Check(!std::memcmp(&before,&copy,sizeof copy),"failed decode atomic");}
 }
 r=base;for(unsigned g=0;g<Groups;++g)for(unsigned f=0;f<FieldCount;++f){auto bad=r;bad.values[g][f]=std::numeric_limits<float>::quiet_NaN();Check(!Valid(bad),"NaN rejected");
  bad.values[g][f]=definitions[Fields[f]].maximum+1;Check(!Valid(bad),"range rejected");
  bad=r;bad.values[g][f]=definitions[Fields[f]].minimum;Check(Valid(bad),"minimum accepted");
  bad.values[g][f]=definitions[Fields[f]].maximum;Check(Valid(bad),"maximum accepted");
 }
 for(auto forbidden:{Enabled,Hold,Split,ApplyModel,RetiredEffect})Check(Index(forbidden)<0,"no runtime/debug state sharing");
 {// S18: scene layers carry no skin-specific settings; character/global untouched.
  auto skin=MakePreset(base,Realistic);for(int layer=0;layer<3;++layer)for(unsigned g=0;g<Groups;++g){Set(skin,Group(g),LayerId(Skin,layer),2.f);Set(skin,Group(g),LayerId(AutoMask,layer),0.f);}
  const auto before=skin;Check(NeutralSceneSkin(skin)&&Valid(skin),"scene skin normalized and still valid");
  for(int layer=0;layer<3;++layer){
   Check(Get(skin,Scene,LayerId(Skin,layer))==-1.f&&Get(skin,Scene,LayerId(AutoMask,layer))==1.f,"scene skin follows detail strength, auto mask default");
   for(auto g:{Whole,Character})Check(Get(skin,g,LayerId(Skin,layer))==2.f&&Get(skin,g,LayerId(AutoMask,layer))==0.f,"character and global skin settings preserved");
  }
  for(unsigned f=0;f<FieldCount;++f){bool skinField=false;for(int layer=0;layer<3;++layer)skinField|=Fields[f]==LayerId(Skin,layer)||Fields[f]==LayerId(AutoMask,layer);
   if(!skinField)Check(skin.values[Scene][f]==before.values[Scene][f],"no other scene field changes");}
  Check(!NeutralSceneSkin(skin),"normalization is idempotent");
  Recipe decoded;Check(Decode(Encode(skin),decoded)==DecodeResult::Ok&&!std::memcmp(&decoded,&skin,sizeof skin),"normalized recipe round trips");
 }
 Recipe unchanged=base;Check(Decode(std::string(MaxCode+1,'a'),unchanged)==DecodeResult::TooLong,"oversize rejected");
 auto wrong=base;wrong.model[0]='x';Check(!Valid(wrong)&&Encode(wrong).empty(),"foreign model rejected");wrong=base;wrong.version=3;Check(!Valid(wrong),"future schema rejected");
 RequestQueue q;Check(q.Submit(base,true,0)==SubmitResult::RegionsUnavailable&&!q.queued&&q.revision==0,"no fake regional success");
 Check(q.Submit(base,false,0)==SubmitResult::Accepted&&q.revision==1,"whole accepted");
 Check(q.Submit(real,false,0)==SubmitResult::Stale&&q.next.kind==Custom,"stale request cannot overwrite");
 real.regional=0;Check(q.Submit(real,false,1)==SubmitResult::Accepted&&q.revision==2,"latest wins");
 unsigned calls=0;q.Consume([&](const Recipe& received){++calls;Check(!std::memcmp(&real,&received,sizeof real),"whole transaction no mixture");});
 Check(calls==1&&q.consumed==2&&!q.queued,"one drain");Check(!q.Consume([](const Recipe&){}),"no repeated drain");
 Check(q.Submit(r,true,q.revision,true)==SubmitResult::Accepted&&q.next.regional==1,"regional request retains mode");
 {// S32 模式二: a third partition value travels whole; nothing above it is accepted.
  auto shared=r;shared.regional=SharedFirstLayer;Check(Valid(shared),"mode 2 is a valid recipe");
  Recipe copy;Check(Decode(Encode(shared),copy)==DecodeResult::Ok&&copy.regional==SharedFirstLayer&&!std::memcmp(&copy,&shared,sizeof copy),"mode 2 share code round trips");
  auto beyond=shared;beyond.regional=PartitionCount;Check(!Valid(beyond)&&Encode(beyond).empty(),"unknown partition refused");
  Check(q.Submit(r,SharedFirstLayer,q.revision,true)==SubmitResult::Accepted&&q.next.regional==SharedFirstLayer,"mode 2 request keeps mode 2");
  Check(q.Submit(r,PartitionCount,q.revision,true)==SubmitResult::Invalid,"unknown partition request refused");
  Check(q.Submit(r,SharedFirstLayer,q.revision,false)==SubmitResult::RegionsUnavailable,"mode 2 needs character regions too");
  for(unsigned f=0;f<FieldCount;++f){const auto id=Fields[f];
   Check(ChainGroup(WholePicture,id)==Whole&&ChainGroup(SeparateChains,id)==Scene,"whole picture and mode 1 read one group");
   Check(ChainGroup(SharedFirstLayer,id)==(FirstLayerField(id)?Whole:Scene),"mode 2: first layer from the whole column, everything else from the scene column");}
  for(auto id:{Work,Full,Preset,Style,Intensity,Structure,GlobalTone,LocalTone,Skin,AutoMask,UiCorrect})Check(FirstLayerField(id),"first-layer field");
  for(auto id:{Passes,PassWork,PassWork3,L2Intensity,L3Intensity,L2Skin,Blend,Colour,Grade,Exposure,Sharpen,SkinLift,Curve,White})Check(!FirstLayerField(id),"not a first-layer field");
  Check(WorkRendered(WholePicture,Whole)&&!WorkRendered(WholePicture,Character)&&!WorkRendered(WholePicture,Scene),"whole picture renders the whole column's precision");
  Check(!WorkRendered(SeparateChains,Whole)&&WorkRendered(SeparateChains,Character)&&WorkRendered(SeparateChains,Scene),"mode 1 renders character and scene precision");
  Check(WorkRendered(SharedFirstLayer,Whole)&&!WorkRendered(SharedFirstLayer,Character)&&!WorkRendered(SharedFirstLayer,Scene),"mode 2 renders only the shared first layer's precision");
  struct FakeLayer{float skin=2;int autoMask=0;};struct FakeCfg{float skin_structure=2;int auto_mask=0;FakeLayer extra[2];};
  FakeCfg mode1,mode2;NeutralSceneSkin(mode1);NeutralSceneSkin(mode2,true);
  Check(mode1.skin_structure==SceneSkinStructure&&mode1.auto_mask==1&&mode1.extra[0].skin==SceneSkinStructure&&mode1.extra[1].autoMask==1,"mode 1 scene chain neutral on every layer");
  Check(mode2.skin_structure==2&&mode2.auto_mask==0&&mode2.extra[0].skin==SceneSkinStructure&&mode2.extra[1].autoMask==1,"mode 2 keeps the whole column's first-layer skin; layers 2-3 neutral");
 }
 {// S32 超分模型: only 游戏默认 / K / M / L exist; the stored file is exact or ignored.
  using namespace srmodelabi;
  for(uint32_t p=0;p<32;++p)Check(Allowed(p)==(p==0||p==11||p==12||p==13),"only game default, K, L, M");
  Check(K==11&&L==12&&M==13&&SecondGeneration(M)&&SecondGeneration(L)&&!SecondGeneration(K)&&!SecondGeneration(GameDefault),"NGX preset numbers");
  Check(SupportsSecondGeneration(310,5)&&SupportsSecondGeneration(310,6)&&SupportsSecondGeneration(311,0)&&!SupportsSecondGeneration(310,4)&&!SupportsSecondGeneration(3,10),"L/M need DLSS 310.5 or newer");
  // S33 (owner: 「K,M不配性能档位，配质量。 L配平衡」): K and M = Quality 1.5, L = Balanced 1.7, game default untouched.
  Check(LinkedRatio(K)==1.5f&&LinkedRatio(M)==1.5f&&LinkedRatio(L)==1.5f&&LinkedRatio(GameDefault)==0.0f,"model-linked render size (S36: L is Quality too)");
  Check(LinkedMode(1500)==1&&LinkedMode(1700)==2&&LinkedMode(0)==0&&LinkedMode(2000)==0&&LinkedMode(3000)==0,"panel names only the linked modes");
  for(uint32_t p:{K,M,L})Check(LinkedMode(uint32_t(LinkedRatio(p)*1000.0f+0.5f))!=0,"every linked model has a panel name");
  for(uint32_t p=0;p<32;++p)if(!Allowed(p))Check(LinkedRatio(p)==0.0f,"no render size for an unknown model");
  Check(sizeof(srmodelabi::Status)==72&&srmodelabi::Status{}.version==2,"status carries the render-size questions");
  Check(ParseStored("schema=1\npreset=13\n")==M&&ParseStored("schema=1\npreset=11\n")==K&&ParseStored("schema=1\npreset=12\n")==L&&ParseStored("schema=1\npreset=0\n")==GameDefault,"stored choices");
  for(const char* bad:{"","schema=2\npreset=13\n","schema=1\npreset=10\n","schema=1\npreset=13","schema=1\npreset=13\nx","schema=1\npreset=1300\n","schema=1\npreset=\n","schema=1\r\npreset=13\r\n",static_cast<const char*>(nullptr)})
   Check(ParseStored(bad)==GameDefault,"damaged or foreign file keeps the game's own model");
 }
 Store<FixtureLocation> active(true);Check(active.Save(base),"active persisted separately");
 Store<FixtureLocation> store;Check(store.Save(real),"draft save");Recipe loaded;Check(store.Load(loaded)&&!std::memcmp(&loaded,&real,sizeof real),"draft reload");
 Check(active.Load(loaded)&&!std::memcmp(&loaded,&base,sizeof base),"draft does not overwrite active recipe");
 Check(!store.Save(wrong)&&store.Load(loaded)&&!std::memcmp(&loaded,&real,sizeof real),"failed save keeps old file");
 std::wstring folder;DWORD error=0;FixtureLocation::resolve(folder,error);auto lockPath=folder+L"\\recipes.v2.txt.lock";
 HANDLE lock=CreateFileW(lockPath.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 Check(lock!=INVALID_HANDLE_VALUE&&!store.Save(base),"writer contention rejected");CloseHandle(lock);
 Check(store.Load(loaded)&&!std::memcmp(&loaded,&real,sizeof real),"contention preserves bytes");
 {// S23 (user): author-credited share codes and saved records.
  const auto shared=ShareText(real);
  Check(shared.rfind(ShareCredit,0)==0&&shared.find("033YY2|")==std::strlen(ShareCredit),"share code starts with the author credit");
  Recipe got{};Check(DecodeShared(shared,got)==DecodeResult::Ok&&!std::memcmp(&got,&real,sizeof real),"credited share code imports exactly");
  Check(DecodeShared("  "+shared+"\r\n",got)==DecodeResult::Ok&&!std::memcmp(&got,&real,sizeof real),"pasted with spaces and a line break");
  Check(DecodeShared("看这个 "+shared+" 很好看\n第二行",got)==DecodeResult::Ok&&!std::memcmp(&got,&real,sizeof real),"pasted with a comment around it");
  Check(DecodeShared(Encode(base),got)==DecodeResult::Ok&&!std::memcmp(&got,&base,sizeof base),"bare codes of earlier builds still import");
  const auto kept=got;Check(DecodeShared(ShareCredit,got)!=DecodeResult::Ok&&!std::memcmp(&kept,&got,sizeof got),"credit alone imports nothing");
  auto broken=shared;broken.back()=broken.back()=='a'?'b':'a';
  Check(DecodeShared(broken,got)!=DecodeResult::Ok&&!std::memcmp(&kept,&got,sizeof got),"damaged credited code rejected atomically");
  Check(ShareText(wrong).empty(),"no share text for an invalid recipe");
  LibraryEntry e;SetName(e,"  夜战\t方案\r\n ");Check(std::string(e.name)=="夜战 方案","names trimmed, no tabs or line breaks");
  SetName(e,"   ");Check(std::string(e.name)=="方案","empty name falls back");
  std::string longName="a";for(int i=0;i<30;++i)longName+="燕";SetName(e,longName);
  Check(std::strlen(e.name)==46&&longName.compare(0,46,e.name)==0,"long name cut on a character boundary");
  Check(TimeName(9,21,22,5)=="09-21 22:05","time name");
  {std::vector<LibraryEntry> named(2);SetName(named[0],"09-21 22:05");SetName(named[1],"09-21 22:05 (2)");
   Check(UniqueName(named,"09-21 22:05")=="09-21 22:05 (3)"&&UniqueName(named,"09-21 22:06")=="09-21 22:06","same-minute saves get a number");}
  std::vector<LibraryEntry> list(3),parsed;
  SetName(list[0],"写实");list[0].recipe=MakePreset(base,Realistic);SetName(list[1],"还原 夜里");list[1].recipe=MakePreset(base,Restore);
  SetName(list[2],TimeName(9,21,22,5));list[2].recipe=real;list[2].recipe.regional=1;list[2].recipe.fidelity=.37f;list[2].recipe.sceneStrength=.81f;
  Check(Valid(list[2].recipe),"regional record valid");
  auto same=[&](const std::vector<LibraryEntry>& a){if(a.size()!=list.size())return false;
   for(size_t i=0;i<a.size();++i)if(std::strcmp(a[i].name,list[i].name)||std::memcmp(&a[i].recipe,&list[i].recipe,sizeof(Recipe)))return false;return true;};
  ParseLibrary(SerializeLibrary(list),parsed);Check(same(parsed),"records keep names and every saved parameter");
  std::string crlf;for(char c:SerializeLibrary(list)){if(c=='\n')crlf+='\r';crlf+=c;}
  ParseLibrary(crlf,parsed);Check(same(parsed),"records edited in Notepad (CRLF) still read");
  ParseLibrary("no tab here\n"+SerializeLibrary(list)+"坏的\t033YY2|broken\n\n",parsed);Check(same(parsed),"unreadable lines skipped");
  std::vector<LibraryEntry> many(LibraryMax+5,list[0]);ParseLibrary(SerializeLibrary(many),parsed);Check(parsed.size()==LibraryMax,"at most 40 records read");
  LibraryStore<FixtureLocation> library;std::vector<LibraryEntry> stored(1);
  Check(library.Load(stored)==LibraryRead::Missing&&stored.empty(),"no record file yet");
  const auto libraryPath=folder+L"\\recipe-library.v1.txt";
  {std::ofstream big(libraryPath,std::ios::binary);big<<std::string(LibraryBytes+1,'x');}
  Check(library.Load(stored)==LibraryRead::Failed&&stored.empty(),"unreadable record file is not taken for a missing one");
  Check(DeleteFileW(libraryPath.c_str())!=0,"fixture cleanup");
  Check(library.Save(list)&&library.Load(stored)==LibraryRead::Ok&&same(stored),"records saved and read back");
  Check(!library.Save(many)&&library.Load(stored)==LibraryRead::Ok&&same(stored),"over-full list refused, file kept");
  HANDLE held=CreateFileW((libraryPath+L".lock").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Check(held!=INVALID_HANDLE_VALUE&&!library.Save({}),"record writer contention rejected");CloseHandle(held);
  Check(library.Load(stored)==LibraryRead::Ok&&same(stored),"contention keeps the records");
  Check(library.Save({})&&library.Load(stored)==LibraryRead::Ok&&stored.empty(),"deleting the last record leaves an empty list");
  Check(store.Load(loaded)&&!std::memcmp(&loaded,&real,sizeof real),"records never touch the single-slot file");
 }
 {// S35 033特调 (owner 2026-09-24), S36: the owner's 14:42 recipe verbatim, checked against their screenshots.
  namespace tu=yanyuntuning;Recipe preset;auto about=[](float a,float b){return std::fabs(a-b)<.005f;};
  Check(tu::Preset(preset),"033 tuning preset decodes with the production codec");
  Check(Encode(preset)==tu::Code,"033 tuning preset re-encodes to the embedded code byte for byte");
  Check(preset.kind==Custom&&preset.regional==SharedFirstLayer,"033 tuning is 模式二");
  Check(Get(preset,Character,PreStyle)==2&&Get(preset,Character,PreStyleStrength)==1&&about(Get(preset,Character,Exposure),.26f)&&about(Get(preset,Character,Contrast),.91f)&&
   about(Get(preset,Character,Saturation),1.02f)&&about(Get(preset,Character,Warmth),.08f)&&about(Get(preset,Character,Tint),0)&&about(Get(preset,Character,Highlights),0),
   "前置 person: 柔和 1.00 / 0.26 / 0.91 / 1.02 / 0.08 / -0.00 / 0.00");
  Check(Get(preset,Scene,PreStyle)==1&&Get(preset,Scene,PreStyleStrength)==1&&about(Get(preset,Scene,Exposure),.25f)&&about(Get(preset,Scene,Contrast),1.01f)&&
   about(Get(preset,Scene,Saturation),.87f)&&about(Get(preset,Scene,Warmth),-.09f)&&about(Get(preset,Scene,Tint),0)&&about(Get(preset,Scene,Highlights),.04f),
   "前置 scene: 自然 1.00 / 0.25 / 1.01 / 0.87 / -0.09 / 0.00 / 0.04");
  Check(Get(preset,Whole,Work)==100&&Get(preset,Scene,PassWork)==100&&Get(preset,Scene,PassWork3)==100,"SR: first layer 100 % (S36), scene layers 2 and 3 100 %");
  Check(Get(preset,Whole,Style)==1&&Get(preset,Whole,Intensity)==2&&Get(preset,Whole,Structure)==1&&Get(preset,Whole,LocalTone)==2&&
   Get(preset,Whole,GlobalTone)==2&&Get(preset,Whole,Skin)==-1&&Get(preset,Whole,AutoMask)==1,"NR first layer: 自然 2.00 / 1.00 / 2.00 / 2.00 / -1.00, model skin protection on");
  Check(Get(preset,Character,Blend)==95&&Get(preset,Character,Colour)==1,"NR person: blend 95 %, model colour 1.00");
  Check(Get(preset,Scene,Passes)==2,"NR scene: layer 2 on, layer 3 off");
  Check(tu::SrModelFor(40)==srmodelabi::L&&tu::SrModelFor(50)==srmodelabi::L&&tu::SrModelFor(0)==srmodelabi::L&&tu::SrModelFor(30)==srmodelabi::K&&tu::SrModelFor(20)==srmodelabi::K,
   "SR model L; K on RTX 20/30 as the owner's labels advise");
  Check(tu::Multiplier==6&&tu::ValidMultiplier(0)&&tu::ValidMultiplier(2)&&tu::ValidMultiplier(6)&&!tu::ValidMultiplier(1)&&!tu::ValidMultiplier(7),"6x; follow-game and 2-6 kept");
  tu::Before before;before.recipe=real;before.srModel=srmodelabi::M;before.multiplier=4;before.frameGeneration=true;
  const std::string text=tu::Serialize(before);tu::Before back;
  Check(!text.empty()&&tu::Parse(text,back)&&Encode(back.recipe)==Encode(real)&&back.srModel==srmodelabi::M&&back.multiplier==4&&back.frameGeneration,"before record round trip");
  tu::Before invalid=before;invalid.multiplier=1;Check(tu::Serialize(invalid).empty(),"an invalid before record is never written");
  auto swap=[&](const char* from,const char* to){std::string s=text;const auto at=s.find(from);Check(at!=std::string::npos,"fixture key");s.replace(at,std::strlen(from),to);return s;};
  for(const std::string& broken:{std::string(),text.substr(0,text.size()-1),swap("schema=1","schema=2"),swap("multiplier=4","multiplier=7"),swap("srmodel=13","srmodel=5"),
   swap("fg=1","fg=2"),text+"fg=1\n",text+"extra=1\n",swap("recipe=033YY2","recipe=033YY1")})Check(!tu::Parse(broken,back),"damaged before record refused");
  tu::Store<FixtureLocation> tuning;tu::Before kept;
  Check(tuning.Exists()==tu::Presence::No&&tuning.Load(kept)==tu::Read::Missing,"no 033 tuning record yet: the switch is off");
  Check(tuning.Save(before)&&tuning.Exists()==tu::Presence::Yes&&tuning.Load(kept)==tu::Read::Ok&&Encode(kept.recipe)==Encode(real)&&kept.multiplier==4,"on: the before record is kept");
  Check(tuning.Remove()&&tuning.Exists()==tu::Presence::No&&tuning.Remove(),"off: the record goes (removing twice is fine)");
  {std::ofstream damaged(folder+L"\\033-tuning-before.v1.txt",std::ios::binary);damaged<<"schema=1\n";}
  Check(tuning.Exists()==tu::Presence::Yes&&tuning.Load(kept)==tu::Read::Unreadable,"a damaged record still shows on and reads as unreadable");
  Check(tuning.Remove(),"fixture cleanup");
 }
 {// S37 (owner 2026-09-24: 「另外快捷键能改」): the NR key file and the keys offered.
  namespace hk=hotkey033;
  Check(hk::Default==0x7A&&!std::strcmp(hk::Name(0x7A),"F11"),"F11 by default");
  // S39 (owner: 「F11的热键修改有问题，有些键不能用」): letters, digits, symbols, F9, F10, arrows, the
  // keypad and the middle / side mouse buttons are offered; Pause (never seen held) no longer is.
  for(int vk:{0x78,0x79,0x41,0x5A,0x30,0x39,0xC0,0xBF,0xDC,0x25,0x28,0x2D,0x91,0x60,0x6F,0x6E,0x5D,0x04,0x05,0x06})Check(hk::Allowed(vk),"S39 keys offered");
  for(int vk:{0x24,0x08,0x1B,0x10,0x11,0x12,0x0D,0x20,0x09,0x2C,0x13,0x14,0x90,0x5B,0x5C,0xA0,0xA5,0x01,0x02,0x0C})
   Check(!hk::Allowed(vk),"Home, Backspace, Esc, modifiers, Enter, Space, Tab, Print Screen, Pause, Caps / Num Lock, Win, left / right click and Clear are never offered");
  for(int vk:{0x24,0x08,0x0D,0x20,0x09,0x14,0x90,0x2C,0x13,0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0x5B,0x5C})Check(hk::Refused(vk)!=nullptr,"a refused key says why");
  for(const auto& k:hk::Keys)Check(!hk::Refused(k.vk)&&k.name&&std::strlen(k.name)<16,"offered keys are not refused and their names fit the panel");
  for(size_t i=0;i<sizeof(hk::Keys)/sizeof(hk::Keys[0]);++i)for(size_t j=i+1;j<sizeof(hk::Keys)/sizeof(hk::Keys[0]);++j)Check(hk::Keys[i].vk!=hk::Keys[j].vk,"each key listed once");
  Check(sizeof(hk::Keys)/sizeof(hk::Keys[0])==88,"88 keys and buttons offered (S37: 26)");
  Check(hk::Note(0x78)&&hk::Note(0x41)&&hk::Note(0xBF)&&hk::Note(0x60)&&!hk::Note(0x7A)&&!hk::Note(0x05)&&!hk::Note(0x25),"notes for F9, typing keys and the keypad only");
  Check(hk::Fires(0x7A,true,false,true,false,false),"a fresh press with the game in front switches");
  Check(!hk::Fires(0x7A,true,true,true,false,false),"holding the key does not repeat");
  Check(!hk::Fires(0x7A,false,false,true,false,false),"no press, no switch");
  Check(!hk::Fires(0x4E,true,false,false,false,false),"a letter typed in another program does not switch");
  Check(!hk::Fires(0x4E,true,false,true,false,true),"waiting for a key or typing on the panel does not switch");
  Check(hk::Fires(0x7A,true,false,true,true,false),"F11 with Shift still switches");
  Check(!hk::Fires(0x78,true,false,true,true,false)&&hk::Fires(0x78,true,false,true,false,false),"F9 switches alone, not with Shift (the monitor)");
  for(const auto& k:hk::Keys)Check(hk::Allowed(k.vk)&&hk::ParseStored(("schema=1\nkey="+std::to_string(k.vk)+"\n").c_str())==k.vk,"every offered key round-trips through the file");
  for(const char* bad:{"","schema=1\nkey=19\n","schema=1\nkey=36\n","schema=1\nkey=122","schema=2\nkey=122\n","schema=1\nkey=122x\n","schema=1\nkey=\n","schema=1\nkey=99999\n","key=117\n"})
   Check(hk::ParseStored(bad)==hk::Default,"a damaged or unoffered key file keeps F11 (S37's unusable Pause too)");
  wchar_t kept[32768]{};const DWORD had=GetEnvironmentVariableW(L"LOCALAPPDATA",kept,32768);
  const std::wstring local=folder+L"\\hotkey-local";CreateDirectoryW(local.c_str(),nullptr);Check(SetEnvironmentVariableW(L"LOCALAPPDATA",local.c_str())!=0,"fixture LOCALAPPDATA");
  Check(hk::Load()==hk::Default,"no key file: F11");
  Check(hk::Save(0x05)&&hk::Load()==0x05,"a side mouse button kept for the next start");
  Check(hk::Save(0x77)&&hk::Load()==0x77,"F8 kept for the next start");
  Check(!hk::Save(0x13)&&hk::Load()==0x77,"Pause refused, F8 kept");
  Check(DeleteFileW((local+L"\\033YanYunRuntime\\hotkey.cfg").c_str())!=0,"fixture cleanup");
  RemoveDirectoryW((local+L"\\033YanYunRuntime").c_str());RemoveDirectoryW(local.c_str());
  SetEnvironmentVariableW(L"LOCALAPPDATA",had&&had<32768?kept:nullptr);
 }
 {// S38: the read-only Windows checks answer on this PC (it runs frame generation).
  const auto hags=fgcheck033::Scheduling();const unsigned build=fgcheck033::WindowsBuild();
  Check(hags==fgcheck033::Hags::On||hags==fgcheck033::Hags::Off||hags==fgcheck033::Hags::Unknown,"GPU scheduling reads as on, off or unknown");
  Check(build>=fgcheck033::MinimumBuild,"this PC is Windows 10 2004 or newer");
  printf("S38 checks on this PC: GPU scheduling %u (2 on), Windows build %u\n",unsigned(hags),build);
 }
 k033settings::NativeSettingsLocation::resolve(folder,error);Check(folder.size()>16&&folder.substr(folder.size()-17)==L"\\033YanYunRuntime","dedicated settings root");
 Check(yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64r\\yysls.exe"),"primary entry");
 Check(yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64rh\\yysls.exe"),"secondary entry");
 Check(!yanyun033::Layout(L"E:\\unrelated\\yysls.exe"),"renamed game rejected");
 // S39: the international client (Steam: ...\Where Winds Meet\wwm_standard\Engine\Binaries\Win64r\wwm.exe).
 Check(yanyun033::Layout(L"E:\\SteamLibrary\\steamapps\\common\\Where Winds Meet\\wwm_standard\\Engine\\Binaries\\Win64r\\wwm.exe"),"international primary entry");
 Check(yanyun033::Layout(L"E:\\SteamLibrary\\steamapps\\common\\Where Winds Meet\\wwm_standard\\Engine\\Binaries\\Win64rh\\wwm.exe"),"international secondary entry");
 Check(yanyun033::Layout(L"D:\\WWM\\ENGINE\\BINARIES\\WIN64R\\WWM.EXE"),"international entry, any letter case");
 Check(!yanyun033::Layout(L"E:\\unrelated\\wwm.exe"),"renamed international game rejected");
 Check(!yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64\\wwm.exe"),"other binary folder rejected");
 Check(!yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64r\\xwwm.exe")&&!yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64r\\wwm.exe.bak"),"near names rejected");
 Check(!yanyun033::Layout(L"\\Engine\\Binaries\\Win64r\\wwm.exe"),"no folder above Engine rejected");
 Check(yanyun033::International(L"E:\\g\\Engine\\Binaries\\Win64r\\wwm.exe")&&!yanyun033::International(L"E:\\g\\Engine\\Binaries\\Win64r\\yysls.exe"),"international client told apart");
 Check(yanyun033::Icon(yanyun033::icon,sizeof(yanyun033::icon)),"pinned icon accepted");
 Check(!yanyun033::Icon(yanyun033::icon,sizeof(yanyun033::icon)-1),"truncated icon rejected");
 printf("YANYUN CPU: %u checks, 0 failures; no games, DLL loads, GPU or production settings writes\n",checks);return 0;
 }catch(const std::exception& ex){printf("YANYUN CPU FAILED: %s\n",ex.what());return 1;}}
