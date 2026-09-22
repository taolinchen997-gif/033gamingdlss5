// =====================================================================
//  staterestore_test.cpp —— 只为证明 staterestore.h 在 /std:c++17 /utf-8 + ReShade 6.8
//  头文件下能编过(cl /c 出 obj 就算数)。不链接、不进成品。
//  用法(与 build.bat 同一套环境变量):
//    cl /nologo /c /std:c++17 /O2 /MT /EHa /utf-8 /W3 /I sdk\reshade-6.8.0\include /I sdk /I src
//       /Fo<临时目录>\staterestore_test.obj src\staterestore_test.cpp
// =====================================================================
#include <Windows.h>
#include <d3d12.h>
#include <reshade.hpp>
#include <vector>
#include <unordered_map>

namespace rsu {
#include "reshade_utils/state_tracking.hpp"
#include "reshade_utils/state_tracking.cpp"
}

// 用一个假的日志函数走一遍 STATERESTORE_LOG 那条宏路
static void TestLog(const char *, ...) {}
#define STATERESTORE_LOG TestLog
#include "staterestore.h"

// 模拟 hostnr::Stage 的用法: 信封建在第一次绑之前, 中间有早退
int StageLike(ID3D12GraphicsCommandList *cl, int fail_early)
{
    if (!staterestore::allow_frame(cl)) return 0;
    staterestore::Envelope env(cl);   // 出作用域自动还
    if (fail_early) return 0;         // 早退也被 RAII 罩着
    ID3D12DescriptorHeap *heaps[1] = {};
    (void)heaps;
    return staterestore::captured(cl) ? 1 : 0;
}

void RegisterLike()
{
    staterestore::cfg_enabled = 2;
    staterestore::register_events();
    (void)staterestore::note();
    (void)staterestore::active();
    staterestore::unregister_events();
}
