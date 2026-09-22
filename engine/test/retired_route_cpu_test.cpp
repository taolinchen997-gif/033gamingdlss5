#include "../src/fg_route.h"
#include <cstdio>
#include <sstream>
int main(){
    unsigned checks=0,failed=0,saves=0;
    auto check=[&](bool pass){++checks;if(!pass)++failed;};
    check(!fgroute033::UniversalOffered());
    for(const char* config:{"imagefg=1\n","fgroute=1\nfgrouteauto=0\n","fgroute=0\n"}){
        std::istringstream input(config);
        fgroute033::Selection selected(fgroute033::ParsePreferences(input));
        check(selected.Boot()==fgroute033::Native);
        check(!selected.Pending());
        check(!selected.Select(fgroute033::Universal,true,[&](unsigned){++saves;return true;}));
        check(selected.Boot()==fgroute033::Native && !selected.Pending());
    }
    check(saves==0);
    std::printf("Shipping retired FG route: %u checks, %u failures; no runtime, file write or GPU call\n",checks,failed);
    return failed?1:0;
}
