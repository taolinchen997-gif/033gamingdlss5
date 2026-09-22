// Test double only: file-backed settings, no NVAPI import or driver access.
#include <Windows.h>
#include <cstdio>
#include <cwchar>
int wmain(int argc,wchar_t** argv){if(argc<3)return 2;wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);wcscat_s(path,L".state");
 unsigned value=7;FILE* f=nullptr;if(!_wfopen_s(&f,path,L"rb")&&f){fscanf_s(f,"%u",&value);fclose(f);}
 if(wcscmp(argv[1],L"set")==0){if(argc!=4)return 2;value=wcstoul(argv[3],nullptr,10);if(_wfopen_s(&f,path,L"wb"))return 2;fprintf(f,"%u",value);fclose(f);
  wchar_t fail[32768]{};wcscpy_s(fail,path);wcscat_s(fail,L".fail-zero");if(value==0&&GetFileAttributesW(fail)!=INVALID_FILE_ATTRIBUTES)return 2;}
 else if(wcscmp(argv[1],L"read")==0){
  wchar_t calls[32768]{};wcscpy_s(calls,path);wcscat_s(calls,L".reads");if(!_wfopen_s(&f,calls,L"ab")&&f){fputc('r',f);fclose(f);}
  wchar_t fail[32768]{};wcscpy_s(fail,path);wcscat_s(fail,L".fail-read");if(GetFileAttributesW(fail)!=INVALID_FILE_ATTRIBUTES)return 2;}
 else return 2;
 printf("{\"foundApplication\":true,\"status\":0,\"value\":%u,\"location\":0,\"predefined\":0}\n",value);return 0;}
