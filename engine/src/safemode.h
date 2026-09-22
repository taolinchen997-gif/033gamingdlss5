#pragma once
// ============================================================================
//  崩溃自愈 —— 连续崩就自己退场
// ----------------------------------------------------------------------------
//  为什么要有这个:
//    我们没法把每一款游戏都测一遍。总会有游戏跟我们犯冲(反作弊、自研引擎、
//    某个我们没想到的状态), 而玩家的体感是「装了这个包, 游戏打不开了」——
//    他不会知道是哪一个文件的锅, 只会觉得整个包是坏的。
//
//  做法(跟浏览器的安全模式一个路子):
//    游戏目录里放一个 dlss5-033.state, 里面只有一个数 = 连续几次没能好好退出。
//      进游戏      -> 读到 n, 立刻写 n+1        (要是这一局崩了, 这个 n+1 就留下了)
//      活过一分钟  -> 写 0                       (能跑这么久就不算崩)
//      正常退出    -> 写 0
//    所以: 崩一次留 1, 再崩留 2 ……
//      到 2  -> 一级: 渲染全停, 只留界面告诉玩家为什么, 并给一个「再试一次」
//      到 4  -> 二级: 连载入都不载入, 在游戏目录留一封说明信
//
//  「活过一分钟就清零」这条很重要: 否则玩家用任务管理器强杀游戏, 会被误记成崩溃。
// ============================================================================

namespace safemode
{

static int  s_level  = 0;       // 0=正常  1=渲染全停  2=不加载
static int  s_count  = 0;       // 读到的连续异常退出次数
static bool s_leveled = false;  // 这一局已经清过零了, 不用再写
static bool s_disarmed = false; // 二级时有没有成功把代理改名
static bool s_attached = false; // ★真的挂上了才算数★
                                //   二级时我们让 DllMain 返回 FALSE, 而 Windows 在那之后
                                //   仍然会调一次 DLL_PROCESS_DETACH。要是不认这个标记,
                                //   on_detach 就会把账清成 0 —— 保护当场失效, 下一局又崩。
static unsigned s_ticks = 0;
static bool s_recovery_ready = false;
static bool s_recovery_write_failed = false;

static const unsigned kSettleTicks = 3000;   // 约 30-60 秒
static const int      kOffAt       = 2;
static const int      kNoLoadAt    = 3;

// ★路径里只要有中文, 就必须走宽字符★
//   源码是 UTF-8 编译的, 窄字符串里的中文是 UTF-8 字节; 而 fopen/MoveFileA
//   这些窄字符 API 按系统代码页(简体中文机器上是 GBK)去解释这串字节 ——
//   对不上, 文件当场就找不到, 而且不报错, 只是静默失败。
//   实测栽过两次: 说明信写不出来、代理改不了名, 都是这一个原因。
static std::wstring game_dir_w()
{
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p(exe);
    const size_t slash = p.find_last_of(L'\\');
    if (slash != std::wstring::npos) p.resize(slash);
    return p;
}
static std::string path() { return game_dir() + "\\dlss5-033.state"; }

static int read_count()
{
    FILE *f = nullptr;
    const auto state_path=game_dir_w()+L"\\dlss5-033.state";
    if (_wfopen_s(&f, state_path.c_str(), L"rb") != 0 || f == nullptr) return 0;
    char buf[32] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return 0;
    const int v = std::atoi(buf);
    return (v < 0) ? 0 : ((v > 99) ? 99 : v);
}

static bool write_count(int n)
{
    FILE *f = nullptr;
    const auto state_path=game_dir_w()+L"\\dlss5-033.state";
    if (_wfopen_s(&f, state_path.c_str(), L"wb") != 0 || f == nullptr) return false;
    const bool wrote=std::fprintf(f, "%d", n)>0;
    const bool closed=std::fclose(f)==0;
    return wrote && closed;
}

// ★二级的真正动作: 把我们装的那个代理 DLL 改名★
//   龙之信条2 的裸 ReShade 对照说明：光让【我们的渲染】退场不一定够；
//       干净游戏(什么都不装) ............ 活 91 秒
//       只放 ReShade, 挂 d3d12 .......... 崩 10.1 秒
//       只放 ReShade, 挂 dxgi ........... 崩 8.7 秒
//   这组实验没试 REFramework 前置，不能推出“架构内无解”。但在一次启动已经
//   连续失败时，下一次最稳妥的自救仍是让本包整条加载链退场：
//   把【我们自己装进去的】那个代理 DLL 改个名, 下次游戏就一个外来 DLL 都不加载。
//   ★只动 _安装记录.txt 里我们自己写下的那个名字★ —— 绝不乱改玩家原有的文件。
//   Windows 允许给已加载的 DLL 改名(删不掉, 但改得动), 所以这一手在当场就能生效。
static bool disarm_proxy()
{
    // 从安装记录里取出我们装的代理名 (文件名带中文 -> 必须宽字符)
    const std::wstring rec = game_dir_w() + L"\\_安装记录.txt";
    FILE *f = nullptr;
    if (_wfopen_s(&f, rec.c_str(), L"rb") != 0 || f == nullptr) return false;
    char buf[1024] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return false;

    // ★v4.2.2 起是多挂载★: 同一份 ReShade 会以好几个名字放进去(治「按 Home
    //   没反应」)。所以这里必须把【整组】都改名 —— 只改 proxy= 那一个的话,
    //   剩下那两份照样被游戏加载, 等于自愈没生效。
    //   proxyall= 是整组(逗号分隔); 老版本的记录里没有这一行, 退回 proxy=。
    const char *p = std::strstr(buf, "proxyall=");
    if (p != nullptr) p += 9;
    else { p = std::strstr(buf, "proxy="); if (p == nullptr) return false; p += 6; }

    std::string line;
    while (*p != 0 && *p != 13 && *p != 10 && line.size() < 256) line.push_back(*p++);
    if (line.size() < 5) return false;

    static const char *kOk[] = { "dxgi.dll", "d3d12.dll", "d3d11.dll",
                                "d3d10.dll", "d3d9.dll", "dinput8.dll",
                                "opengl32.dll", "ddraw.dll" };
    int done = 0;
    size_t at = 0;
    while (at <= line.size())
    {
        const size_t comma = line.find(',', at);
        const std::string name = line.substr(at, (comma == std::string::npos) ? std::string::npos : comma - at);
        at = (comma == std::string::npos) ? line.size() + 1 : comma + 1;
        bool ok = false;
        for (const char *k : kOk) if (name == k) { ok = true; break; }
        if (!ok) continue;   // 记录被改坏时别误伤玩家自己的文件
        const std::wstring wname(name.begin(), name.end());   // 全是 ASCII
        const std::wstring src = game_dir_w() + L"\\" + wname;
        const std::wstring dst = src + L".dlss5-off";
        DeleteFileW(dst.c_str());
        // 已加载的 DLL 删不掉(拒绝访问), 但【改名可以】—— 实测验证过, 当场生效。
        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) ++done;
    }
    return done > 0;
}
// 二级时在游戏目录留一封说明信 —— 玩家最可能在这里找原因
//   ★必须走宽字符★: 文件名里有中文, 窄字符 fopen 在 GBK 环境下会直接失败,
//   信写不出来, 玩家只看见 ReShade 报个 1114 却不知道为什么。
static void write_letter()
{
    const std::wstring p = game_dir_w() + L"\\DLSS5已自动停用-看这里.txt";

    FILE *f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"wb") != 0 || f == nullptr) return;
    // 记事本认 BOM, 不然中文是乱码。直接写字节, 不用转义。
    static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
    std::fwrite(kBom, 1, 3, f);
    // ★原始字符串字面量★: 里面的换行就是真换行, 一个转义都不用写,
    //   免得又被哪一层工具把 \r\n 吃成别的东西。
    static const char *kText =
R"(DLSS5 一键包 —— 已经自动停用

这个游戏连着三次都没能正常退出。为了不再影响你玩, 本包已经把自己从游戏里撤出来了。
)";
    // 撤没撤干净, 说的是两码事, 别糊弄玩家
    static const char *kDone =
R"(
已经把本包装进去的那个代理 DLL 改成了 .dlss5-off 结尾。
也就是说: 下次进游戏, 它加载的外来文件是零, 跟没装过本包一样。
要是这样游戏还打不开, 那就跟本包没有关系了。

想再用回来: 把那个 .dlss5-off 的后缀去掉, 并删掉同目录的 dlss5-033.state。
)";
    static const char *kFail =
R"(
本包的渲染已经全部停掉, 但那个代理 DLL 没能改名(可能是权限不够)。
想彻底排除本包的嫌疑, 请双击游戏目录里的 dlss5_uninstall.ps1 卸载一次。
)";
    static const char *kTail =
R"(
想彻底卸掉: 双击游戏目录里的 dlss5_uninstall.ps1。

B站 @热心网友033
)";
    std::fputs(kText, f);
    std::fputs(s_disarmed ? kDone : kFail, f);
    std::fputs(kTail, f);
    std::fclose(f);
}

// 进程刚起来时调用。返回 false = 二级, 这一局根本不该加载。
static bool on_attach()
{
    s_count = read_count();
#ifdef K033_MONOLITHIC_ENGINE
    // An abnormal-exit counter cannot attribute a crash. Keep the adapter/UI
    // available and suspend only 033 NR; never rename installed game files.
    s_level = s_count >= kOffAt ? 1 : 0;
    write_count(s_count + 1);
    s_attached = true;
    if (s_level) Log("[安全] %d 次异常退出记录：暂停 033 NR，保留控制面板；未改挂载或其他文件。", s_count);
    return true;
#else
    if (s_count >= kNoLoadAt)
    {
        s_level = 2;
        s_disarmed = disarm_proxy();   // 把代理改名, 下次就是干净游戏
        Log("[安全] 连着 %d 次没能正常退出 —— 本包整个退出这个游戏。", s_count);
        Log("       代理改名: %s", s_disarmed ? "成功, 下次进游戏就是干净的" : "★失败★(看看 _安装记录.txt 在不在)");
        write_letter();
        // ★改名成功就把账本清零★
        //   代理都不在了, 这个账本对下一局毫无意义; 留着反而害人:
        //   玩家要是手动把 .dlss5-off 的后缀去掉想再试一次, 一进游戏
        //   就又读到「已经崩满三次」, 当场再被改走 —— 成了死循环。
        //   清零 = 每一次手动恢复, 都重新给三次机会。
        if (s_disarmed) write_count(0);
        return false;
    }
    if (s_count >= kOffAt)
        s_level = 1;

    write_count(s_count + 1);
    s_attached = true;

    if (s_level == 1)
    {
        Log("[安全] 上次(以及上上次)这个游戏都没能正常退出 —— 这一局我们不做任何渲染。");
        Log("       想再试: 删掉游戏目录里的 dlss5-033.state, 或在面板上点「再试一次」。");
    }
    return true;
#endif
}

// 每次 present 调用一次: 活够久就把账清了
static void tick()
{
    if (s_leveled) return;
    if (++s_ticks < kSettleTicks) return;
    if(write_count(0)) {
        s_leveled=true;s_recovery_ready=s_level>=1;s_recovery_write_failed=false;
        Log("[033 recovery] startup counter cleared after settled frames; paused=%d; restart required=%d",s_level>=1,s_level>=1);
    }
}

static void on_detach()
{
    if (!s_attached) return;   // 二级那条路也会走到这里, 但那不算「跑过一局」
    if (!s_leveled) write_count(0);
}

static bool off()    { return s_level >= 1; }   // 渲染全停
static int  level()  { return s_level; }
static int  count()  { return s_count; }
static bool recovery_ready() {return s_recovery_ready;}
static bool recovery_write_failed() {return s_recovery_write_failed;}

static void retry()  {
#ifndef K033_MONOLITHIC_ENGINE
    s_level = 0;
#endif
    // The monolithic adapter's render events are registered at startup only.
    // Clearing the counter must not activate a half-registered renderer now.
    const bool saved=write_count(0);
    s_recovery_write_failed=!saved;s_recovery_ready=saved;
    if(saved){s_count=0;s_leveled=true;}
    Log("[033 recovery] user requested next-start recovery; saved=%d; paused=%d",saved,s_level>=1);
}

} // namespace safemode
