#pragma once
#include <cstdint>
#include <string>
#include <cctype>
namespace nrdistribution033 {
struct Geometry {const void* device=nullptr;unsigned width=0,height=0,guideWidth=0,guideHeight=0;int format=0;};
// Both reusable and newly initialized banks must belong to the requested device.
// Only the existing full-frame NGX route may ignore render-subrect size here.
// Presentation/guide-sized routes pass false and retain their exact extent checks.
inline bool GeometryMatches(const Geometry& a,const Geometry& b,bool fullGuideInvariant,bool activation){
    (void)activation;
    return a.device==b.device && a.width==b.width && a.height==b.height && a.format==b.format &&
        (fullGuideInvariant || (a.guideWidth==b.guideWidth && a.guideHeight==b.guideHeight));
}
// Every layer follows the same input contract. Native NGX subrects already encode
// the resolution mapping; RE presentation adapters instead supply guide pixels.
inline float ExtraMotionFactor(bool presentation,unsigned work,unsigned guide){
    return presentation && guide?float(work)/float(guide):1.f;
}
inline int NvidiaGeneration(const std::string& name){
    // Match the installer's GeForce product family, not workstation model numbers.
    // This is name classification only, never proof of an active graphics device.
    std::string s; s.reserve(name.size());
    for(unsigned char c:name)s+=char(std::toupper(c));
    auto p=s.find("GEFORCE");
    if(p==std::string::npos || (p && std::isalnum(static_cast<unsigned char>(s[p-1]))))return 0;
    p+=7;
    if(p==s.size() || !std::isspace(static_cast<unsigned char>(s[p])))return 0;
    while(p<s.size() && std::isspace(static_cast<unsigned char>(s[p])))++p;
    if(s.compare(p,3,"RTX"))return 0;p+=3;
    while(p<s.size() && std::isspace(static_cast<unsigned char>(s[p])))++p;
    if(p+4>s.size() || s[p]<'2' || s[p]>'5' || s[p+1]!='0' || s[p+2]<'5' || s[p+2]>'9' || s[p+3]!='0')return 0;
    if(p+4<s.size() && std::isdigit(static_cast<unsigned char>(s[p+4])))return 0;
    return (s[p]-'0')*10;
}
}
