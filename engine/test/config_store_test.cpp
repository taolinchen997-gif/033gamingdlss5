#include "../src/config_store.h"
#include <fstream>
#include <iterator>
static std::string Read(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool ok,const char* text){++checks;if(!ok){++failures;std::printf("FAIL %s\n",text);}};
    configstore::Debounce policy;policy.Init(50);
    check(!policy.Due(50,1000),"first present does not rewrite defaults");
    check(!policy.Due(100,1200),"edit debounces");
    check(!policy.Due(100,2199),"no premature write");
    check(policy.Due(100,2200),"panel closed: present still saves latest setting");
    policy.Completed(false,2200);
    check(policy.dirty && !policy.has_saved,"failure retains dirty state and no success badge");
    check(!policy.Due(100,3199) && policy.Due(100,3200),"failed writes back off then retry");
    policy.Completed(true,3200);
    check(policy.has_saved && !policy.Due(100,9000),"successful save clears dirty state");
    check(policy.Due(99,9000,true),"shutdown flush catches final edit without one second delay");
    char temp[MAX_PATH]={};GetTempPathA(MAX_PATH,temp);
    const std::string path=std::string(temp)+"033_cfg_test_"+std::to_string(GetCurrentProcessId())+".cfg";
    const std::string original="# user comment\r\n\r\ninject=1\r\ninjreset=0\r\nwork=50\r\nwork=75\r\nforeign="+std::string(900,'x')+"\r\n;tail without newline";
    {std::ofstream f(path,std::ios::binary);f<<original;}
    const char* keys[]={"work","mas"};
    auto write=[](FILE* f){std::fputs("work=100\nmas=0\n",f);};
    check(configstore::Update(path,keys,2,write),"atomic update succeeds");
    std::string saved=Read(path);
    check(saved.find("# user comment\r\n\r\n")==0,"comments and blank lines preserved");
    check(saved.find("injreset=0\r\n")!=std::string::npos,"foreign routing/reset keys preserved");
    check(saved.find(std::string(900,'x'))!=std::string::npos,"long foreign lines preserved");
    check(saved.find("work=50")==std::string::npos && saved.find("work=75")==std::string::npos && saved.find("work=100")!=std::string::npos,"owned duplicates replaced with current value");
    check(saved.find(";tail without newline\nwork=100")!=std::string::npos,"unterminated foreign comment safely separated");
    HANDLE guard=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    check(guard!=INVALID_HANDLE_VALUE,"file lock created");
    check(!configstore::Update(path,keys,2,[](FILE* f){std::fputs("work=25\n",f);}),"rename denied is reported");
    check(Read(path)==saved,"failed replacement leaves exact original bytes");
    CloseHandle(guard);
    check(configstore::Update(path,keys,2,write),"retry after lock release succeeds");
    DeleteFileA(path.c_str());
    check(configstore::Update(path,keys,2,write),"missing config can be created");
    check(Read(path)=="work=100\nmas=0\n","new file complete");
    DeleteFileA(path.c_str());
    std::printf("configuration persistence: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
