#define NOMINMAX
#include "../src/yanyun_recipe_store.h"
#include "../src/yanyun_exclusive.h"
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
 k033settings::NativeSettingsLocation::resolve(folder,error);Check(folder.size()>16&&folder.substr(folder.size()-17)==L"\\033YanYunRuntime","dedicated settings root");
 Check(yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64r\\yysls.exe"),"primary entry");
 Check(yanyun033::Layout(L"E:\\test\\Engine\\Binaries\\Win64rh\\yysls.exe"),"secondary entry");
 Check(!yanyun033::Layout(L"E:\\unrelated\\yysls.exe"),"renamed game rejected");
 Check(yanyun033::Icon(yanyun033::icon,sizeof(yanyun033::icon)),"pinned icon accepted");
 Check(!yanyun033::Icon(yanyun033::icon,sizeof(yanyun033::icon)-1),"truncated icon rejected");
 printf("YANYUN CPU: %u checks, 0 failures; no games, DLL loads, GPU or production settings writes\n",checks);return 0;
 }catch(const std::exception& ex){printf("YANYUN CPU FAILED: %s\n",ex.what());return 1;}}
