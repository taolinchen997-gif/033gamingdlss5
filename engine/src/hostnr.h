// =====================================================================
//  hostnr.h ——  老游戏(32位)路线的神经渲染阶段
//
//  32 位游戏进不了 NGX(只有 64 位), 所以 Feeder 用一个 64 位帮手进程
//  (dlss5-feed-host64.exe)代跑, 两边靠共享纹理传画面。
//
//  ★为什么不能直接用 carrier★
//  我们的插件确实能加载进那个帮手进程, 但那里 ReShade 的后缓冲是【帮手
//  自己那个小窗】, 不是游戏画面 —— 在上面做降噪等于给 UI 窗口降噪, 白跑。
//  游戏画面在帮手的共享纹理 FEED_COLOR / FEED_OUTPUT 里。
//
//  所以改成: 帮手在自己的 Evaluate 里, DLSS 写完 output 之后回调我们,
//  我们就地把 output 改写掉。共用它的命令列表 —— 不用跨列表同步, 不用
//  自己开围栏, 最省事也最安全。
//  (帮手那边加的两个导出: dlss5_feed_033_set_nr / dlss5_feed_033_guides,
//   改的是 dlss5-feed-host64.cpp, MIT, 见 工具\署名)
//
//  流程跟桌面路线一样: 原画 → 缩小 → NR(小→小) → 差值合成 → 写回 output
// =====================================================================
#pragma once
#include "nr_clarity_policy.h"
#include "nr_stack_policy.h"
#include "nr_skin_policy.h"
#include "nr_layer_bank.h"
#include "nr_recovery_policy.h"
#include "nr_layer_resolution.h"
#include "nr_stack_layout.h"
#include "nr_stack_finish.h"
#include "nr_output_cache.h"
#include "nr_snapshot.h"
#include "nr_lifetime_diagnostics.h"

#include "nr_resize_policy.h"
#include "nr_distribution_policy.h"
#include "gpu_fault.h"
#include "framegen_scene.h"
#include "nr_game_inputs.h"
#include "nr_guide_normalize.h"
namespace hostnr
{
#ifdef K033_BETA2_RESHADE_HOST
static thread_local const k033core::Frame* input_frame=nullptr;
static thread_local bool input_graded=false;
struct InputScope {
    const k033core::Frame* previous;bool graded;
    InputScope(const k033core::Frame* f,bool g):previous(input_frame),graded(input_graded){input_frame=f;input_graded=g;}
    ~InputScope(){input_frame=previous;input_graded=graded;}
};
static bool InputCurrent(){return !input_frame||(input_frame->current&&input_frame->current(input_frame));}
#endif

typedef int(__cdecl *PFN_NR)(ID3D12GraphicsCommandList *, ID3D12Device *, ID3D12Resource *,
                             ID3D12Resource *, ID3D12Resource *, unsigned, unsigned,
                             float, float, int, int);
typedef void(__cdecl *PFN_SetNR)(PFN_NR);

static bool            s_hooked = false;
static int             s_attempts = 0;   // 重试次数: Feeder 插件可能比我们晚加载
static bool            s_failed = false;
static std::string     s_note = "未接入";
static ID3D12Device   *s_dev = nullptr;
static nrclarity::Cache<ID3D12Resource,ID3D12Device> s_clarity;
static ID3D12Resource *s_full = nullptr;   // output 的满尺寸副本(模型要读, output 是 UAV 不好直接采)
static ID3D12Resource *s_small = nullptr;  // 缩小后的输入
static ID3D12Resource *s_out = nullptr;    // 模型的答案(小)
static ID3D12Resource *s_res = nullptr;    // 差值合成的落点(满)
static void           *s_feat = nullptr;
static void           *s_extra_feat[nrfeatures::MaxPasses-1] = {};
static ID3D12Resource *s_extra_out = nullptr;
static int             s_built_passes = 1;
static int s_built_passwork=100,s_built_passwork3=100;
static int s_selected_passes=1;
static ID3D12Resource* s_refined[2]={};
static decltype(carrier::cfg) s_model_cfg;
static UINT64 s_pass_frame[nrfeatures::MaxPasses]={};
static unsigned s_built_appearance=0;
static UINT s_ew=0,s_eh=0,s_tw=0,s_th=0;
static ID3D12Resource *s_pass_input=nullptr,*s_pass_input3=nullptr,*s_extra_alt=nullptr;
static uintptr_t s_generation=1;
static uintptr_t s_first_generation=1;
static uint64_t s_bank_revision=0;
static void* GenerationTag(){return reinterpret_cast<void*>(s_generation);}
static unsigned long long s_full_builds=0,s_pass_builds=0;
static double s_last_build_ms=0;
static UINT64 s_memory_usage=0,s_memory_budget=0;
static IDXGIAdapter3* s_memory_adapter=nullptr;
static UINT            s_w = 0, s_h = 0, s_sw = 0, s_sh = 0;
static int             s_built_work = 0;
static unsigned        s_pending_size = 0xFFFFFFFFu;   // 尺寸旋钮防抖
static unsigned        s_pending_geom = 0xFFFFFFFFu;   // 画面/引导图尺寸防抖
// ★引导图尺寸由 Stage 显式交过来★ Build 跑在 present 里, 那个时刻 s_gw_override
//   可能已经不是钩子里那个值了 —— 记的和比的取自不同时刻, 就会永远不相等 =
//   每帧都想重建。显式传就没有这个时刻差。
static UINT            s_want_gw = 0, s_want_gh = 0;
static int             s_geom_hold  = 0;
static int             s_size_hold  = 0;
// ★建 feature 时的「模型基准」★ —— 少了这一项, 面板上那两个单选按钮就是死的:
//   模型尺寸只在 Build() 里算一次, 而下面的重建条件从来没比过 modelfull,
//   于是切换基准只改了日志上那行字, 模型该多大还是多大(业主 2026-09-04 实测发现)。
static int             s_built_full = 1;
// ★自动让位★ 就地插入被状态闸连续拒绝多少帧就认输, 把 feature 18 交回交换链。
//   龙之信条 2 那类 bindless 引擎: 我们抓不全它的计算态, 闸门只能一直跳帧保命,
//   结果是「占着位置又不干活」—— 用户一点效果没有, 还以为插件坏了。
//   而交换链那条路用自己的命令列表, 不需要还原游戏状态, 在同一游戏上实测能跑
//   5400 帧零掉线。所以: 跳够 600 帧(约十秒)就让位, 别干耗着。
static unsigned        s_gate_miss = 0;
static bool            s_yielded   = false;

// ★模型输入的时域稳定★ 两张乒乓的历史图, 跟 s_small 同尺寸同格式。
//   写哪张就把哪张临时转成 UAV, 用完转回 SR; 模型和差值合成都读【稳定后】那张
//   —— 差值必须用「模型真正看到的输入」, 否则稳定带来的那点差异会被当成模型编辑铺回画面。
// 运动矢量的 SRV 格式。carrier::TypedColorFormat 只认颜色格式(RGBA/RGB), 运动矢量是
// 双通道 RG, 拿它去问一律返回 UNKNOWN —— 第一版就是这么把整趟稳定化判掉的。
// typeless 要挑一个 typed 变体; 已经是 typed 的原样用; 认不出的返回 UNKNOWN 表示别做。
static DXGI_FORMAT MvSrvFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT:  return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:  return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16_SNORM:    return DXGI_FORMAT_R16G16_SNORM;
    case DXGI_FORMAT_R16G16_UNORM:    return DXGI_FORMAT_R16G16_UNORM;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

static ID3D12Resource *s_hist[2] = { nullptr, nullptr };
static int             s_hist_i   = 0;
static unsigned        s_hist_age = 0;   // 重建后前几帧没有历史, 直接透传
// ★调参是【建 feature 时读一次】的★
//   carrier 那条路一直有 TuneSig() 比对(改了就重建), hostnr 这条路没有 ——
//   于是就地插入路和 32 位老游戏路上, 预设/强度/细节/层次/肤质/风格
//   【全都不生效】(业主实测: 预设换 1/2/3 完全没区别)。
//   s_pending/s_hold 是防抖: 拖滑杆时别每帧重建(建一次要 200ms 级)。
static unsigned        s_built_tune   = 0;
static UINT            s_built_gw = 0, s_built_gh = 0;   // 建 feature 时的引导图尺寸
static unsigned        s_pending_tune = 0;
static int             s_tune_hold    = 0;
static DXGI_FORMAT     s_fmt = DXGI_FORMAT_UNKNOWN;
static DXGI_FORMAT     s_model_fmt = DXGI_FORMAT_UNKNOWN;
static ID3D12Resource *s_spare_full=nullptr,*s_spare_res=nullptr;
static DXGI_FORMAT s_spare_fmt=DXGI_FORMAT_UNKNOWN;
static scale::Blitter  s_blit;
static nrrecovery033::Retry s_blit_retry,s_signal_retry;
static nrrecovery033::Evaluation s_evaluate_recovery;
static bool s_signal_pending=false; // owned by the existing NR writer
static std::atomic<unsigned> s_auto_recovery{0};
static exposure::FrameMeter s_exposure; // Per-lease owned exposure/white resources.
static UINT64          s_frames = 0;
static nrcontract::History s_history;
static ID3D12Resource *s_hold_depth=nullptr,*s_hold_motion=nullptr,*s_hold_white=nullptr;
static nrcontract::FrameKey s_hold_key;
static bool s_hold_active=false;
static thread_local uintptr_t s_stream_key = 0;
static void set_stream_key(uintptr_t key) { s_stream_key = key; }
// S18: the person bank keeps its own history gate. Shared discontinuities
// (panel reset, hold, colour switch, dispatch gap, NR toggle) invalidate both
// through invalidate_history(); scene-only rebuilds call s_history.invalidate()
// directly and no longer reset the person chain or its recognition results.
static nrcontract::History s_person_history;
static void invalidate_history() { s_history.invalidate(); s_person_history.invalidate(); s_hist_age = 0; }
// 面板上的「清一次历史帧」。只置一个一次性标志, 真正的动作在帧里做 ——
// 面板是在 present 里画的, 不在那儿碰渲染状态。
static std::atomic<bool> s_history_reset_request{false};
static void RequestHistoryReset() { s_history_reset_request.store(true, std::memory_order_release); }
static int             s_grace = 0;    // 建完 feature 后的宽限帧数
static bool            s_has_depth = false, s_has_mv = false;  // 帮手递过来的引导图有没有

// ★就地插入路的延迟建造★
// nrfwd::create 会调进 NGX 建 feature。如果这动作发生在【游戏 NGX 求值的钩子里】,
// 运行库拿着锁, 重入 = 死锁(审判之眼实测卡死)。
// 所以就地插入路上, 建 feature 挪到 present(不在任何 NGX 调用栈里), 用我们【自己的】
// 命令队列录初始化、提交、等完 —— 之后钩子里只做求值, 不碰"建"。
static bool                    s_defer_build = false;   // inject 打开时置 1
// ★画面尺寸 ≠ 引导图尺寸★
//   就地插入路上, 游戏的 DLSS Output 是【输出分辨率】(5120x2160), 而深度/运动
//   矩阵是【渲染分辨率】(2970x1253)。以前把后者当成画面尺寸传进来, 结果只处理了
//   左上角那块 —— 实测「古墓丽影只有左上角四分之一渲染了」就是这个。
//   0 = 跟画面尺寸一致(老游戏那条路本来就一致)。
static unsigned        s_gw_override = 0, s_gh_override = 0;
static thread_local nrcontract::Guides s_frame_guides;
static thread_local bool s_have_frame_guides = false;
static thread_local nrcontract::Rect s_frame_output;
static void set_output_rect(const nrcontract::Rect &v) { s_frame_output = v; }
static void set_guide_dims(unsigned gw, unsigned gh) { s_gw_override = gw; s_gh_override = gh; }
static void set_guide_rects(const nrcontract::Guides &v) { s_frame_guides = v; s_have_frame_guides = true; set_guide_dims(v.depth.width, v.depth.height); }
static void clear_guide_rects() { s_have_frame_guides = false; s_frame_output = {}; s_gw_override = s_gh_override = 0; }
static bool                    s_want_build  = false;
// ★正在建的时候不许再排队★ (2026-09-04 夜, 燕云崩溃现场实锤)
//   建一个 feature 要 ~200 毫秒。这段时间里 Release() 已经把 s_feat 清空了,
//   而游戏每 1/60 秒调一次我们的钩子 —— 它看到 s_feat == nullptr, 就当作
//   「还没有模型」, 于是【又排一次重建】。日志实锤:
//     29.645 要重建 → 29.673 又要重建 → 29.862 ★就绪★ → 30.078 ★就绪★ → 崩
//   建完第一个, 第二个请求接着把它拆了重建 —— 拆一个 GPU 正在用的 feature
//   就是无声闪退。那个"每 0.8 秒重建一次"的无限循环也是这么自我繁殖的。
static std::atomic<bool>       s_building{false};
static std::atomic<bool>       s_cpu_building{false};
static int                     s_busy_tries  = 0;
static int                     s_retry_cool  = 0;
// ★建完之后的冷却★ (2026-09-04 晚, 燕云十六声实测)
//   日志实锤: 改一次设置连着出两行「★就绪★」, 相隔 140-220 毫秒, 尺寸一模一样;
//   一局里重建了 13 次。两个 feature 18 在同一瞬间活着 = 运行库 0xC0000005,
//   而且崩得无声无息(没有设备移除、没有 ReShade 错误)。
//   建完先冷却一段, 这期间任何重建请求一律压住 —— 参数最终还是会生效,
//   只是不会一次改动引发好几次重建。
static int                     s_build_cool  = 0;
static ID3D12Device           *s_want_dev  = nullptr;
static UINT                     s_want_w = 0, s_want_h = 0;
static DXGI_FORMAT              s_want_fmt = DXGI_FORMAT_UNKNOWN;
static ID3D12CommandQueue     *s_bq = nullptr;
static ID3D12CommandAllocator *s_ba = nullptr;
static ID3D12GraphicsCommandList *s_bl = nullptr;
static ID3D12Fence            *s_bf = nullptr;
static HANDLE                  s_be = nullptr;
static UINT64                  s_bv = 0;

static bool EnsureBuilder(ID3D12Device *dev)
{
    if(!dev)return false;
    if(s_bq || s_ba || s_bl || s_bf || s_be){
        // Existing work may be in flight. Never retire/rebuild it on a device
        // change or accept an incomplete preparation as a usable builder.
        if(!s_bq || !s_ba || !s_bl || !s_bf || !s_be)return false;
        ID3D12Device* owner=nullptr;
        if(FAILED(s_bq->GetDevice(__uuidof(ID3D12Device),reinterpret_cast<void**>(&owner))) || !owner)return false;
        IUnknown* actual=nullptr;IUnknown* requested=nullptr;
        const HRESULT actualHr=owner->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&actual));
        owner->Release();
        if(FAILED(actualHr) || !actual)return false;
        const HRESULT requestedHr=dev->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&requested));
        const bool same=SUCCEEDED(requestedHr) && requested && actual==requested;
        if(requested)requested->Release();
        actual->Release();return same;
    }
    ID3D12CommandQueue* queue=nullptr;
    ID3D12CommandAllocator* allocator=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Fence* fence=nullptr;
    HANDLE event=nullptr;
    // Only ordinary failures reach this cleanup. These objects are private,
    // have never been submitted and have not been published to PumpBuild.
    // Do not add exception-unwind cleanup of unknown driver state here.
    auto fail=[&](){
        if(event)CloseHandle(event);
        if(fence)fence->Release();
        if(list)list->Release();
        if(allocator)allocator->Release();
        if(queue)queue->Release();
        return false;
    };
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(FAILED(dev->CreateCommandQueue(&qd,__uuidof(ID3D12CommandQueue),reinterpret_cast<void**>(&queue))) || !queue)return fail();
    if(FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),reinterpret_cast<void**>(&allocator))) || !allocator)return fail();
    if(FAILED(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,__uuidof(ID3D12GraphicsCommandList),reinterpret_cast<void**>(&list))) || !list)return fail();
    if(FAILED(list->Close()))return fail();
    if(FAILED(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),reinterpret_cast<void**>(&fence))) || !fence)return fail();
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event)return fail();
    if(gpufault033::Requested()){queue->SetName(L"033 NR model initialization queue");list->SetName(L"033 NR model initialization list");allocator->SetName(L"033 NR model initialization allocator");}
    // PumpBuild's existing WriterAccess serializes this publication.
    s_bq=queue;s_ba=allocator;s_bl=list;s_bf=fence;s_be=event;
    return true;
}

// ★不能用 carrier::MakeTex★ —— 它内部用的是 carrier::g.dev, 而 carrier 在帮手
// 进程里是停用的, 那个指针是空的, 一调就崩。这里用传进来的设备自己建。
static bool MakeTexOn(ID3D12Device *dev, ID3D12Resource **out, const char *name, UINT w, UINT h,
                      DXGI_FORMAT fmt, D3D12_RESOURCE_STATES initial)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr,
                                                    __uuidof(ID3D12Resource),
                                                    reinterpret_cast<void **>(out));
    if (FAILED(hr)) { Log("[hostnr] %s 建不出来 0x%08X", name, hr); return false; }
    if(gpufault033::Requested()){wchar_t label[128];swprintf_s(label,L"033 NR %hs %ux%u",name,w,h);(*out)->SetName(label);}
    return true;
}

// Retire immediately once command recording can no longer replay AND every
// observed native queue has completed. A guessed number of frames adds memory
// pressure without proving safety. Unknown submissions remain bounded and held.
// Retained first-pass resources can span multiple incremental generations.
// Their retirement proof includes all earlier recordings that may still replay.
struct Parked { ID3D12Resource *r; void *feat; void* tag; uintptr_t first; };
static std::vector<Parked> s_parked;
static void SampleMemory(ID3D12Device* dev) {
    if(!s_memory_adapter && dev) {
        IDXGIFactory4* factory=nullptr;
        if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4),reinterpret_cast<void**>(&factory)))){
            factory->EnumAdapterByLuid(dev->GetAdapterLuid(),__uuidof(IDXGIAdapter3),reinterpret_cast<void**>(&s_memory_adapter));factory->Release();
        }
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if(s_memory_adapter && SUCCEEDED(s_memory_adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))){s_memory_usage=info.CurrentUsage;s_memory_budget=info.Budget;}
}
static void ParkTick()
{
    if(nrfault033::Blocked())return;
    nrdispatch::WriterAccess writer;
    if(!writer.entered || s_building.load())return;
    if (s_build_cool > 0) --s_build_cool;
    if (s_retry_cool > 0) --s_retry_cool;
    for (size_t i = 0; i < s_parked.size();)
    {
        if (resolveleases::PendingRange(s_parked[i].first,reinterpret_cast<uintptr_t>(s_parked[i].tag))) { ++i; continue; }
        if (s_parked[i].feat != nullptr) {
            nrfwd::release(s_parked[i].feat, false);
            if(nrfault033::Blocked())return;
            if(s_parked[i].feat){++i;continue;}
        }
        if (s_parked[i].r != nullptr) s_parked[i].r->Release();
        s_parked.erase(s_parked.begin() + static_cast<long>(i));
    }
    static ULONGLONG reported=0;
    if(GetTickCount64()-reported>=5000){
        reported=GetTickCount64();SampleMemory(s_dev);
        unsigned features=0;for(const auto& p:s_parked)if(p.feat)++features;
        Log("[033 model lifetime] created=%llu released=%llu release_failed=%llu parked_features=%u parked_objects=%u full_builds=%llu pass_changes=%llu last_build_ms=%.2f VRAM_MiB=%llu budget_MiB=%llu",
            nrfwd::s_created,nrfwd::s_released,nrfwd::s_release_failed,features,unsigned(s_parked.size()),s_full_builds,s_pass_builds,s_last_build_ms,s_memory_usage>>20,s_memory_budget>>20);
    }
}
static void Park(ID3D12Resource *&r) { if (r) { ++s_bank_revision;s_parked.push_back({ r, nullptr, GenerationTag(),s_first_generation }); r = nullptr; } }
static void ParkFeature(void*& feature){if(feature){++s_bank_revision;s_parked.push_back({nullptr,feature,GenerationTag(),s_first_generation});feature=nullptr;}}
static void ReleaseParkedFeaturesNow(){ParkTick();}

// 停车场里还压着没放掉的 feature 吗? —— 建新的之前必须先问这一句
// ★这一帧没建成, 但【不是失败】★ —— 只是旧 feature 还没退干净, 下一帧再来。
//   两个 Build 调用点都必须认这个标记: 它们对 false 的默认反应是 s_failed = true,
//   那是【永久】关掉整条路(s_failed 没有任何地方会复位)。
//   2026-09-04 夜实测: 少了这个标记, 业主一碰设置神经渲染就再也不回来,
//   面板卡在「切换中」、帧数停住、自检还倒打一耙说"游戏一次都没调用它"。
static bool s_build_defer = false;

static bool AnyParkedFeature()
{
    for (size_t i = 0; i < s_parked.size(); ++i)
        if (s_parked[i].feat != nullptr) return true;
    return false;
}

static void Release()
{
    if(nrfault033::Blocked())return; // Keep all owned models/textures, including unsubmitted candidates.
    Park(s_extra_out);Park(s_extra_alt);Park(s_pass_input);Park(s_pass_input3);Park(s_refined[0]);Park(s_refined[1]);
    for(auto& feature:s_extra_feat)ParkFeature(feature);
    Park(s_hold_depth);Park(s_hold_motion);Park(s_hold_white);s_hold_active=false;
    Park(s_full);Park(s_small);Park(s_out);Park(s_res);
    Park(s_spare_full);Park(s_spare_res);s_spare_fmt=DXGI_FORMAT_UNKNOWN;
    Park(s_hist[0]);Park(s_hist[1]);s_hist_i=0;s_hist_age=0;
    ParkFeature(s_feat);++s_generation;s_first_generation=s_generation;
    s_w=s_h=0;
}

static bool ConfigurePasses(ID3D12Device* dev,ID3D12GraphicsCommandList* cl,UINT sw,UINT sh,DXGI_FORMAT fmt)
{
    if(nrfault033::Blocked())return false;
    const int count=nrfeatures::ClampPasses(carrier::cfg.passes);
    const int work=nrlayersr::ClampWork(carrier::cfg.passwork),work3=nrlayersr::ClampWork(carrier::cfg.passwork3);
    const auto layout=nrlayersr::Make({sw,sh},work,work3);
    const UINT ew=layout.layer[1].width,eh=layout.layer[1].height;
    const UINT tw=layout.layer[2].width,th=layout.layer[2].height;
    const bool changed2=s_ew!=ew || s_eh!=eh,changed3=s_tw!=tw || s_th!=th;
    for(int i=0;i<nrfeatures::MaxPasses-1;++i)if((i?changed3:changed2) || i>=count-1)ParkFeature(s_extra_feat[i]);
    if(changed2 || count==1){Park(s_extra_out);Park(s_pass_input);}
    if(changed3 || count<3){Park(s_extra_alt);Park(s_pass_input3);}
    ++s_generation;
    // Keep at most the retained first pass plus one retired pass set alive.
    // Allow no additional model allocation while an old set is outstanding.
    ParkTick();
    if(nrfault033::Blocked())return false;
    if(AnyParkedFeature()){s_build_defer=true;s_note="等待旧模型完成并释放";return false;}
    if(count>1){
        if(!s_extra_out && !MakeTexOn(dev,&s_extra_out,"extra-out",ew,eh,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
        if(!s_pass_input && !MakeTexOn(dev,&s_pass_input,"layer2-input",ew,eh,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
        if(count>2){
            if(!s_extra_alt && !MakeTexOn(dev,&s_extra_alt,"layer3-output",tw,th,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
            if(!s_pass_input3 && !MakeTexOn(dev,&s_pass_input3,"layer3-input",tw,th,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
        }
        DWORD seh=0;
        for(int i=0;i<count-1;++i)if(!s_extra_feat[i]){
            const auto tune=nrlayers::Get(carrier::cfg,i+1);
            const auto size=layout.layer[i+1];
            s_extra_feat[i]=nrfwd::create(dev,cl,size.width,size.height,tune.preset,tune.intensity,tune.style,
                tune.structure,tune.tone,tune.skin,tune.autoMask,
                tune.uiCorrect,&seh,tune.globalTone);
            if(!s_extra_feat[i]||seh){s_note="后续层模型创建失败";return false;}
        }
    }
    if(count>1){
        if(nrlayersr::AnyScaled(layout,count)){
            for(int i=0;i<2;++i)if(!s_refined[i] && !MakeTexOn(dev,&s_refined[i],i?"refinement-result":"conditioned-prior",sw,sh,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
        }else{Park(s_refined[0]);Park(s_refined[1]);}
    }else{
        Park(s_refined[1]);
        // The single NR layer needs the same final local face/colour condition.
        // Reuse the retained full-model scratch slot, never alias the model input.
        if(!s_refined[0] && !MakeTexOn(dev,&s_refined[0],"single-layer-final-condition",sw,sh,fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS))return false;
    }
    Log("[hostnr stack policy v5] layers=%d first=%ux%u layer2=%ux%u work2=%d%% layer3=%ux%u work3=%d%% final_guard=%d; independent NR dimensions; game SR unchanged",count,sw,sh,ew,eh,work,tw,th,work3,1);
    for(int pass=0;pass<count;++pass){
        const auto tune=nrlayers::Get(carrier::cfg,pass);
        Log("[hostnr stack request] layer=%d intensity=%.4f structure=%.4f localTone=%.4f skin=%.4f globalTone=%.4f style=%d preset=%d independent_of_first=%d; runtime adoption not verified",pass+1,tune.intensity,tune.structure,tune.tone,tune.skin,tune.globalTone,tune.style,tune.preset,pass>0?1:0);
    }
    s_selected_passes=count;s_built_passes=count;s_built_passwork=work;s_built_passwork3=work3;
    s_ew=ew;s_eh=eh;s_tw=tw;s_th=th;
    s_model_cfg=carrier::cfg;for(auto& frame:s_pass_frame)frame=0;
    return true;
}

static bool Build(ID3D12Device *dev, ID3D12GraphicsCommandList *cl, UINT w, UINT h, DXGI_FORMAT fmt)
{
    if(nrfault033::Blocked())return false;
    const auto started=GetTickCount64();
    const UINT gw=s_want_gw?s_want_gw:w,gh=s_want_gh?s_want_gh:h;
    // NVIDIA keeps shared allocations while any NR feature remains alive.
    // Returning to one pass drains the old set so its high-water cache can go.
    const bool trimCache=carrier::cfg.passes==1 && s_built_passes>1;
    const bool sameBase=!trimCache && s_feat && s_dev==dev && s_w==w && s_h==h && s_fmt==carrier::TypedColorFormat(fmt) &&
        s_built_gw==gw && s_built_gh==gh && s_built_work==carrier::cfg.work && s_built_full==(carrier::cfg.modelfull?1:0) &&
        s_built_appearance==carrier::TuneSig(false);
    if(sameBase){
        if(!ConfigurePasses(dev,cl,s_sw,s_sh,s_model_fmt))return false;
        s_built_tune=carrier::TuneSig();s_pending_tune=s_built_tune;s_tune_hold=0;s_build_cool=0;
        s_history.invalidate();++s_pass_builds;s_last_build_ms=double(GetTickCount64()-started);
        Log("[hostnr] retained first model; passes=%d layer2=%ux%u (%d%%) layer3=%ux%u (%d%%) build_ms=%.2f",s_built_passes,s_ew,s_eh,s_built_passwork,s_tw,s_th,s_built_passwork3,s_last_build_ms);
        return true;
    }
    Release();ParkTick();
    if(nrfault033::Blocked())return false;
    if(AnyParkedFeature()){s_build_defer=true;s_note="等待旧模型完成并释放";return false;}
    const DXGI_FORMAT t = carrier::TypedColorFormat(fmt);
    if (t == DXGI_FORMAT_UNKNOWN) { s_note = "不支持的格式"; return false; }

    int work = carrier::cfg.work;
    if (work < 25 || work > 200) work = 75;   // 25..200, 跟参考实现同范围

    // ★★ 模型分辨率的基准是【引导图】, 不是画面 ★★
    //   就地插入路上, 画面(游戏的 DLSS Output)是输出分辨率 5120x2160, 而深度和
    //   运动矢量是渲染分辨率 2970x1253。以前按画面算模型尺寸, 于是模型 5120 宽
    //   却拿着 5120 的坐标去读 2970 宽的引导图 —— 越界读, 【整台机器硬冻死】
    //   (实测 2026-09-03 23:06: 跑 5 帧后死机, 连 TDR 都没来得及记)。
    //   钉在引导图上还顺手把性能找回来了: 那就是游戏自己的渲染分辨率,
    //   正是上一版 renodx 做神经渲染的地方 —— 5120x2160 的 1106 万像素
    //   降到 2970x1253 的 372 万, 少 2.97 倍。
    //   交换链那条路引导图跟画面同尺寸, 这里的行为跟以前完全一致。
    // 模型分辨率基准: 参考实现是【整帧】(workWidth = width * workScale, 默认 1.0),
    // 引导图按 subrect 交过去, 尺寸不同没关系 —— 那样模型看到的细节最多、也最锐。
    // 我们先前钉在引导图上是为了躲开死机, 而死机真因是 MV 乘了两遍(已修)。
    const bool full_model = (carrier::cfg.modelfull != 0);
    const UINT build_gw = s_want_gw ? s_want_gw : (s_gw_override ? s_gw_override : w);
    const UINT build_gh = s_want_gh ? s_want_gh : (s_gh_override ? s_gh_override : h);
    const UINT base_w = full_model ? w : build_gw;
    const UINT base_h = full_model ? h : build_gh;

    UINT sw = (static_cast<UINT>(static_cast<unsigned long long>(base_w) * work / 100)) & ~1u;
    UINT sh = (static_cast<UINT>(static_cast<unsigned long long>(base_h) * work / 100)) & ~1u;
    // ★死守: 模型永远不许超过引导图 —— 越界读的代价是死机, 不是画面难看★
    //   ↓ 这里以前夹的是 base_w(模型基准), 不是引导图。注释写对了, 代码写错了。
    //   modelfull=1 时 base_w 就是整帧宽(比如 5120), 而引导图只有 3413 ——
    //   处理精度拉到 100% 就得到 5120x2160 的模型去读 3413x1440 的引导图, 越界。
    //   2026-09-04 燕云十六声实测: 面板点「150」当场重建成 5120x2160, 日志到此为止。
    const UINT guide_w = (s_gw_override != 0) ? s_gw_override : w;
    const UINT guide_h = (s_gh_override != 0) ? s_gh_override : h;
    // ★★这个夹子撤了 —— 是我加错的, 而且在偷偷降画质★★ (2026-09-04 晚)
    //   我照着上面那句注释, 把模型尺寸夹到了引导图大小, 理由是「越界读会死机」。
    //   但实测反过来: 巫师 3 用模型 5120x2160 / 引导图 3413x1440 连跑 151 秒没事,
    //   燕云同样的配比跑了好几千帧、14 毫秒一帧, 稳得很 —— 模型比引导图大是
    //   【整帧基准】的正常形态, NGX 本来就按 subrect 收引导图(evaluate 单独传 gw/gh)。
    //   而夹上之后, 开着 DLSS 超分的游戏一律被砍到渲染分辨率: 5120x2160 变 3413x1440,
    //   像素少了三分之二 —— 业主选「整帧 + 满精度」就是要那份细节, 我们却默默给降了。
    //   业主原话:「我们改来改去画面都没有其他人的好」。这一条就是原因之一。
    //   真正会死机的是别的事(引导图克隆尺寸对不上), 不是这个。
    (void)guide_w; (void)guide_h;
    // ★不再把模型夹到基准尺寸★ 那道夹子正是挡住「超采样」的东西。
    //   参考实现明确允许模型比画面大(WorkingScale 上限 2.0), NGX 本来就按 subrect
    //   收引导图, 模型大过引导图是【整帧基准】的正常形态。
    //   只留一条硬上限: 不许超过基准的两倍(跟参考实现一致), 免得手滑把显存打爆。
    const UINT cap_w = base_w * 2u, cap_h = base_h * 2u;
    if (sw > cap_w) sw = cap_w & ~1u;
    if (sh > cap_h) sh = cap_h & ~1u;
    if (sw < 64 || sh < 64) { s_note = "尺寸太小"; return false; }

    const DXGI_FORMAT model_fmt = t == DXGI_FORMAT_R32G32B32A32_FLOAT ? t : DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!MakeTexOn(dev, &s_full,  "full",  w,  h,  t, D3D12_RESOURCE_STATE_COPY_DEST) ||
        !MakeTexOn(dev, &s_small, "small", sw, sh, model_fmt, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexOn(dev, &s_out,   "out",   sw, sh, model_fmt, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeTexOn(dev, &s_res,   "res",   w,  h,  t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
    { Release(); s_note = "建纹理失败"; return false; }

    // ★★硬闸: 账上还有别人的 feature 18 就绝不建★★
    //   两个同时活 = DXGI_ERROR_DEVICE_REMOVED, 显卡当场掉线(燕云/审判之眼各实测过)。
    //   账本看不见 renodx 走运行库那扇门(它钩不得), 但看得见薄壳/核心那两扇门 ——
    //   能拦多少拦多少, 拦不住的那部分交给 handover 的静置期。
    if (carrier::RenodxOwns18() || nrscale::other_owns())
    {
        s_note = "等主插件先放下神经渲染";
        s_build_defer = true;          // 「还没轮到」不是失败, 别把自己永久关掉
        return false;
    }
    load_ngx_once();   // 帮手里没人替我们加载过 NGX 入口点
    if (!nrfwd::init(dev, engine033::Module()))
    { Release(); s_note = nrfwd::note(); return false; }

    DWORD seh = 0;
    s_feat = nrfwd::create(dev, cl, sw, sh, carrier::cfg.preset, carrier::cfg.intensity,
                           carrier::cfg.style, carrier::cfg.local_structure, carrier::cfg.local_tone,
                           carrier::cfg.skin_structure, carrier::cfg.auto_mask,
                           carrier::cfg.ui_correct, &seh, carrier::cfg.global_tone);
    if(seh || nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return false;}
    // ★兜底★ 万一「停着的旧 feature + 新建」真的冲突(全进程只许有一个 feature 18),
    //   就把停车场强行清空再试一次。正常情况下这条永远不会走到。
    if (s_feat == nullptr && AnyParkedFeature())
    {
        Log("[hostnr] 建不出来, 先把停着的旧模型强行放掉再试一次");
        ReleaseParkedFeaturesNow();
        seh = 0;
        s_feat = nrfwd::create(dev, cl, sw, sh, carrier::cfg.preset, carrier::cfg.intensity,
                               carrier::cfg.style, carrier::cfg.local_structure, carrier::cfg.local_tone,
                               carrier::cfg.skin_structure, carrier::cfg.auto_mask,
                               carrier::cfg.ui_correct, &seh, carrier::cfg.global_tone);
        if(seh || nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return false;}
    }
    if (s_feat == nullptr)
    {
        Release();
        char b[128];
        std::snprintf(b, sizeof(b), "建 feature 失败 (init=0x%08X create=0x%08X seh=0x%08X)",
                      nrfwd::last_init(), nrfwd::last_create(), (unsigned)seh);
        s_note = b;
        // ★就地插入这条路没成 → 把 feature 18 还给交换链★
        //   否则两边都不做, 用户一点效果都没有 —— 那比退回旧做法更糟。
        if (s_defer_build) carrier::g_inject_dead = true;
        return false;
    }

    if(!ConfigurePasses(dev,cl,sw,sh,model_fmt)){
        if(nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return false;}
        if(!s_build_defer)Release();return false;
    }
    ++s_full_builds;s_built_appearance=carrier::TuneSig(false);s_last_build_ms=double(GetTickCount64()-started);
    s_dev = dev; s_w = w; s_h = h; s_sw = sw; s_sh = sh; s_fmt = t; s_model_fmt = model_fmt; s_built_work = work;
    s_built_full = full_model ? 1 : 0;
    s_hist_i = 0; s_hist_age = 0;
    s_history.invalidate();
    s_build_cool = 30;   // 建完压一小段(ParkTick 每帧泵两次 ≈ 15 帧), 只为防「一次改动建两次」。
                         //   别设大: 冷却 + 防抖 + 退休是串起来加的, 加到两秒多业主就当成"没生效"了。
    s_size_hold = 0; s_pending_size = 0xFFFFFFFFu;
    s_geom_hold = 0; s_pending_geom = 0xFFFFFFFFu;
    s_built_tune = carrier::TuneSig(); s_pending_tune = s_built_tune; s_tune_hold = 0;
    // ★记的必须是【引导图】尺寸, 不是模型基准★
    //   modelfull=1 时 base_w 是整帧宽(5120), 而下面比较用的是真引导宽(2970),
    //   记错了就永远不相等 —— 实测每帧重建一次 feature, 日志 90 秒刷了 681 行,
    //   GPU 只有 31%(卡的不是渲染, 是每帧重建)。
    s_built_gw = (s_want_gw != 0) ? s_want_gw : ((s_gw_override != 0) ? s_gw_override : w);
    s_built_gh = (s_want_gh != 0) ? s_want_gh : ((s_gh_override != 0) ? s_gh_override : h);
    s_note.clear();   // 建成了, 把「切换中」那类临时状态字清掉
    Log("[hostnr] ★就绪★ 画面 %ux%u → 模型 %ux%u (%d%%)", w, h, sw, sh, work);
    Log("[hostnr] independent model passes=%d; each history advances once per real frame",s_built_passes);
    // 预设到底进没进运行库 —— 设的值 vs 读回来的值
    Log("[hostnr] 预设参数容器: 写入 %d, 回读 %d%s（不证明模型采用）", nrfwd::preset_set(), nrfwd::preset_back(),
        (nrfwd::preset_back() == nrfwd::preset_set()) ? " (一致)" : " ★对不上★");
    return true;
}

struct ModelBank {
    decltype(s_dev) dev{};
    decltype(s_full) full{};
    decltype(s_small) model_input{};
    decltype(s_out) out{};
    decltype(s_res) res{};
    decltype(s_spare_full) spare_full{};
    decltype(s_spare_res) spare_res{};
    decltype(s_spare_fmt) spare_fmt{};
    decltype(s_feat) feat{};
    decltype(s_extra_out) extra_out{};
    decltype(s_extra_alt) extra_alt{};
    decltype(s_pass_input) pass_input{};
    decltype(s_pass_input3) pass_input3{};
    decltype(s_built_passes) built_passes{};
    decltype(s_selected_passes) selected_passes{};
    decltype(s_built_passwork) built_passwork{};
    decltype(s_built_passwork3) built_passwork3{};
    decltype(s_built_appearance) built_appearance{};
    decltype(s_ew) ew{};
    decltype(s_eh) eh{};
    decltype(s_tw) tw{};
    decltype(s_th) th{};
    decltype(s_w) w{};
    decltype(s_h) h{};
    decltype(s_sw) sw{};
    decltype(s_sh) sh{};
    decltype(s_built_work) built_work{};
    decltype(s_built_full) built_full{};
    decltype(s_built_tune) built_tune{};
    decltype(s_built_gw) built_gw{};
    decltype(s_built_gh) built_gh{};
    decltype(s_fmt) fmt{};
    decltype(s_model_fmt) model_fmt{};
    decltype(s_hist_i) hist_i{};
    decltype(s_hist_age) hist_age{};
    decltype(s_history) history{};
    decltype(s_hold_depth) hold_depth{};
    decltype(s_hold_motion) hold_motion{};
    decltype(s_hold_white) hold_white{};
    decltype(s_hold_key) hold_key{};
    decltype(s_hold_active) hold_active{};
    decltype(s_first_generation) first_generation{};
    decltype(s_model_cfg) model_cfg{};
    decltype(s_extra_feat) extra_feat{};
    decltype(s_hist) hist{};
    decltype(s_refined) refined{};
    decltype(s_pass_frame) pass_frame{};
};
static ModelBank ReadBank(){ModelBank b;
    b.dev=s_dev;
    b.full=s_full;
    b.model_input=s_small;
    b.out=s_out;
    b.res=s_res;
    b.spare_full=s_spare_full;b.spare_res=s_spare_res;b.spare_fmt=s_spare_fmt;
    b.feat=s_feat;
    b.extra_out=s_extra_out;
    b.extra_alt=s_extra_alt;
    b.pass_input=s_pass_input;
    b.pass_input3=s_pass_input3;
    b.built_passes=s_built_passes;
    b.selected_passes=s_selected_passes;
    b.built_passwork=s_built_passwork;
    b.built_passwork3=s_built_passwork3;
    b.built_appearance=s_built_appearance;
    b.ew=s_ew;
    b.eh=s_eh;
    b.tw=s_tw;b.th=s_th;
    b.w=s_w;
    b.h=s_h;
    b.sw=s_sw;
    b.sh=s_sh;
    b.built_work=s_built_work;
    b.built_full=s_built_full;
    b.built_tune=s_built_tune;
    b.built_gw=s_built_gw;
    b.built_gh=s_built_gh;
    b.fmt=s_fmt;
    b.model_fmt=s_model_fmt;
    b.hist_i=s_hist_i;
    b.hist_age=s_hist_age;
    b.history=s_history;
    b.hold_depth=s_hold_depth;
    b.hold_motion=s_hold_motion;
    b.hold_white=s_hold_white;
    b.hold_key=s_hold_key;
    b.hold_active=s_hold_active;
    b.first_generation=s_first_generation;
    b.model_cfg=s_model_cfg;
    for(int i=0;i<nrfeatures::MaxPasses-1;++i)b.extra_feat[i]=s_extra_feat[i];
    for(int i=0;i<2;++i)b.hist[i]=s_hist[i];
    for(int i=0;i<2;++i)b.refined[i]=s_refined[i];
    for(int i=0;i<nrfeatures::MaxPasses;++i)b.pass_frame[i]=s_pass_frame[i];
    return b;
}
static void WriteBank(const ModelBank& b){
    s_dev=b.dev;
    s_full=b.full;
    s_small=b.model_input;
    s_out=b.out;
    s_res=b.res;
    s_spare_full=b.spare_full;s_spare_res=b.spare_res;s_spare_fmt=b.spare_fmt;
    s_feat=b.feat;
    s_extra_out=b.extra_out;
    s_extra_alt=b.extra_alt;
    s_pass_input=b.pass_input;
    s_pass_input3=b.pass_input3;
    s_built_passes=b.built_passes;
    s_selected_passes=b.selected_passes;
    s_built_passwork=b.built_passwork;
    s_built_passwork3=b.built_passwork3;
    s_built_appearance=b.built_appearance;
    s_ew=b.ew;
    s_eh=b.eh;
    s_tw=b.tw;s_th=b.th;
    s_w=b.w;
    s_h=b.h;
    s_sw=b.sw;
    s_sh=b.sh;
    s_built_work=b.built_work;
    s_built_full=b.built_full;
    s_built_tune=b.built_tune;
    s_built_gw=b.built_gw;
    s_built_gh=b.built_gh;
    s_fmt=b.fmt;
    s_model_fmt=b.model_fmt;
    s_hist_i=b.hist_i;
    s_hist_age=b.hist_age;
    s_history=b.history;
    s_hold_depth=b.hold_depth;
    s_hold_motion=b.hold_motion;
    s_hold_white=b.hold_white;
    s_hold_key=b.hold_key;
    s_hold_active=b.hold_active;
    s_first_generation=b.first_generation;
    s_model_cfg=b.model_cfg;
    for(int i=0;i<nrfeatures::MaxPasses-1;++i)s_extra_feat[i]=b.extra_feat[i];
    for(int i=0;i<2;++i)s_hist[i]=b.hist[i];
    for(int i=0;i<2;++i)s_refined[i]=b.refined[i];
    for(int i=0;i<nrfeatures::MaxPasses;++i)s_pass_frame[i]=b.pass_frame[i];
}
static std::string s_build_note;
static void BuildWait(const char* reason){
    const auto now=GetTickCount64();static ULONGLONG reported=0;
    const bool changed=s_build_note!=reason;s_build_note=reason;
    if(!changed && now-reported<5000)return;reported=now;
    Log("[hostnr switch wait] %s; VRAM_MiB=%llu budget_MiB=%llu",reason,s_memory_usage>>20,s_memory_budget>>20);
    if(AnyParkedFeature())for(const auto& p:s_parked)if(p.feat){
        const auto r=resolveleases::InspectRange(p.first,reinterpret_cast<uintptr_t>(p.tag));
        Log("[hostnr retired bank] generation=%llu..%llu active=%u gpu_or_unknown=%u replayable=%u allocator_unknown=%u oldest_ms=%llu",
            static_cast<unsigned long long>(p.first),static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p.tag)),
            r.active,r.gpu,r.replayable,r.unknownAllocator,r.oldestMs);break;
    }
}
static ModelBank s_candidate;
// A faulted, partially built bank cannot be retired or submitted safely.
// Raw owned references stay here until process exit; never a reusable candidate.
static ModelBank s_quarantined_candidate;
static bool s_candidate_valid=false;
static ModelBank s_candidate_borrowed;
static uint64_t s_candidate_revision=0;
static bool s_candidate_reused=false;
static void RetireBank(const ModelBank& old,const ModelBank& borrowed=ModelBank{}){
    if(nrfault033::Blocked())return;
    auto live=ReadBank();auto retired=old;nrlayers::ExcludeBorrowed(retired,live);
    nrlayers::ExcludeBorrowed(retired,borrowed);
    WriteBank(retired);Release();WriteBank(live);
}
static bool CanReuseLayerBank(const ModelBank& b){
    return b.feat && nrdistribution033::GeometryMatches(
        {b.dev,b.w,b.h,b.built_gw,b.built_gh,int(b.fmt)},
        {s_want_dev,s_want_w,s_want_h,s_want_gw,s_want_gh,int(carrier::TypedColorFormat(s_want_fmt))},
        false,false) &&
        b.built_work==carrier::cfg.work && b.built_full==(carrier::cfg.modelfull?1:0);
}
static bool BuildLayerCandidate(ModelBank& candidate,const ModelBank& live,ID3D12Device* dev,ID3D12GraphicsCommandList* cl,unsigned replaceMask=0){
    if(nrfault033::Blocked())return false;
    candidate=live;const int count=nrfeatures::ClampPasses(carrier::cfg.passes);
    nrlayersr::ResizeCandidate(candidate,live,carrier::cfg.passwork,carrier::cfg.passwork3);
    const auto layout=nrlayersr::Of(candidate);
    auto texture=[&](ID3D12Resource*& r,const char* name,UINT w,UINT h){
        return r || MakeTexOn(dev,&r,name,w,h,candidate.model_fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    };
    if(count>1){
        if(!texture(candidate.extra_out,"layer-output",candidate.ew,candidate.eh) ||
           !texture(candidate.pass_input,"layer-input",candidate.ew,candidate.eh))return false;
        if(count>2 && (!texture(candidate.extra_alt,"layer-output-3",candidate.tw,candidate.th) ||
            !texture(candidate.pass_input3,"layer-input-3",candidate.tw,candidate.th)))return false;
        if(nrlayersr::AnyScaled(layout,count))
            for(auto& r:candidate.refined)if(!texture(r,"layer-refined",candidate.sw,candidate.sh))return false;
    }
    if(count==1 && !texture(candidate.refined[0],"single-layer-final-condition",candidate.sw,candidate.sh))return false;
    nrrecovery033::RequestFresh(candidate,replaceMask&((1u<<count)-1u));
    const bool built=nrlayers::ReplaceFeatures(candidate,live,carrier::cfg,[&](int pass,const nrlayers::Model& tune,void*& feature){
        if(nrfault033::Blocked())return false;
        DWORD seh=0;
        const auto size=layout.layer[pass];
        feature=nrfwd::create(dev,cl,size.width,size.height,
            tune.preset,tune.intensity,tune.style,tune.structure,tune.tone,tune.skin,tune.autoMask,tune.uiCorrect,&seh,tune.globalTone);
        Log("[hostnr layer candidate] layer=%d style=%d preset=%d intensity=%.9g structure=%.9g tone=%.9g skin=%.9g globalTone=%.9g autoMask=%d ui=%d created=%d seh=0x%08X size=%ux%u; runtime adoption not verified",
            pass+1,tune.style,tune.preset,tune.intensity,tune.structure,tune.tone,tune.skin,tune.globalTone,tune.autoMask,tune.uiCorrect,feature?1:0,unsigned(seh),size.width,size.height);
        return feature && !seh && !nrfault033::Blocked();
    });
    if(!built)return false;
    candidate.built_tune=carrier::TuneSig();candidate.built_appearance=carrier::TuneSig(false);
    return true;
}
// 在 present 里调 —— 不在任何 NGX 钩子的调用栈里, 所以建 feature 安全。
// 只在就地插入路(s_defer_build)上有活干。
// Ordinary failures keep their ownership and are retried by the existing pump.
// Never reset the SEH latch, replay an initialization list, or fake completion.
static bool EnsureBlitter(ID3D12Device* dev){
    if(s_blit.ready)return true;
    const auto now=GetTickCount64();
    if(!s_blit_retry.Due(now))return false;
    if(!scale::Create(s_blit,dev)){
        s_blit_retry.Failed(now);s_auto_recovery.fetch_or(1);
        s_note=s_blit.error;
        if(s_blit_retry.failures<=4)Log("[hostnr recovery] pipeline creation failed; retry=%u error=%s",s_blit_retry.failures,s_note.c_str());
        // Create has not published or submitted these partial objects.
        scale::Destroy(s_blit);return false;
    }
    s_blit_retry.Success();s_auto_recovery.fetch_and(~1u);s_note.clear();
    return true;
}
static bool ConfirmInitialization(){
    if(!s_signal_pending)return true;
    const auto now=GetTickCount64();
    if(!s_signal_retry.Due(now))return false;
    // Device removal reports UINT64_MAX, not completion of the submitted work.
    if(!s_bq || !s_bf || s_bf->GetCompletedValue()==UINT64_MAX){
        s_signal_retry.Failed(now);s_auto_recovery.fetch_or(2);
        BuildWait("初始化完成状态不可用，保留模型资源");return false;
    }
    const auto hr=s_bq->Signal(s_bf,s_bv);
    if(FAILED(hr)){
        s_signal_retry.Failed(now);s_auto_recovery.fetch_or(2);
        if(s_signal_retry.failures<=4)Log("[hostnr recovery] initialization signal failed hr=0x%08X retry=%u value=%llu",unsigned(hr),s_signal_retry.failures,s_bv);
        BuildWait("初始化提交未确认，正在自动重试；当前模型继续运行");return false;
    }
    s_signal_pending=false;s_signal_retry.Success();s_auto_recovery.fetch_and(~2u);
    return true;
}
static void PumpBuild()
{
    if(nrfault033::Blocked())return;
    nrdispatch::WriterAccess writer;if(!writer.entered)return;
    if(s_building.load()){
        if(!ConfirmInitialization())return;
        const auto done=s_bf?s_bf->GetCompletedValue():0;
        if(!nrlayers::InitializationComplete(done,s_bv)){BuildWait("等待新模型初始化完成，当前模型继续运行");return;}
        s_building=false;
        if(s_candidate_valid){
            const bool current=nrlayers::BorrowStillValid(s_candidate_reused,s_candidate_revision,s_bank_revision) &&
                nrlayers::Matches(s_candidate.model_cfg,carrier::cfg,nrfeatures::ClampPasses(carrier::cfg.passes)) &&
                s_candidate.built_tune==carrier::TuneSig() && s_candidate.built_work==carrier::cfg.work && s_candidate.built_full==(carrier::cfg.modelfull?1:0) &&
                s_candidate.built_passwork==carrier::cfg.passwork && s_candidate.built_passwork3==carrier::cfg.passwork3 &&
                nrdistribution033::GeometryMatches(
                    {s_candidate.dev,s_candidate.w,s_candidate.h,s_candidate.built_gw,s_candidate.built_gh,int(s_candidate.fmt)},
                    {s_want_dev,s_want_w,s_want_h,s_want_gw,s_want_gh,int(carrier::TypedColorFormat(s_want_fmt))},
                    false,true);
            if(current){
                auto old=ReadBank();
                // Shared objects keep their earliest lease generation. A new
                // generation alone would forget still-replayable older lists.
                const auto replaced=nrrecovery033::Replaced(old,s_candidate);
                WriteBank(s_candidate);++s_generation;
                s_evaluate_recovery.Adopted(replaced,GetTickCount64());
                for(auto& frame:s_pass_frame)frame=0;s_hist_age=0;
                s_history.invalidate();RetireBank(old);s_want_build=false;s_grace=0;s_build_cool=0;s_build_note.clear();
                Log("[hostnr hot switch] candidate activated; old model rendered until initialization completed; passes=%d",s_selected_passes);
            }else{
                RetireBank(s_candidate,s_candidate_borrowed);s_want_build=true;
                Log("[hostnr hot switch] superseded candidate retired; current model remains active");
            }
            if(nrfault033::Blocked())return;
            s_candidate={};s_candidate_borrowed={};s_candidate_valid=false;s_candidate_reused=false;
        }
        ParkTick();return;
    }
    ParkTick();
    if(!s_want_build){s_build_note.clear();return;}
    if(nrfault033::Blocked())return;
    if(s_failed || !s_want_dev){BuildWait("模型准备路径不可用，设置尚未应用");return;}
    if(s_retry_cool>0){BuildWait("上次模型准备失败，等待重试");return;}
    // Bound overlap to active + one retired/candidate bank. No resource release
    // based on elapsed time, and no need to blank current NR while waiting.
    const bool drainResize=nrresize033::DrainBeforeBuild(
        nrinput033::ownership.Get()==nrinput033::Route::Presentation,s_feat!=nullptr,
        {s_dev,s_w,s_h,s_built_gw,s_built_gh,int(s_fmt)},
        {s_want_dev,s_want_w,s_want_h,s_want_gw,s_want_gh,int(carrier::TypedColorFormat(s_want_fmt))});
    if(!nrresize033::Prepare(drainResize,[&]{
            Log("[hostnr resize] retiring old model %ux%u before %ux%u creation; pending GPU/list leases must retire",s_w,s_h,s_want_w,s_want_h);
            Release();
        },[]{ParkTick();},[]{return AnyParkedFeature();})){
        BuildWait(s_feat?"等待旧模型资源回收，设置尚未应用":"正在回收旧尺寸模型，游戏画面继续");return;
    }
    if(nrfault033::Blocked())return;
    if(carrier::g.nr_feat){BuildWait("等待另一条神经渲染路径释放模型");return;}
    ID3D12Device* dev=s_want_dev;
    if(!EnsureBlitter(dev)){BuildWait("NR 资源准备失败，正在自动重试");return;}
    if(!EnsureBuilder(dev)){BuildWait("模型准备队列不可用");return;}
    SampleMemory(dev);
    const UINT64 thirdBoundW=(std::max)(UINT64(64),UINT64((std::max)(s_want_w,s_want_gw))*2u);
    const UINT64 thirdBoundH=(std::max)(UINT64(64),UINT64((std::max)(s_want_h,s_want_gh))*2u);
    const UINT64 thirdInputBound=thirdBoundW*thirdBoundH*16u;
    const UINT64 estimated=UINT64(s_want_w)*s_want_h*(64u+128u*(std::max)(1,carrier::cfg.passes))+
        (carrier::cfg.passes>2?thirdInputBound:0u); // Up to RGBA32F; first NR dimensions may be 200% per axis.
    if(s_feat && s_memory_budget && (s_memory_usage>=s_memory_budget || estimated>s_memory_budget-s_memory_usage)){
        BuildWait("显存余量不足，未创建新模型；当前模型继续运行");return;
    }
    if(FAILED(s_ba->Reset()) || FAILED(s_bl->Reset(s_ba,nullptr))){BuildWait("模型准备命令重置失败");return;}
    auto current=ReadBank();ModelBank candidate{};
    BuildWait("正在创建有变化的模型层");s_cpu_building=true;s_build_defer=false;
    nrdispatch::LongStep longBuild; // Frames skip this CPU build at once instead of waiting out the maintenance grace.
    const bool previousInjectDead=carrier::g_inject_dead;bool built=false;
    const auto started=GetTickCount64();
    const bool reuse=CanReuseLayerBank(current);const auto revision=s_bank_revision;
    if(reuse){
        built=BuildLayerCandidate(candidate,current,dev,s_bl,s_evaluate_recovery.mask);
        if(built)++s_pass_builds;
    }else{
        WriteBank(ModelBank{});s_first_generation=++s_generation;
        built=Build(dev,s_bl,s_want_w,s_want_h,s_want_fmt);
        candidate=ReadBank();WriteBank(current);
    }
    if(current.feat)carrier::g_inject_dead=previousInjectDead;
    s_cpu_building=false;longBuild.end();s_last_build_ms=double(GetTickCount64()-started);
    if(nrfault033::Blocked()){
        s_quarantined_candidate=candidate;s_failed=true;s_want_build=false;
        s_note=nrfault033::Note();return; // No Close/Reset/submit/retire after the fault.
    }
    if(FAILED(s_bl->Close()) || !built){
        RetireBank(candidate,reuse?current:ModelBank{});BuildWait("新模型准备失败，继续当前模型");s_retry_cool=120;
        return; // Ordinary failure keeps the existing bounded retry policy.
    }
    s_candidate_borrowed=reuse?current:ModelBank{};s_candidate_reused=reuse;s_candidate_revision=revision;
    s_candidate=candidate;s_candidate_valid=true;s_building=true;s_want_build=false;
    ID3D12CommandList* lists[]={s_bl};s_bq->ExecuteCommandLists(1,lists);
    ++s_bv;s_signal_pending=true;s_signal_retry.Success();
    if(ConfirmInitialization())BuildWait("等待新模型初始化完成，当前模型继续运行");
}

// 帮手每帧调这里。cl 是它的命令列表, 已经在录制中; 我们只往里加命令, 不提交。
// mvScaleX/Y、depthInverted、reset 全部由宿主传过来 —— 那是游戏那边的编码约定,
// 猜不得。写死成 1.0 的下场: 模型按错的位移对齐上一帧, 时域一累积满屏红斑。
static int __cdecl Stage(ID3D12GraphicsCommandList *cl, ID3D12Device *dev,
                         ID3D12Resource *color_inout, ID3D12Resource *depth, ID3D12Resource *mv,
                         unsigned w, unsigned h,
                         float mvScaleX, float mvScaleY, int depthInverted, int reset)
{
    if(nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return 0;}
    if (!rendercore::AllowHost() || s_cpu_building.load()) return 0;
    nrdispatch::WriterAccess writer(true);
    if(!writer.entered)return 0;
    nrfeatures::Restrict(carrier::cfg);
    // 已经让位给交换链了, 这条路从此不再碰游戏的命令列表
    if (s_defer_build && s_yielded) return 0;

    ParkTick();
    if (s_failed || nrfault033::Blocked()) return 0;
    if (cl == nullptr || dev == nullptr || color_inout == nullptr)
    {
        static bool said = false;
        if (!said) { said = true; Log("[hostnr] Stage 拿到空指针 (cl=%p dev=%p color=%p)",
                                      (void*)cl, (void*)dev, (void*)color_inout); }
        return 0;
    }
    s_has_depth = nrinput033::HasNativeGuide(depth != nullptr);
    s_has_mv    = nrinput033::HasNativeGuide(mv != nullptr);
    reset=nrinput033::Reset(reset);
    // 面板请求清历史: 跟下面「游戏切换颜色格式」那条路做的是同样三件事。
    if(s_history_reset_request.exchange(false,std::memory_order_acq_rel)){
        invalidate_history();for(auto& frame:s_pass_frame)frame=0;reset=1;
        Log("[hostnr] 面板请求: 时间历史已清空, 本帧 InReset=1; 模型与纹理不动");
    }
    if (!carrier::cfg.enabled) return 0;

    if (!EnsureBlitter(dev))
    { return 0; }

    const bool holdRequested=carrier::cfg.holdframe!=0;
    // A frozen HDR frame must never be copied into an SDR output (or vice versa).
    if(s_hold_active && (s_dev!=dev || s_w!=w || s_h!=h || s_fmt!=carrier::TypedColorFormat(color_inout->GetDesc().Format))) {
        Park(s_hold_depth);Park(s_hold_motion);Park(s_hold_white);++s_generation;s_hold_active=false;invalidate_history();
    }
    if(!holdRequested && s_hold_active){Park(s_hold_depth);Park(s_hold_motion);Park(s_hold_white);++s_generation;s_hold_active=false;invalidate_history();}
    if(holdRequested && s_hold_active && s_w==w && s_h==h) {
        depth=s_hold_depth;mv=s_hold_motion;
        mvScaleX=s_hold_key.scaleX;mvScaleY=s_hold_key.scaleY;depthInverted=s_hold_key.depthInverted;
        set_guide_rects(nrcontract::Guides{sizeof(nrcontract::Guides),s_hold_key.depth,s_hold_key.motion});
        s_stream_key=s_hold_key.stream;
    }
    // 引导图尺寸变了也得重建 —— 玩家在游戏里改 DLSS 档位, 渲染分辨率就变了,
    // 不重建的话模型尺寸还停在旧值, 又会出现「模型比引导图大」那个死机条件。
    const UINT now_gw = (s_gw_override != 0) ? s_gw_override : w;
    const UINT now_gh = (s_gh_override != 0) ? s_gh_override : h;
    s_want_gw = now_gw; s_want_gh = now_gh;   // 交给 Build, 免得两个时刻取到不同的值
    // ★一切改动【立刻】生效 —— 不防抖★ (业主 2026-09-04 夜拍板)
    //   防抖是我为了压住一个还没查清的重建抖动加的, 代价是「改了要等」。
    //   业主的要求是"不管怎么改都能实时生效", 那就把等待全去掉,
    //   只留建造冷却防「一次改动建两次」。
    const DXGI_FORMAT incomingFormat=carrier::TypedColorFormat(color_inout->GetDesc().Format);
    const DXGI_FORMAT incomingModelFormat=incomingFormat==DXGI_FORMAT_R32G32B32A32_FLOAT
        ?incomingFormat:DXGI_FORMAT_R16G16B16A16_FLOAT;
    const bool outputCompatible=nroutput::Compatible(
        {s_dev,s_w,s_h,s_built_gw,s_built_gh,s_built_work,s_built_full,int(s_model_fmt)},
        {dev,w,h,now_gw,now_gh,carrier::cfg.work,carrier::cfg.modelfull?1:0,int(incomingModelFormat)});
    static ULONGLONG nextColourAllocation=0;
    if(s_feat && outputCompatible && incomingFormat!=DXGI_FORMAT_UNKNOWN && s_fmt!=incomingFormat && !s_building &&
        (s_spare_fmt==incomingFormat || GetTickCount64()>=nextColourAllocation)) {
        const auto oldFormat=s_fmt;
        const auto switched=nroutput::Switch(s_full,s_res,s_fmt,s_spare_full,s_spare_res,s_spare_fmt,incomingFormat,
            [&](ID3D12Resource*& full,ID3D12Resource*& result,DXGI_FORMAT format) {
                SampleMemory(dev);
                const UINT64 reserve=UINT64(w)*h*32u;
                if(s_memory_budget && (s_memory_usage>=s_memory_budget || reserve>s_memory_budget-s_memory_usage))return false;
                return MakeTexOn(dev,&full,"colour-switch-full",w,h,format,D3D12_RESOURCE_STATE_COPY_DEST) &&
                    MakeTexOn(dev,&result,"colour-switch-resolve",w,h,format,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            },[](ID3D12Resource* resource){resource->Release();});
        if(switched==nroutput::Result::Failed)nextColourAllocation=GetTickCount64()+1000;
        if(switched==nroutput::Result::Allocated || switched==nroutput::Result::Cached) {
            invalidate_history();for(auto& frame:s_pass_frame)frame=0;reset=1;
            Log("[hostnr colour switch] %d -> %d; existing NR model retained; output_pair=%s; temporal history reset",
                int(oldFormat),int(s_fmt),switched==nroutput::Result::Cached?"cached":"allocated");
        }
    }
    const bool cacheFits=s_feat && outputCompatible && s_fmt==incomingFormat && nrlayers::Matches(s_model_cfg,carrier::cfg,carrier::cfg.passes) &&
        s_built_work==carrier::cfg.work && s_built_full==(carrier::cfg.modelfull?1:0) &&
        s_built_passwork==carrier::cfg.passwork && s_built_passwork3==carrier::cfg.passwork3 && carrier::cfg.passes<=s_built_passes;
    if(cacheFits){
        if(s_selected_passes!=carrier::cfg.passes){
            s_selected_passes=carrier::cfg.passes;++s_pass_builds;
            Log("[hostnr hot switch] cached layer count=%d applied on current frame; capacity=%d",s_selected_passes,s_built_passes);
        }
        s_built_tune=carrier::TuneSig();if(!s_building && !s_evaluate_recovery.Pending(carrier::cfg.passes))s_want_build=false;
    }
    // 调参改了 → 等它拖稳了再重建
    bool tune_ready = false;
    {
        const unsigned tune = carrier::TuneSig();
        // ★开着帧生成时, 画质参数不重建 feature★ (2026-09-04 晚, 龙之信条 2 + 燕云十六声)
        //   重建 feature 18 是这条路上最危险的一步: 驱动那边要拆掉旧的、建新的,
        //   而帧生成让 GPU 落后好几帧 —— 两次实测都是「点一下画质参数, 几秒后闪退」,
        //   而且我们这边日志一切正常、没有设备移除、没有 ReShade 错误, 崩得无声无息。
        //   上游也有同一条经验: 模型参数在创建时锁存, 频繁重建会耗尽驱动的 latch,
        //   「feature 停止响应直到进程重启」。
        //   所以有帧生成时就不重建了 —— 参数留到下次进游戏生效, 总比崩了强。
        // ★不再按帧生成锁死参数★ 崩的根因是「在 GPU 还用着的时候释放旧 feature」,
        //   已经在 Release()/Build() 那对闸里治掉了(退休→隔开→再建), 这里放开。
        if (tune != s_built_tune)
        {
            if (tune != s_pending_tune) { s_pending_tune = tune; s_tune_hold = 0; }
            else if (s_tune_hold < 1)  { ++s_tune_hold; }
            else                        { tune_ready = true; }
        }
    }

    const int cfg_work = carrier::cfg.work;
    const int cfg_full = carrier::cfg.modelfull ? 1 : 0;
    const bool size_ready = (s_built_work != cfg_work || s_built_full != cfg_full);
    const bool geom_ready = (s_dev != dev || s_w != w || s_h != h || s_built_gw != now_gw || s_built_gh != now_gh ||
        s_fmt != carrier::TypedColorFormat(color_inout->GetDesc().Format));

    const bool want_rebuild =
        (s_feat == nullptr || tune_ready || size_ready || geom_ready || s_evaluate_recovery.Pending(carrier::cfg.passes));
    if(want_rebuild){
        s_want_dev=dev;s_want_w=w;s_want_h=h;s_want_fmt=color_inout->GetDesc().Format;
        if(!s_building && !s_want_build){
            s_want_build=true;BuildWait("等待准备新模型，当前模型继续运行");
        }
        if(!s_feat || geom_ready)return 0;
    }
    yanyundual::Want(dev,w,h,now_gw,now_gh,s_fmt,s_memory_usage,s_memory_budget);
    if (s_grace > 0) { --s_grace; return 0; }

    // restorestate 开着时，没有捕获到可完整还原的计算态就整帧跳过，绝不猜绑。
    // ★这道闸只对【就地插入路】有意义★ (s_defer_build = inject 打开时置 1):
    //   那条路我们把命令录进【游戏自己的】命令列表, 弄脏了游戏接着画就掉设备,
    //   所以宁可跳帧。而 Feeder 帮手这条路用的是【我们自己的】命令列表,
    //   压根没有游戏状态可污染 —— 闸门在这儿永远抓不到东西, 于是每一帧都判跳,
    //   神经渲染整局不工作。
    //   2026-09-04 生化危机 4 实测: 全新安装走 Feeder 路 + 白名单自动写的
    //   restorestate=3, 连续跳 6000 帧、一帧 NR 都没跑 —— 业主反馈「DLSS5 起不来」
    //   就是这个。
    if (s_defer_build && nrinput033::NeedsGameState() && !staterestore::allow_frame(cl))
    {
        if (carrier::cfg.autoroute != 0 && !s_yielded && ++s_gate_miss >= 600)
        {
            s_yielded = true;
            Log("[hostnr] 状态闸连续拒绝 %u 帧 —— 就地插入认输, 把神经渲染交回交换链", s_gate_miss);
            Release();                          // 先把 feature 18 交出来(全进程只能有一个)
            ReleaseParkedFeaturesNow();         // 当场还, 不能停车 —— 让位后没人再泵停车场
            nrfwd::drop_external_caps();        // ★把借来的游戏参数块还掉, 否则交换链拿它建必崩★
            carrier::g_yield_grace = 120;      // ★给 NGX 时间真的放干净, 否则交换链建第二个必崩★
            carrier::g_inject_dead = true;      // 交换链看到这个就会接手
        }
        return 0;
    }
    s_gate_miss = 0;

    // ★状态信封★ 从这里往下我们要绑自己的堆/根签名/PSO, 出作用域(含下面所有早退)把游戏的还回去。
    //   bindless 引擎(RE Engine / 怪猎荒野)一条列表只绑一次, 不还就设备移除。见 staterestore.h。
    //   OptiScaler 9/1 那版修的就是这个(它叫 ScopedNrStateEnvelope), 思路一样, 代码是自己的。
    // Immutable bindings and resource references cover the entire NR dispatch,
    // including disabled sharpening. Opaque features retire only after every
    // observed list generation and actual GPU submission has completed.
    if(nrinput033::context.nativeGuides&&nrnative033::enhancedSeen.load())return 0;
    const bool wantHistory=carrier::cfg.replica==0 && carrier::cfg.stabilize>0;
    if(!wantHistory && (s_hist[0]||s_hist[1])){Park(s_hist[0]);Park(s_hist[1]);++s_generation;s_hist_age=0;}
    if(wantHistory && (!s_hist[0]||!s_hist[1])){
        if(!s_hist[0])MakeTexOn(dev,&s_hist[0],"hist0",s_sw,s_sh,s_model_fmt,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if(!s_hist[1])MakeTexOn(dev,&s_hist[1],"hist1",s_sw,s_sh,s_model_fmt,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    // Optional final-picture pass. No model/history rebuild; failure retains NR.
    const float clarityRequested=std::isfinite(carrier::cfg.sharpen)?std::clamp(carrier::cfg.sharpen,0.0f,1.0f):0.0f;
    const float naturalRequested=std::isfinite(carrier::cfg.natural_look)?std::clamp(carrier::cfg.natural_look,0.f,1.f):0.f;
    auto* clarityTarget=s_clarity.Prepare(dev,w,h,int(s_fmt),(clarityRequested>0.0f || naturalRequested>0.0f) && carrier::cfg.applymodel!=0,GetTickCount64(),
        [&](ID3D12Resource*& target){
            SampleMemory(dev);const UINT64 reserve=UINT64(w)*h*16u+65536u;
            if(s_memory_budget && (s_memory_usage>=s_memory_budget || reserve>s_memory_budget-s_memory_usage))return false;
            return MakeTexOn(dev,&target,"final-clarity",w,h,s_fmt,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        },[](ID3D12Resource* resource){resource->Release();});
    const float clarityApplied=clarityTarget?clarityRequested:0.0f;
    const float naturalApplied=clarityTarget?naturalRequested:0.0f;
    static int lastClarity=-1;static unsigned clarityReports=0;
    const int pictureMask=(clarityRequested>0.0f?1:0)|(naturalRequested>0.0f?2:0);
    const int clarityState=pictureMask?(clarityTarget?2:1):0;
    if(clarityState!=lastClarity && clarityReports<16){lastClarity=clarityState;++clarityReports;
        Log("[033 clarity] state=%d (0=off 1=not-applied 2=prepared); requested=%.3f; natural_requested=%.3f natural_prepared=%.3f mask=%d; GPU result unverified",clarityState,clarityRequested,naturalRequested,naturalApplied,pictureMask);}
    nrnormalize033::Prepared normalized;
    if(nrinput033::context.presentation&&nrinput033::context.nativeGuides&&nrinput033::context.normalizeRe4&&!(s_hold_active&&holdRequested)){
        SampleMemory(dev);
        const uint64_t available=s_memory_budget>s_memory_usage?s_memory_budget-s_memory_usage:0;
        if(!nrnormalize033::Prepare(dev,depth,mv,true,s_memory_budget!=0,available,normalized,s_memory_budget)){
            s_note=nrnormalize033::note;return 0;
        }
    }
    // Private conversion descriptors and resources share the original exact
    // submission lease, including list replay/reset retirement requirements.
    const bool anyScaled=(s_selected_passes>1&&(s_ew!=s_sw||s_eh!=s_sh))||(s_selected_passes>2&&(s_tw!=s_sw||s_th!=s_sh));
    auto* finalScratch=nrstack::FinalScratch(s_selected_passes,anyScaled,s_refined[0],s_pass_input);
    // Validate before any NR command; never discover a missing destination after Evaluate.
    // Cached 2/3 -> 1 switches use the retained full-size pass_input when refined[0] is absent.
    if(!finalScratch){s_note="等待完整的 NR 合成资源";static bool reported=false;if(!reported){reported=true;Log("[hostnr] final scratch missing before NR commands; no frame recorded");}return 0;}
    IUnknown* frameRefs[]={clarityTarget,color_inout,depth,mv,s_full,s_small,s_out,s_res,s_extra_out,s_extra_alt,s_pass_input,s_pass_input3,s_refined[0],s_refined[1],s_hist[0],s_hist[1],
        s_blit.rs,s_blit.pso,s_blit.rs_rv,s_blit.pso_rv,s_blit.pso_st,s_blit.pso_stack,
        normalized?normalized.cache->depth.Get():nullptr,normalized?normalized.cache->motion.Get():nullptr,
        normalized?normalized.cache->heap.Get():nullptr,normalized?nrnormalize033::signature.Get():nullptr,
        normalized?nrnormalize033::pipeline.Get():nullptr};
#ifdef K033_BETA2_RESHADE_HOST
    if(!InputCurrent())return 0;
#endif
    auto* frameLease=resolveleases::Begin(dev,cl,frameRefs,sizeof(frameRefs)/sizeof(frameRefs[0]),GenerationTag(),leasewait033::Owner::NR);
    if(!frameLease){s_note="等待可验证的 GPU 资源提交/回收";return 0;}
    struct EndLease {~EndLease(){resolveleases::End();}} endLease;
    scale::Blitter frameBlitter=s_blit;frameBlitter.heap=frameLease->heap;frameBlitter.white_bound=nullptr;
#ifdef K033_BETA2_RESHADE_HOST
    if(!InputCurrent()){resolveleases::CancelUnrecorded(resolveleases::GetTicket(frameLease));return 0;}
#endif

    // No cancellation is permitted after the first resource barrier.
    nrnormalize033::Attach(normalized,frameLease);
    staterestore::Envelope state_env(cl,nrinput033::NeedsGameState());
    nrgame033::ReadScope guideRead(cl,depth,mv,s_hold_active&&holdRequested,
        nrinput033::context.depthPlane0?nrgame033::GuideSubresource::DepthPlane0:nrgame033::GuideSubresource::Whole);
    if(normalized){nrnormalize033::Record(cl,normalized);depth=normalized.cache->depth.Get();mv=normalized.cache->motion.Get();}

    // ★GPU 分段计时★ 从这里往下全是我们的活儿。
    //   以前这条路禁用计时, 因为早退没配 End 会跨命令列表(实测古墓 INVALID_CALL)。
    //   现在每个早退出口都 Abort(), Begin/Stamp/End 全带守卫 —— 见 gputime.h 顶上的纪律。
    //   频率问【游戏的】队列(carrier::g.queue 就是它), 就地插入跑的正是那条队列。
    // 频率要问一条真队列: 就地插入路 carrier::g.queue 是游戏的队列; Feeder 帮手路
    //   (老游戏 / D3D11 桥)上 carrier 根本没初始化, 它是空的 —— 那就用我们自己的 s_bq。
    //   两个都没有就别算了(freq_ok=false, 面板和日志都不显示废数)。
    ID3D12CommandQueue *fq = (carrier::g.queue != nullptr) ? carrier::g.queue : s_bq;
    gputime::EnsureDevice(dev);
    if (!gputime::ready()) gputime::Create(dev, fq);
    else if (!gputime::freq_ok()) gputime::SetFrequencyFrom(fq);
    gputime::Begin(cl);
    gpufault033::NR(cl,s_frames+1,w,h,1);
    auto grade=pregrade::Checked(&carrier::cfg.pre);grade.skinProtection=0.f;
    nrcontract::FrameKey historyKey;
    historyKey.grade=pregrade::Signature(grade);
#ifdef K033_BETA2_RESHADE_HOST
    // Grade changes still reset NR history. Only the actual same-O successful
    // independent grade suppresses the duplicate model-internal grade pass.
    if(input_graded)grade=pregrade::Settings{};
#endif
    historyKey.grade=(historyKey.grade*16777619u)^unsigned(carrier::cfg.white_source);
    historyKey.grade=(historyKey.grade*16777619u)^unsigned(carrier::EffectiveWhite()*1000.f);
    historyKey.stream = s_stream_key;
    historyKey.output = {s_frame_output.x, s_frame_output.y, w, h};
    historyKey.depth = s_have_frame_guides ? s_frame_guides.depth : nrcontract::Rect{0,0,now_gw,now_gh};
    historyKey.motion = s_have_frame_guides ? s_frame_guides.motion : historyKey.depth;
    historyKey.scaleX = mvScaleX; historyKey.scaleY = mvScaleY; historyKey.depthInverted = depthInverted;
    const unsigned resetReason = s_history.reset_reason(historyKey, reset != 0);
    const bool resetHistory = resetReason != 0 || holdRequested;
    if (resetHistory) s_hist_age = 0;
    // S18: the person chain and the recognition identity no longer inherit the
    // scene chain's grade/white/model resets or the applied-recipe revision.
    // The person gate compares only the shared stream/rectangle/guide contract
    // (its own input encoding is compared inside the person bank). A recognition
    // result stays spatially valid across colour, model and recipe changes; its
    // generation changes only with a different stream or output rectangle or a
    // hold toggle, and FrameIdentity still rejects any size change by itself.
    nrcontract::FrameKey sharedKey=historyKey;sharedKey.grade=0;
    const bool personReset=s_person_history.needs_reset(sharedKey,reset!=0)||holdRequested;
    s_person_history.commit(sharedKey);
    static uint64_t semanticEpoch=1;static uintptr_t semanticStream=0;static nrcontract::Rect semanticOutput;
    static bool semanticHold=false,semanticValid=false;
    if(!semanticValid||semanticStream!=historyKey.stream||!(semanticOutput==historyKey.output)||semanticHold!=holdRequested){
        if(semanticValid)++semanticEpoch;
        semanticValid=true;semanticStream=historyKey.stream;semanticOutput=historyKey.output;semanticHold=holdRequested;
    }
    const yanyundual::FrameIdentity personFrame{s_stream_key?s_stream_key:reinterpret_cast<uintptr_t>(color_inout),
        semanticEpoch,s_frames+1,GetTickCount64(),w,h};

    // The existing mode remains the default. An explicit game-exposure source
    // is independent from the reversible composition choice. Never infer
    // exposure from a post-processed image or an unknown resource state.
    ID3D12Resource *frame_white=nullptr;
    s_exposure.lastTick=GetTickCount64();s_exposure.valid=false;
    const int whiteEncoding=carrier::EncodeMode(s_fmt,nrinput033::context.presentation);
    if(s_hold_active && holdRequested){frame_white=s_hold_white;s_exposure.status=exposurepolicy::Status::Frozen;}
    else if(exposurepolicy::UsesGame(carrier::cfg.white_source,carrier::cfg.replica!=0,whiteEncoding)) {
        D3D12_RESOURCE_STATES exp_arrive=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const bool known=carrier::ExposureArrivalState(exp_arrive);
        frame_white=exposure::WhiteForFrame(s_exposure,dev,cl,frameLease,s_stream_key,resetHistory,
            exposurepolicy::ExposureGain(carrier::cfg.white_source,carrier::cfg.whitepoint,carrier::cfg.white_trim),
            carrier::EffectiveWhite(),known,exp_arrive);
    }else{
        s_exposure.previous=-1;s_exposure.sampleTick=0;
        s_exposure.status=whiteEncoding==1?exposurepolicy::Status::Fixed:exposurepolicy::Status::DisplayReferred;
    }
    static auto reportedWhite=exposurepolicy::Status::Waiting;
    if(reportedWhite!=s_exposure.status){reportedWhite=s_exposure.status;
        Log("[033 input white] source=%d status=%u fixed_scale=%.4f: %s",carrier::cfg.white_source,
            unsigned(s_exposure.status),carrier::EffectiveWhite(),exposure::note(s_exposure));}
    resolveleases::Hold(frameLease,frame_white);
    // output 是 UAV 态; 先拷一份满尺寸的出来给模型读
    // GPU 计时: 从这里开始就是我们的活儿
    // ★不许硬假设游戏把 Output 留在 UAV★ 见 carrier::cfg.outstate 那段注释。
    //   进来什么状态, 走的时候就还回什么状态 —— 两处成对, 中间不早退。
    // ★冻结帧★ 抄自 OptiScaler 的 DlssNrHoldFrame: 不再把新画面拷进来, 也不重新编码,
    //   于是喂给模型的图和满分辨率原画都停在按下那一刻 —— 之后改任何设置,
    //   画面上变的【只有设置本身】。这是唯一能公平比画质的办法。
    const bool held = holdRequested && s_hold_active;
    if(holdRequested && !s_hold_active) {
        // Capture all guides and the effective same-frame white along with THIS
        // frame's colour. Each frozen evaluation resets NR history for a stable
        // single-frame comparison instead of replaying motion against live input.
        auto clone=[&](ID3D12Resource* src,ID3D12Resource** dst){
            if(!src)return true;
            if(!nrsnapshot::Clone(dev,cl,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,dst))return false;
            return resolveleases::Hold(frameLease,*dst);
        };
        if(!clone(depth,&s_hold_depth)||!clone(mv,&s_hold_motion)||!clone(frame_white,&s_hold_white)){
            Park(s_hold_depth);Park(s_hold_motion);Park(s_hold_white);++s_generation;gputime::Abort();s_note="完整冻结输入建立失败";return 0;
        }
        s_hold_key=historyKey;s_hold_active=true;
        depth=s_hold_depth;mv=s_hold_motion;frame_white=s_hold_white;
        Log("[033 snapshot] colour/depth/motion/white and guide contract captured together");
    }
    const D3D12_RESOURCE_STATES arrive = nrinput033::context.presentation
        ? D3D12_RESOURCE_STATE_RENDER_TARGET : carrier::OutputArrivalState();
    if (!held)
    {
    scale::Barrier(cl, color_inout, arrive, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION copySrc = {}, copyDst = {};
    copySrc.pResource = color_inout; copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copyDst.pResource = s_full; copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const UINT ox = s_frame_output.x, oy = s_frame_output.y;
    const D3D12_BOX box = {ox, oy, 0, ox + w, oy + h, 1};
    cl->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, &box);
    scale::Barrier(cl, color_inout, D3D12_RESOURCE_STATE_COPY_SOURCE, arrive);
    }
    scale::Barrier(cl, s_full, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpufault033::NR(cl,s_frames+1,w,h,2);
    gputime::Stamp(cl);   // ① 拷入

    // 缩小 + 编码
    //   这一趟同时负责把画面变成模型认得的样子: 白点归一 + 高光滚降 + sRGB。
    //   100% 档时着色器走 Load 直通, 只编码, 不引入重采样。
    const int enc = carrier::EncodeMode(s_fmt,nrinput033::context.presentation);
    // ★把颜色路记一次★ 排查画质必看: 走没走编码、白点多少。
    //   白点定错的后果(参考实现原话): 画面根本没被归一, 高光那一支算出几百倍的比值,
    //   ToOkLab 拿到远超设计范围的值 —— 表现是发绿 / 大片发灰发花。
    {
        static DXGI_FORMAT reported_fmt = DXGI_FORMAT_UNKNOWN;
        if (reported_fmt != s_fmt)
        {
            reported_fmt = s_fmt;
            Log("[hostnr] 颜色路: 格式 %d · %s · 白点设定 %.2f · 游戏曝光纹理 %s",
                static_cast<int>(s_fmt),
                enc ? "编码路(HDR/浮点, 要白点)" : "直通(SDR/UNORM, 不用白点)",
                carrier::cfg.whitepoint,
                (frame_white != nullptr) ? "★拿到了, 白点按它算★" : "没拿到, 用上面那个设定值");
        }
    }
    // Re-encode the complete frozen input with the current artistic settings.
        scale::Dispatch(frameBlitter, dev, cl, s_full, s_fmt, s_small, s_model_fmt, s_sw, s_sh, 0,
                        carrier::EffectiveWhite(), enc, frame_white,
                        carrier::EffectiveCurve(), carrier::cfg.diffuse_white, &grade);
    scale::Barrier(cl, s_small, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    yanyundual::Capture(dev,cl,s_small,frameLease,personFrame);
    if(yanyundual::RecognitionRequested(yanyundual::enabled,yanyundual::previewMask.load())){
        // S20/S22: this frame's motion at the mask grid; every latched mask is
        // later followed back along the motion recorded for each frame since its
        // capture. Same typed view, full-rectangle and mvScale/guide conversion
        // as the input stabilizer.
        yanyundual::MaskMotion maskMotion;
        if(mv){
            const auto md=mv->GetDesc();const DXGI_FORMAT mvfmt=MvSrvFormat(md.Format);
            const bool fullRect=!s_have_frame_guides||(!s_frame_guides.motion.x&&!s_frame_guides.motion.y&&
                s_frame_guides.motion.width==md.Width&&s_frame_guides.motion.height==md.Height);
            const float kx=now_gw?mvScaleX/float(now_gw):0.f,ky=now_gh?mvScaleY/float(now_gh):0.f;
            if(fullRect&&mvfmt!=DXGI_FORMAT_UNKNOWN&&std::isfinite(kx)&&std::isfinite(ky)&&(kx!=0.f||ky!=0.f))
                maskMotion={mv,mvfmt,kx,ky};
        }
        // S22: this frame's depth too, so a mask followed back along the recorded
        // motion can tell the person from the background it just uncovered.
        if(maskMotion.texture&&depth){
            const auto dd=depth->GetDesc();const DXGI_FORMAT dfmt=yanyundual::late::DepthSrvFormat(dd.Format);
            const bool depthFull=!s_have_frame_guides||(!s_frame_guides.depth.x&&!s_frame_guides.depth.y&&
                s_frame_guides.depth.width==dd.Width&&s_frame_guides.depth.height==dd.Height);
            if(depthFull&&dfmt!=DXGI_FORMAT_UNKNOWN&&dd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&dd.SampleDesc.Count==1){
                maskMotion.depth=depth;maskMotion.depthFormat=dfmt;maskMotion.depthInverted=depthInverted!=0;}
        }
        yanyundual::late::RecordMotion(dev,cl,frameLease,maskMotion,personFrame);
    }
    gputime::Stamp(cl);   // ② 编码

    // ★模型输入的时域稳定★ 治「怎么调都闪」的细节沸腾。
    //   稳的是喂进模型的那张图, 不是模型的答案 —— 后者上游实测两次都失败。
    //   我们是比值合成, 输入被时域滤糊只影响那个倍率(增强层), 原画锐度不受影响,
    //   这正是同一条路在他们那儿被否掉、在我们这儿仍值得试的原因。
    ID3D12Resource *model_in = s_small;
    if (carrier::cfg.replica == 0 && carrier::cfg.stabilize > 0 && s_hist[0] != nullptr && s_hist[1] != nullptr && mv != nullptr)
    {
        const int wi = s_hist_i;          // 这一帧写哪张
        const int ri = 1 - s_hist_i;      // 读哪张(上一帧的结果)
        // 强度 100 -> 当前帧只占 20%, 强度 1 -> 几乎不动。头几帧没历史, 直接透传。
        float alpha = 1.0f - 0.8f * (static_cast<float>(carrier::cfg.stabilize) / 100.0f);
        if (s_hist_age < 2) alpha = 1.0f;
        const float kx = (now_gw > 0) ? (mvScaleX / static_cast<float>(now_gw)) : 0.0f;
        const float ky = (now_gh > 0) ? (mvScaleY / static_cast<float>(now_gh)) : 0.0f;

        // ★MV 的格式必须是 typed★ —— typeless 上建 SRV 会失败, 而失败的描述符被
        //   绑上去就是掉设备。认不出来就整趟不做, 宁可不稳定也不冒这个险。
        const DXGI_FORMAT mvraw = mv->GetDesc().Format;
        const DXGI_FORMAT mvfmt = MvSrvFormat(mvraw);
        const auto md = mv->GetDesc();
        const bool rect_full = !s_have_frame_guides || (!s_frame_guides.motion.x && !s_frame_guides.motion.y &&
            s_frame_guides.motion.width == md.Width && s_frame_guides.motion.height == md.Height);
        const bool mv_ok = rect_full && (mvfmt != DXGI_FORMAT_UNKNOWN) && (kx != 0.0f || ky != 0.0f);
        if (!mv_ok)
        {
            static bool logged_mv = false;
            if (!logged_mv)
            {
                logged_mv = true;
                Log("[hostnr] 时域稳定跳过: 运动矢量格式 %d 认不出 (换算 %.6f/%.6f)", static_cast<int>(mvraw), kx, ky);
            }
        }
        else
        {
        scale::Barrier(cl, s_hist[wi], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        scale::DispatchStabilize(frameBlitter, dev, cl, s_hist[ri], s_small, s_model_fmt,
                                 mv, mvfmt, s_hist[wi], s_sw, s_sh, alpha, kx, ky);
        scale::UavBarrier(cl, s_hist[wi]);
        scale::Barrier(cl, s_hist[wi], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        model_in = s_hist[wi];
        s_hist_i = ri;
        if (s_hist_age < 1000) ++s_hist_age;
        if (s_frames < 2) Log("[hostnr] 时域稳定: 强度 %d (当前帧权重 %.2f), mv->uv %.6f/%.6f",
                              carrier::cfg.stabilize, alpha, kx, ky);
        }
    }

    // ── 人脸包围盒: 已写好但【暂未接线】 ──────────────────────────
    //   facebox.h 里那一趟在审判之眼上第一帧就把设备干掉了
    //   (ReShade: Failed to create backup depth-stencil texture ->
    //    DXGI_ERROR_DEVICE_REMOVED / INVALID_CALL)。
    //   就地插入这条路用的是【游戏的】命令列表, 在上面临时绑我们自己的
    //   描述符堆/根签名/UAV, 紧接着又要把这条列表交给 NGX 求值 —— 冲突点
    //   还没查清。要接回来的话, 先在【交换链那条自有命令列表】上验通,
    //   那里资源和状态都是我们自己的。


    // Input skin brightening was removed; no detector/readback/enhance dispatch.
    ID3D12Resource* resolve_in=model_in;

    // NR (小 → 小)。引导图是帮手从游戏那边开过来的, 满尺寸。
    //
    // ★运动矢量的缩放必须跟着模型尺寸走★
    //   MVecScale 的含义是「把运动矢量纹理里的数值换算成【渲染分辨率下的像素】」。
    //   宿主给的那对值是按【满尺寸】算的, 而我们的模型跑在 s_sw x s_sh(默认 50%)。
    //   照抄的话模型拿到的运动量整整大一倍, 历史帧被拉过头 —— 跑动时人物边缘、
    //   衣摆糊成一团。(黑旗实测反馈: "更新后拖影很厉害, 不如上个版本")
    //   D3D12 那条路(carrier)本来就有防拖影的处理, 这条路一直漏了。
    // 引导图尺寸: 就地插入路由 inject 传进来(渲染分辨率), 老游戏路跟画面一致
    const unsigned gw = (s_gw_override != 0) ? s_gw_override : w;
    const unsigned gh = (s_gh_override != 0) ? s_gh_override : h;
    // ★★ 运动矢量缩放【原样透传】—— 不许再乘任何尺寸比例 ★★
    //   这条已经栽过两次, 结论写死在这里, 别再改回去:
    //
    //   2026-09-04 第二次: 照参考实现 DlssNr_Dx12.cpp:2152 的
    //   mvToWork = workWidth / width 乘了 0.75, 燕云十六声当场开始闪。
    //   为什么那个公式在我们这条路上不成立 —— 那局是 DLAA,
    //   【引导图 5120x2160 和画面 5120x2160 一样大】, 所以
    //   「模型宽÷画面宽」和「模型宽÷引导宽」算出来都是 0.75, 两种候选公式
    //   给的是同一个值, 而结果是闪的 => 两种都错。
    //   真正的原因: 每个资源本身带着 subrect 说明自己多大(我们确实设了
    //   ColorSubrect / DepthSubrect / MVecSubrect), 模型是照 subrect 自己换算的,
    //   我们在这之上再乘一遍就是【算了两遍】 —— 矢量变短、历史帧跟不上, 就是闪。
    //
    //   2026-09-04 第一次: 乘了 s_sw/gw, 审判之眼那次是 1.724 倍(矢量太长),
    //   那也正是把整台机器冻死那一版的配置。
    //
    //   一句话: 尺寸的事交给 subrect, 这里只把游戏自己的编码原样递过去。
    const float mvKx = 1.0f;
    const float mvKy = 1.0f;

    if (s_frames < 2) Log("[hostnr] 画面 %ux%u · 引导图 %ux%u · 模型 %ux%u · MV 缩放 x%.3f",
                          w, h, gw, gh, s_sw, s_sh, mvKx);
    DWORD seh = 0;
    LARGE_INTEGER cpuBegin, cpuEnd, cpuFrequency;
    QueryPerformanceFrequency(&cpuFrequency);
    QueryPerformanceCounter(&cpuBegin);
    gpufault033::NR(cl,s_frames+1,w,h,3);
    unsigned evaluatedPass=0;
    int r = nrfwd::evaluate(cl, s_feat, model_in, depth, mv, s_out, s_sw, s_sh, gw, gh,
                                  depthInverted, resetHistory ? 1 : 0,
                                  s_model_cfg.intensity, s_model_cfg.style,
                                  s_model_cfg.local_structure, s_model_cfg.local_tone,
                                  s_model_cfg.skin_structure, s_model_cfg.auto_mask,
                                  mvScaleX * mvKx, mvScaleY * mvKy, &seh,
                                  s_have_frame_guides ? &s_frame_guides : nullptr, s_model_cfg.global_tone);
    if(nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return 0;}
    ID3D12Resource* model_result=s_out;
    for(int pass=1;pass<s_selected_passes && !seh && r==NVSDK_NGX_Result_Success;++pass){
        scale::Barrier(cl,model_result,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const UINT passW=pass==1?s_ew:s_tw,passH=pass==1?s_eh:s_th;
        auto* passInput=pass==1?s_pass_input:s_pass_input3;
        const bool scaledRefinement=passW!=s_sw || passH!=s_sh;
        const unsigned base=nrstack::PassBase(pass);
        // Prepare the NEXT model's input. The unmodified first input anchors
        // chroma and local contrast so repeated passes do not amplify fringes.
        // No post-NR face replacement, motion fade, temporal filter or readback.
        scale::DispatchResolve(frameBlitter,dev,cl,s_small,s_small,model_result,s_model_fmt,scaledRefinement?s_refined[0]:passInput,
            scaledRefinement?s_sw:passW,scaledRefinement?s_sh:passH,
            1.f,1.f,3.f,1,1.f,0.f,nullptr,4,0,0,1,0.f,203.f,nullptr,nullptr,
            nrskin::Protection(s_selected_passes),0.f,base);   // 层间精修不提亮, 只在最终合成提一次
        if(scaledRefinement){
            scale::Barrier(cl,s_refined[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale::Dispatch(frameBlitter,dev,cl,s_refined[0],s_model_fmt,passInput,s_model_fmt,passW,passH,(base+4)/2);
        }
        scale::Barrier(cl,passInput,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        const bool resetPass=resetHistory || s_pass_frame[pass]+1!=s_frames+1;
        auto* next=(pass%2)?s_extra_out:s_extra_alt;
        const auto tune=nrlayers::Get(s_model_cfg,pass);
        evaluatedPass=unsigned(pass);
        r=nrfwd::evaluate(cl,s_extra_feat[pass-1],passInput,depth,mv,next,passW,passH,gw,gh,
            depthInverted,resetPass?1:0,tune.intensity,tune.style,tune.structure,
            tune.tone,tune.skin,tune.autoMask,mvScaleX,mvScaleY,&seh,
            s_have_frame_guides?&s_frame_guides:nullptr,tune.globalTone);
        if(nrfault033::Blocked()){s_failed=true;s_note=nrfault033::Note();return 0;}
        scale::Barrier(cl,passInput,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        scale::Barrier(cl,model_result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(scaledRefinement && !seh && r==NVSDK_NGX_Result_Success){
            scale::Barrier(cl,passInput,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale::Barrier(cl,next,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            // Lift the complete RGB delta onto its own conditioned full-size
            // prior. Subtracting the low input removes resampling bias; retaining
            // full-size detail prevents a later low-res pass from overwriting it.
            scale::DispatchResolve(frameBlitter,dev,cl,s_refined[0],passInput,next,s_model_fmt,s_refined[1],s_sw,s_sh,
                1.f,1.f,3.f,1,1.f,0.f,nullptr,3,0,0,1,0.f,203.f,nullptr,nullptr,
                0.f,0.f,base+6);   // 保护 0、提亮 0、槽位 base+6。V6.1 曾把 base+6 错传成提亮强度 → 第二层整屏黑
            scale::Barrier(cl,next,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Barrier(cl,passInput,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            model_result=s_refined[1];
        }else model_result=next;
        if(scaledRefinement)scale::Barrier(cl,s_refined[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s_pass_frame[pass]=s_frames+1;
    }
    QueryPerformanceCounter(&cpuEnd);
    if (seh != 0 || r != NVSDK_NGX_Result_Success)
    {
        invalidate_history();
        gputime::Abort();   // 这一帧不算, 也别留半截打点
        // 失败就原样放行 —— 帮手那一帧照常出去, 只是没有我们的效果
        scale::Barrier(cl, s_small, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        scale::Barrier(cl, s_full, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (seh != 0) { s_failed = true; s_note = "求值抛异常"; }
        else {
            if(s_evaluate_recovery.Failed(evaluatedPass,GetTickCount64())){
                s_auto_recovery.fetch_or(4);
                Log("[hostnr recovery] repeated evaluate failure; layer=%u sdk=0x%08X mask=0x%X; requesting fresh candidate with unchanged applied settings",evaluatedPass+1,unsigned(r),s_evaluate_recovery.mask);
            }
            if (++s_frames < 4) Log("[hostnr] 求值 0x%08X", static_cast<unsigned>(r));
        }
        return 0;
    }

    s_history.commit(historyKey);
    // Publish actual applied model/grade only after successful recording. A
    // pending UI request is not yet a new image and must not keep resetting FG.
    uint64_t imageKey=s_built_tune;
    for(uint64_t value:{uint64_t(s_sw),uint64_t(s_sh),uint64_t(s_ew),uint64_t(s_eh),uint64_t(s_tw),uint64_t(s_th),
                       uint64_t(s_selected_passes),uint64_t(historyKey.grade)})
        imageKey=(imageKey^value)*1099511628211ull;
    fgscene033::Model(imageKey);
    // 合成: 满尺寸原画不动, 按【原画亮度该在的位置】把模型那张图缩放回来
    //   (不是加法差值 —— 那会丢掉模型在高光里的行为, 也让各档位看起来一个样)
    gpufault033::NR(cl,s_frames+1,w,h,4);
    gputime::Stamp(cl);   // ③ 模型
    // Apply the local colour/added-crease bound after every selected layer count. Keep
    // broad model colour and lighting; do not restore the first layer's face.
    // Both possible scratch textures already belong to frameRefs. Fresh slots
    // 54..57 cannot overwrite portrait/earlier-pass descriptors on this list.
    model_result=nrstack::Finish(s_selected_passes,model_result,finalScratch,
        [&](ID3D12Resource* src){scale::Barrier(cl,src,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);},
        [&](ID3D12Resource* src,ID3D12Resource* dst){
            scale::DispatchResolve(frameBlitter,dev,cl,s_small,s_small,src,s_model_fmt,dst,s_sw,s_sh,
                1.f,1.f,3.f,1,1.f,0.f,nullptr,4,0,0,1,0.f,203.f,nullptr,nullptr,
                nrskin::Protection(s_selected_passes),0.f,nrstack::FinalBase);   // 同上
        },
        [&](ID3D12Resource* src){scale::Barrier(cl,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
    scale::UavBarrier(cl, model_result);
    scale::Barrier(cl, model_result, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // ★这里【不要】图省事直接写进游戏的 Output★
    //   试过: resolve 的目标直接给 color_inout, 想省掉下面那趟全分辨率拷贝。
    //   结果古墓丽影暗影当场弹 0x887A0001 DXGI_ERROR_INVALID_CALL 退出 ——
    //   游戏那张 Output 的格式我们并不知道(可能是 typeless / _SRGB), 拿我们
    //   自己的 s_fmt 去给它建 UAV 就是非法调用。
    //   省下的那点带宽(~0.1ms)换不来这个风险, 老老实实中转。
    scale::Blitter resolve=frameBlitter;
    auto regionalMotion=yanyundual::MotionSettings(carrier::cfg,mv,s_have_frame_guides?&s_frame_guides:nullptr,mvScaleX,mvScaleY);
    scale::DispatchResolve(resolve, dev, cl, s_full, resolve_in, model_result, s_fmt, s_res, w, h,
                           carrier::cfg.blend/100.f, carrier::EffectiveWhite(),
                           carrier::cfg.guard, carrier::ResolveMode(s_fmt,nrinput033::context.presentation), carrier::cfg.colour,
                           0.0f,
                           frame_white,
                           yanyundual::enabled?(carrier::cfg.replica?1:carrier::cfg.compose):1, // Dedicated branch honours its own composition.
                           carrier::cfg.resample,
                           carrier::EffectiveCurve(),     // Match the input colour encoding
                           carrier::cfg.applymodel,
                           (carrier::cfg.comparepct > 0 && carrier::cfg.comparepct < 100)
                               ? carrier::cfg.comparepct / 100.0f : 0.0f, carrier::cfg.diffuse_white, &regionalMotion, &grade,
                           // 皮肤保护按层数给(业主「单层也给」: 单层时 Finish 直接返回、层间两处也不跑,
                           // 只有这里能给); 肤色提亮取面板值, 整条链只在这里做一次; 槽位 4。
                           // V6.1 这里原是「0,4」: 4 被当成提亮强度(夹到最大, 滑块无效), 保护一直是 0。
                           nrskin::Protection(s_selected_passes), carrier::cfg.skin_lift, UINT(4));

    scale::UavBarrier(cl, s_res);
    auto* finalOutput=nrclarity::Finish(s_res,clarityTarget,
        [&](ID3D12Resource* src){scale::Barrier(cl,src,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);},
        [&](ID3D12Resource* src,ID3D12Resource* dst){
            scale::DispatchResolve(resolve,dev,cl,src,src,src,s_fmt,dst,w,h,naturalApplied,1.0f,carrier::cfg.guard,
                carrier::ResolveMode(s_fmt,nrinput033::context.presentation),1.0f,clarityApplied,nullptr,5,0,0,1,
                (carrier::cfg.comparepct>0 && carrier::cfg.comparepct<100)?carrier::cfg.comparepct/100.0f:0.0f,
                carrier::cfg.diffuse_white,nullptr,nullptr,0.0f,0.0f,UINT(nrstack::ClarityBase));
            scale::UavBarrier(cl,dst);
        },[&](ID3D12Resource* src){scale::Barrier(cl,src,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
    if(yanyundual::RecognitionRequested(yanyundual::enabled,yanyundual::previewMask.load())){
        // S20-S22: the masks are latched, aligned along the recorded motion, voted and feathered on the GPU inside Composite.
        auto* mixed=yanyundual::Composite(dev,cl,s_full,finalOutput,depth,mv,frame_white,
            s_have_frame_guides?&s_frame_guides:nullptr,depthInverted,personReset,mvScaleX,mvScaleY,enc,
            carrier::ResolveMode(s_fmt,nrinput033::context.presentation),personFrame);
        if(!mixed){
            // S30: Composite returns the scene look itself when there is nothing
            // to compose; nullptr now means a failed composite only (build
            // poisoned, person pass failed): the game image stays intact.
            // All recorded branch resources remain leased; no fake success.
            scale::Barrier(cl,model_result,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Barrier(cl,s_small,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Barrier(cl,s_full,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            gputime::Abort();++s_frames;return 2; // recorded models, intentionally no regional publication
        }
        finalOutput=mixed;
    }
    if(matchedcapture::status.load()==matchedcapture::Requested){
        char meta[2048];std::snprintf(meta,sizeof(meta),
            "{\"schema\":1,\"frame\":%llu,\"output\":[%u,%u],\"model\":[%u,%u],\"depth\":[%u,%u,%u,%u],\"motion\":[%u,%u,%u,%u],\"mvScale\":[%.7g,%.7g],\"pregrade\":{\"enabled\":%u,\"exposureEV\":%.7g,\"contrast\":%.7g,\"saturation\":%.7g,\"warmth\":%.7g,\"tint\":%.7g,\"highlights\":%.7g,\"style\":%u,\"styleStrength\":%.7g},\"presetRequested\":%d,\"presetBuilt\":%d,\"presetReadback\":%d,\"builtTuneSignature\":%u,\"skinStructure\":%.7g,\"skinProtection\":%.7g,\"hold\":%d,\"naturalRequested\":%.7g,\"naturalApplied\":%.7g,\"clarityRequested\":%.7g,\"clarityApplied\":%.7g,\"source\":\"%s; content not verified\"}",
            s_frames,w,h,s_sw,s_sh,historyKey.depth.x,historyKey.depth.y,historyKey.depth.width,historyKey.depth.height,
            historyKey.motion.x,historyKey.motion.y,historyKey.motion.width,historyKey.motion.height,mvScaleX,mvScaleY,
            grade.enabled,grade.exposure,grade.contrast,grade.saturation,grade.warmth,grade.tint,grade.highlights,grade.style,grade.styleStrength,
            carrier::cfg.preset,s_model_cfg.preset,nrfwd::preset_back(),s_built_tune,s_model_cfg.skin_structure,nrskin::Protection(s_selected_passes),holdRequested?1:0,naturalRequested,naturalApplied,clarityRequested,clarityApplied,
            nrinput033::context.presentation?(nrinput033::context.nativeGuides?"presentation native-guide contract":"presentation derived guides"):"NGX contract");
        matchedcapture::Record(dev,cl,s_full,finalOutput,frameLease,meta);
    }
    gputime::Stamp(cl);   // ④ 合成

    scale::Barrier(cl, finalOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    scale::Barrier(cl, color_inout, arrive, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION copySrc = {}, copyDst = {};
    copySrc.pResource = finalOutput; copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copyDst.pResource = color_inout; copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cl->CopyTextureRegion(&copyDst, s_frame_output.x, s_frame_output.y, 0, &copySrc, nullptr);
    scale::Barrier(cl, color_inout, D3D12_RESOURCE_STATE_COPY_DEST, arrive);
    scale::Barrier(cl, finalOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    gpufault033::NR(cl,s_frames+1,w,h,5);
    gputime::End(cl);     // ⑤ 拷回 (最后一个点在 End 里)
    // 每 120 帧把 GPU 分段写进日志 —— 面板看不到的场合(远程排障/自动测试)也能拿到硬数据
    if (gputime::ready() && (s_frames % 120) == 0) {
        static UINT64 reported_samples=0;
        const auto timing=gputime::snapshot();
        if (timing.fresh() && timing.samples != reported_samples && timing.total > 0.0) {
            Log("[hostnr] GPU 侧: 拷入 %.2f · 编码 %.2f · 模型 %.2f · 合成 %.2f · 拷回 %.2f = %.2f ms (模型 %ux%u%s; sample=%llu completion_age_ms=%llu)",
                timing.segments[0], timing.segments[1], timing.segments[2], timing.segments[3], timing.segments[4],
                timing.total, s_sw, s_sh, carrier::cfg.modelfull ? ", 基准=整帧" : ", 基准=渲染分辨率", timing.samples, timing.age_ms);
            reported_samples=timing.samples;
        } else Log("[hostnr] GPU timing unavailable: no fresh completed sample (sample=%llu completion_age_ms=%llu)", timing.samples, timing.age_ms);
    }

    // 恢复常驻态
    scale::Barrier(cl, model_result, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    scale::Barrier(cl, s_small, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    scale::Barrier(cl, s_full, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);


    s_evaluate_recovery.Success();
    if(!s_evaluate_recovery.Pending(carrier::cfg.passes)){
        if(s_auto_recovery.fetch_and(~4u)&4u)Log("[hostnr recovery] NR recording succeeded after recovery; passes=%d; displayed result requires game verification",s_selected_passes);
    }
    const UINT64 n = ++s_frames;
    if (n <= 2 || n % 120 == 0) {
        const double cpuMs = cpuFrequency.QuadPart > 0 ? 1000.0 * double(cpuEnd.QuadPart - cpuBegin.QuadPart) / double(cpuFrequency.QuadPart) : 0.0;
        Log("[nr-diagnostics] frame=%llu CPU_NR_call=%.3fms reset=%d reset_reason=%u GPU_completed_samples=%llu GPU_skipped_samples=%llu "
            "native_submit_hook=%d native_submits=%llu ignored_nonimage=%u concurrent_NR_skips=%u concurrent_NR_waits=%u waits_frame=%u skip_reentrant=%u skip_frame=%u skip_build=%u",
            n, cpuMs, resetHistory ? 1 : 0, resetReason, gputime::samples(), gputime::dropped(),
            gputime::submit::installed.load() ? 1 : 0, gputime::submit::calls.load(),
            nrdispatch::non_image_calls.load(), nrdispatch::skipped.load(), nrdispatch::waited.load(),
            nrdispatch::waited_frame.load(), nrdispatch::skipped_reentrant.load(),
            nrdispatch::skipped_frame.load(), nrdispatch::skipped_build.load());
    }
    if (n <= 5 || (n % 3600) == 0)
        Log("[hostnr] 帧 %llu (%ux%u → 模型 %ux%u)", n, w, h, s_sw, s_sh);
    s_note = "运行中";
    return 1;
}

// 在帮手进程里就注册; 不在的话什么都不做
static void try_attach()
{
    // ★失败要能重试★
    // 以前是进来就 s_hooked = true, 只在 DllMain 里调一次 ——
    // 而那个时候 dlss5-feed.addon64 可能还没被 ReShade 加载(插件加载
    // 顺序不归我们管)。抓空一次就永久放弃, D3D11 游戏就死在这里:
    // 面板永远「启动中」+「非 D3D12」(上线后两个用户实报)。
    // 32 位那条路没事, 是因为导出在帮手进程的 exe 上, 进程一起来就在。
    if (s_hooked) return;
    if (++s_attempts > 600) { return; }   // 试够 10 秒就算了, 不无限查下去
    // 两个宿主, 一套接法:
    //   · 32 位游戏 → 64 位帮手进程 dlss5-feed-host64.exe(导出在 exe 上)
    //   · 64 位 D3D11/Vulkan 游戏 → 同进程的 dlss5-feed.addon64(导出在 DLL 上)
    //     (那种游戏进程里没有 D3D12 设备, Feeder 自己开了一个私有的, 我们用它)
    PFN_SetNR set = reinterpret_cast<PFN_SetNR>(
        GetProcAddress(GetModuleHandleW(nullptr), "dlss5_feed_033_set_nr"));
    const char *from = "帮手进程";
    if (set == nullptr)
    {
        HMODULE m = GetModuleHandleW(L"dlss5-feed.addon64");
        if (m != nullptr)
        {
            set = reinterpret_cast<PFN_SetNR>(GetProcAddress(m, "dlss5_feed_033_set_nr"));
            from = "Feeder 插件";
        }
    }
    if (set == nullptr)
    {
        s_note = (s_attempts > 600) ? "不在 Feeder 宿主里" : "等 Feeder 插件加载…";
        return;                      // 不置 s_hooked, 下一帧接着试
    }
    s_hooked = true;
    set(&Stage);
    s_note = "已接入";
    Log("[hostnr] 已接入 %s, 神经渲染由我们做", from);
}

// ★★★ 这里曾经有一个 wake_for_takeover() —— 已删, 不许再写回来 ★★★
//   它的用途是: 面板上点一下, 让我们【当场】把 feature 18 从别的引擎手里接过来。
//   2026-09-04 23:56 燕云实测(ReShade.log 逐字):
//       23:56:07.839  用户点了接管
//       23:56:08.320  [求值] renodx 设进来的参数 5120x2160   ← 它还在跑
//       23:56:08.501  [hostnr] ★就绪★ 模型 1280x540         ← 我们建了第二个
//       23:56:08.503  Device was lost DXGI_ERROR_DEVICE_REMOVED (INVALID_CALL)
//   建完两毫秒设备就没了。审判之眼 23:35/23:37 两次一模一样(见 carrier.h:768)。
//   
//   ★结论: 中途把 feature 18 接过来是物理上做不到的★
//   往外让(yield_to_carrier / 关自己)永远安全; 往里接永远不安全。这不对称,
//   但它是真的。想换到我们这边, 只能写配置 + 重进游戏(carrier::write_engine_choice)。
// ★只许 handover 状态机调★ 把上一轮的放弃状态清掉, 重新开始尝试建 feature。
//   跟当年那个「现在接管」按钮的区别就一条: 它是【裸的】, 点了立刻建;
//   这个只在状态机走完「对面已让开 + 300 帧 + 静置 90 帧 + 账本清零」之后才到得了。
//   ★永远不要把它直接接到按钮上★ 那正是 2026-09-04 23:56 掉显卡的写法。
static void wake_after_handover()
{
    s_failed = false;
    s_note.clear();
    s_busy_tries = 0;
    s_retry_cool = 0;
    s_want_build = true;
    s_yielded    = false;
    carrier::g_inject_dead = false;
    Log("[hostnr] 交接完成, 重新开始尝试建 feature 18");
}
static bool active()   { return s_frames > 0 && !s_failed && !nrfault033::Blocked(); }
// 面板上手动选「交换链」时调: 立刻交出 feature 18, 把活儿让给交换链。
static void yield_to_carrier(const char *why)
{
    if (s_yielded) return;
    s_yielded = true;
    Log("[hostnr] 让位给交换链 (%s)", why ? why : "手动");
    Release();
    ReleaseParkedFeaturesNow();
    nrfwd::drop_external_caps();
    carrier::g_yield_grace = 120;
    carrier::g_inject_dead = true;
}
static bool yielded()  { return s_yielded; }
static bool attached() { return s_hooked && s_note != "不在 Feeder 宿主里"; }
// 给 nrscale 按句柄认「这是我们自己的 feature」用 —— 别把自己的调用当成游戏的
static const void *feature() { return s_feat; }
// 面板的环形图要拿模型尺寸跟画面尺寸比面积 —— 开销就是按这个走的
static UINT model_w()  { return s_sw; }
static UINT model_h()  { return s_sh; }
static UINT full_w()   { return s_w; }
static UINT full_h()   { return s_h; }
static UINT small_w()  { return s_sw; }
static UINT small_h()  { return s_sh; }
static unsigned long long frames() { return s_frames; }
// 引导图是帮手从 32 位游戏那边开过来的共享纹理(DLSS5_Feed.fx + LumeniteFX 算的),
// 直接由回调递进来 —— 跟桌面路线那套 carrier::gd 不是一回事, 别读错。
static const char *guides()
{
    if(nrinput033::ownership.Get()==nrinput033::Route::Presentation)return nrgame033::Note();
    if (s_has_depth && s_has_mv) return "已接收深度和运动纹理 · 原生来源待核验";
    if (s_has_depth)             return "只有深度";
    if (s_has_mv)                return "只有运动矢量";
    return "还没送到";
}
static bool guides_ok() { return s_has_depth && s_has_mv; }
static const char *note() { return nrfault033::Blocked()?nrfault033::Note():(s_auto_recovery.load()?"NR 正在自动恢复；已应用参数保留":s_note.c_str()); }
static const char *exposure_note() { return exposure::note(s_exposure); }

} // namespace hostnr
