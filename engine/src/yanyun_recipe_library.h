#pragma once
// S23 (user): saved-preset records and author-credited share codes. Text
// handling only; the file itself is read and written by yanyun_recipe_store.h.
#include "yanyun_recipe.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
namespace yanyunrecipe {
// Every generated share code carries the author credit on the same line.
// Import looks for the code inside any surrounding text (a code never holds
// whitespace, so it ends at the first one), so bare codes from earlier builds
// and codes pasted with a comment around them import unchanged.
inline constexpr char ShareCredit[]="【B站@热心网友033 · 燕云定制版方案】";
inline std::string ShareText(const Recipe& r){
 const auto code=Encode(r);return code.empty()?code:std::string(ShareCredit)+code;
}
inline DecodeResult DecodeShared(std::string_view text,Recipe& output){
 const auto at=text.find("033YY2|");if(at==std::string_view::npos)return DecodeResult::VersionOrModel;
 auto code=text.substr(at);code=code.substr(0,code.find_first_of(" \t\r\n"));
 return Decode(code,output);
}
// Saved records: one line per record, "name<TAB>code". A name never holds a
// tab or a line break, is trimmed, and is cut on a UTF-8 character boundary.
inline constexpr unsigned LibraryMax=40,NameMax=48;
inline constexpr unsigned LibraryBytes=LibraryMax*(NameMax+MaxCode+2);
struct LibraryEntry {char name[NameMax+1]{};Recipe recipe{};};
inline void SetName(LibraryEntry& e,std::string_view name){
 std::string clean;clean.reserve(name.size());
 for(char c:name)clean+=(c=='\t'||c=='\r'||c=='\n')?' ':c;
 const auto first=clean.find_first_not_of(' ');
 clean=first==std::string::npos?std::string():clean.substr(first,clean.find_last_not_of(' ')-first+1);
 if(clean.size()>NameMax){size_t cut=NameMax;while(cut>0&&(static_cast<unsigned char>(clean[cut])&0xC0)==0x80)--cut;clean.resize(cut);}
 if(clean.empty())clean="方案";
 std::memcpy(e.name,clean.data(),clean.size());e.name[clean.size()]=0;
}
inline std::string TimeName(unsigned month,unsigned day,unsigned hour,unsigned minute){
 char b[32];std::snprintf(b,sizeof b,"%02u-%02u %02u:%02u",month,day,hour,minute);return b;
}
// Two saves in the same minute get "name (2)", "name (3)", ...
inline std::string UniqueName(const std::vector<LibraryEntry>& list,const std::string& base){
 auto taken=[&](const std::string& name){for(const auto& e:list)if(name==e.name)return true;return false;};
 std::string name=base;for(unsigned n=2;taken(name);++n)name=base+" ("+std::to_string(n)+")";return name;
}
inline std::string SerializeLibrary(const std::vector<LibraryEntry>& list){
 std::string out;
 for(const auto& e:list){const auto code=Encode(e.recipe);if(code.empty())continue;out+=e.name;out+='\t';out+=code;out+='\n';}
 return out;
}
// Unreadable lines are skipped; at most LibraryMax records are kept.
inline void ParseLibrary(std::string_view text,std::vector<LibraryEntry>& out){
 out.clear();
 while(!text.empty()&&out.size()<LibraryMax){
  const auto nl=text.find('\n');auto line=text.substr(0,nl);
  text=nl==std::string_view::npos?std::string_view():text.substr(nl+1);
  if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
  const auto tab=line.find('\t');if(tab==std::string_view::npos)continue;
  LibraryEntry e;if(Decode(line.substr(tab+1),e.recipe)!=DecodeResult::Ok)continue;
  SetName(e,line.substr(0,tab));out.push_back(e);
 }
}
}
