#include "worker_protocol.h"
#include <cassert>
int wmain(int argc,wchar_t** argv){try{
 if(argc!=3)return 2;
 // The parent deliberately loads the same old CRT as the failed game.
 auto old=LoadLibraryExW(argv[2],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!old)throw std::runtime_error("old CRT fixture missing");
 yyworker::Client client;client.Start(std::filesystem::absolute(argv[1]),123);
 std::vector<uint8_t> pixels(640*270*4,255);for(size_t i=0;i<pixels.size()/4;++i)pixels[i*4]=uint8_t(i%251);
 for(int n=0;n<3;++n){auto& out=client.Run(pixels,640,270);if(out.completed!=uint64_t(n+1))throw std::runtime_error("sequence failed");
  for(size_t i=0;i<pixels.size()/4;++i)if(out.coverage[i]!=pixels[i*4]||out.groups[i]!=0)throw std::runtime_error("mask transport failed");}
 bool rejected=false;try{client.Run(pixels,641,270);}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("oversized request admitted");
 printf("WORKER CPU: old-CRT parent / private matched-CRT child; 3 complete 640x270 transfers; invalid input rejected; no GPU or ORT session\n");return 0;
}catch(const std::exception& e){printf("WORKER CPU FAILED: %s\n",e.what());return 1;}}
