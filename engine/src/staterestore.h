// =====================================================================
//  staterestore.h ——  就地插入路的「状态信封」: 借了游戏的计算态, 走的时候要还
//
//  ★问题★
//    hostnr::Stage 在【游戏的】ID3D12GraphicsCommandList 上跑我们那几趟 compute
//    (scale::Dispatch / DispatchResolve 里 SetDescriptorHeaps / SetComputeRootSignature /
//     SetPipelineState), 跑完什么都不还。普通引擎每次 draw/dispatch 前都重绑, 无所谓;
//    bindless 引擎(RE Engine: RE4 / RE9 / 龙之信条2, 怪猎荒野)一条列表只绑一次,
//    我们插完它接着用 —— 用的却是我们的堆 / 根签名 / PSO → 设备移除。
//    OptiScaler 同一个坑是用 RAII 信封修的(出作用域把游戏的计算根签名 + 堆绑回去),
//    他们的捕获靠 Detours 钩子。我们不许再钩(ReShade 已经钩着命令列表了),
//    所以改用 ReShade 的插件事件来捕获:
//      bind_pipeline           → 游戏最后 SetPipelineState 的 PSO(SetPipelineState1 的状态对象也认)
//      bind_descriptor_tables  → 计算根签名(SetComputeRootSignature 本身也走这个事件, count=0)
//                                + 用 get_descriptor_heap_offset 把每张表还原成它所在的堆
//      reset_command_list / destroy_command_list → 清记录, 绝不还一个陈旧指针
//    记录按【原生】ID3D12GraphicsCommandList* 键(cmd_list->get_native())。
//
//  ★指针不是同一个指针★
//    游戏手里拿的是 ReShade 的代理列表, 而事件里 get_native() 报的是代理背后的原生指针;
//    hostnr::Stage 收到的 cl 就是游戏那个(代理)。两个数值不相等, 直接拿 cl 查表永远查空。
//    解法: 我们自己的绑定也会触发同样的事件(同一线程、同一条列表)。信封打开期间,
//    事件处理器不记录, 只顺手记下「这条列表的原生指针是多少」; 析构时就用它查表。
//    万一 cl 本来就是原生的(没学到), 退回用 cl 本身查。
//
//  ★线程★
//    ReShade 对某条命令列表的事件在【录制那条列表的线程】上触发; 游戏可能多线程录多条,
//    所以表用一把 std::mutex 护着(临界区只有几十纳秒, 里面绝不调 D3D12 —— D3D12 调用会
//    再触发事件, 再进锁就死锁)。「我们正在绑」这个标记是 thread_local: 只挡本线程
//    (就是跑 Stage 的那条), 别的线程照常记录。
//
//  ★档位★ cfg_enabled(cfg 键 restorestate):
//    0 = 关(载入时就不挂事件, 零开销)
//    1 = 还 堆 + 计算根签名 + PSO 【默认】—— 就是 OptiScaler 那个信封做的事
//    2 = 在 1 之上把【计算根参数】也放回去: 描述符表 / 根 CBV·SRV·UAV / 32 位常量。
//        SetComputeRootSignature 一换, 所有根参数按 D3D12 语义都作废了; 真正一条列表
//        只绑一次的引擎, 光还根签名不够, 它下一次 Dispatch 用的表 / 常量已经没了。
//        这一档多挂 4 个事件(push_descriptors / push_constants / init·destroy_pipeline_layout),
//        改档要重启游戏。先在 RE Engine 上验过再考虑改默认。
//        能还多少是「尽力」: ReShade 认不出的地址(没登记的资源)它根本不报事件, 那一项
//        就还不回去; 认不出 SRV 还是 UAV 的根签名, 整条列表放弃参数重放(堆/根签名/PSO 照还)。
//
//  依赖: <d3d12.h> <reshade.hpp> 已由包含者引入。日志走 STATERESTORE_LOG(可选):
//    #define STATERESTORE_LOG Log   放在 #include "staterestore.h" 之前即可。
// =====================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#ifdef K033_BETA2_RESHADE_HOST
#include "d3d12_identity.h"
#endif

#ifndef STATERESTORE_LOG
#define STATERESTORE_LOG(...) ((void)0)
#endif

namespace staterestore
{

// 档位, 见文件头。carrier::load_cfg 里按键 restorestate 赋值。
// ★默认关 (2026-09-04 11:31 二分定案)★
//   古墓丽影暗影(DX12 + 光追)开着信封 28 秒就 DXGI_ERROR_DEVICE_REMOVED (游戏日志:
//   Map failed 0x887a0005), 关掉跑 5400 帧一帧不崩。还回去的东西在这游戏上不对。
//   RE 引擎那类真正 bindless 的游戏才需要它 —— 安装器按白名单写 restorestate=3。
static int cfg_enabled = 0;   // 0 关 / 1 原生还堆+根签名+PSO(★古墓实测掉设备★) / 2 再重放根参数 / 3 ★闸门 + 官方 state_block 回放(推荐)★

// Beta2 grade reuses these same events even when legacy NR restore is off.
#ifdef K033_BETA2_RESHADE_HOST
static constexpr bool grade_observation=true;
#else
static constexpr bool grade_observation=false;
#endif
enum { MAX_LISTS = 16, MAX_PARAMS = 32, MAX_CONSTS = 128, MAX_LAYOUTS = 64 };
// 根参数种类
enum : uint8_t { K_NONE = 0, K_TABLE, K_CBV, K_SRV, K_UAV, K_CONST, K_UNKNOWN };

struct RootArg
{
    uint8_t  kind;
    uint8_t  cfirst;   // 常量: 起始 DWORD(含)
    uint8_t  cend;     // 常量: 结束 DWORD(不含)
    uint8_t  cpool;    // 常量: 在 Record::consts 里的起点
    uint64_t value;    // 表: GPU 描述符句柄 / 视图: GPU 虚拟地址
    ID3D12DescriptorHeap *heap;   // 表: 句柄落在哪个堆(重放时核对还绑着没有)
};

// 一条命令列表的「游戏计算态」快照。POD, 清零 = 空。
struct Record
{
    uint64_t key;        // 原生 ID3D12GraphicsCommandList*
    uint64_t proxy;      // 学到的「游戏手里那个指针」(可能等于 key), 给 captured() 预查用
    uint64_t stamp;      // LRU 时钟
    ID3D12RootSignature  *rs;
    ID3D12PipelineState  *pso;
    ID3D12StateObject    *so;         // SetPipelineState1 绑的(光追), 与 pso 互斥
    ID3D12DescriptorHeap *heaps[2];   // [0]=CBV_SRV_UAV [1]=SAMPLER
    RootArg  args[MAX_PARAMS];        // 档 2
    uint32_t consts[MAX_CONSTS];      // 档 2: 32 位常量池
    uint32_t const_used;
    bool     args_broken;             // 档 2: 有一项还不回去 → 整条放弃参数重放
};

// 档 2: 根签名每个参数是什么种类(从 init_pipeline_layout 学来, 用来分 SRV / UAV)
struct Layout
{
    uint64_t rs, stamp;
    uint8_t  nparams;
    uint8_t  kind[MAX_PARAMS];
};

static Record     g_rec[MAX_LISTS];
static Layout     g_lay[MAX_LAYOUTS];
static std::mutex g_mu;
static uint64_t   g_clock = 0;
static bool       g_registered = false;
static bool       g_level2 = false;   // 载入时 cfg_enabled>=2 才挂档 2 的事件

// 「我们自己在绑」—— 信封打开期间事件处理器只学原生指针、不记录
static thread_local bool     g_ours_binding = false;
static thread_local uint64_t g_ours_native  = 0;
static thread_local uint64_t g_ours_proxy   = 0;
// 堆 → 类型 的两格小缓存, 省掉每次绑表都 GetDesc
static thread_local ID3D12DescriptorHeap *tl_heap[2] = {};
// ★档 3: 不再自己拼原生调用, 改用 ReShade 官方 examples/utils/state_tracking(BSD/MIT)★
//   它记的是【整条列表】的状态；这里把它当严格的“本帧有完整计算态”闸门。
//   DD2 实测把 state_block 的 descriptor_table 经 ReShade 代理回放会 0xC0000005，
//   所以真正还回使用同一批事件捕获的原生 D3D12 状态；原生记录也不完整就整帧跳过。
//   关键: 我们的事件处理器必须比 state_tracking 的【先注册】, 这样我们的 pass 第一次绑定时,
//   我们先跑、把「此刻还是游戏的」那份 state_block 拷走, 然后它才被我们的绑定覆盖。
//   官方范例 13-effects_during_frame 展示了“先快照、作用域结束再恢复”的时序。
struct GameSnap
{
    uint64_t key;                    // 原生命令列表
    uint64_t proxy;                  // 游戏/NGX 手里的代理指针；第一次进信封时学到
    rsu::state_block block;
    reshade::api::command_list *wrap;
    bool valid;
};
static GameSnap g_snap[MAX_LISTS];
static GameSnap *SnapFor(uint64_t native, bool make)
{
    for (int i = 0; i < MAX_LISTS; ++i) if (g_snap[i].key == native) return &g_snap[i];
    if (!make) return nullptr;
    for (int i = 0; i < MAX_LISTS; ++i) if (g_snap[i].key == 0) { g_snap[i].key = native; g_snap[i].proxy = 0; g_snap[i].valid = false; g_snap[i].wrap = nullptr; return &g_snap[i]; }
    g_snap[0].key = native; g_snap[0].proxy = 0; g_snap[0].valid = false; g_snap[0].wrap = nullptr; g_snap[0].block.clear(); return &g_snap[0];
}
static GameSnap *SnapForProxy(uint64_t proxy)
{
    if (proxy == 0) return nullptr;
    for (int i = 0; i < MAX_LISTS; ++i)
        if (g_snap[i].key != 0 && (g_snap[i].key == proxy || g_snap[i].proxy == proxy)) return &g_snap[i];
    return nullptr;
}
// 我们的 pass 在这条列表上第一次绑定时调: 此刻官方 tracker 里还是游戏的状态 → 拷走
static void TakeGameSnapshot(reshade::api::command_list *cl, uint64_t native)
{
    if (cl == nullptr) return;
    const rsu::state_tracking *st = cl->get_private_data<rsu::state_tracking>();
    if (st == nullptr) return;
    GameSnap *g = SnapFor(native, true);
    if (g->valid) return;                       // 这一趟已经拷过
    g->block = *static_cast<const rsu::state_block *>(st);
    g->proxy = g_ours_proxy;
    g->wrap = cl; g->valid = true;
}
static thread_local uint8_t               tl_slot[2] = {};
// 还没进过信封时尚不知道「代理指针 -> 原生指针」的映射。游戏最后一次绑定计算态的
// 事件与 NGX evaluate 在同一录制线程上，先记住那条 ReShade 命令列表，首帧即可做严格闸门；
// 一旦进过信封，后续都改用 GameSnap 里学到的精确映射。
static thread_local reshade::api::command_list *tl_last_game_wrap = nullptr;
static thread_local uint64_t                    tl_last_game_native = 0;

// 统计(只在锁内改), 给面板 / 日志看
static unsigned g_n_empty = 0;   // 档 3: 没有快照可还(空转)
static unsigned g_n_skipped = 0; // 要求还原但这一帧没有完整计算态，整帧跳过
static unsigned g_n_lists = 0, g_n_evicted = 0, g_n_restores = 0, g_n_noop = 0,
                g_n_unresolved = 0, g_n_broken = 0;
static bool g_said_unresolved = false, g_said_broken = false;

// ------------------------------------------------------------ 小工具
static bool IsD3D12(reshade::api::command_list *cl)
{
    reshade::api::device *d = cl->get_device();
    return d != nullptr && d->get_api() == reshade::api::device_api::d3d12;
}
static bool HasCompute(reshade::api::shader_stage s)
{
    return (static_cast<uint32_t>(s) & static_cast<uint32_t>(reshade::api::shader_stage::compute)) != 0;
}

// 查 / 建 记录。满了踢最久没动的那条 —— 它多半早提交出去了, 记录本来就该作废。
static Record *FindLocked(uint64_t key, bool create)
{
    Record *empty = nullptr, *oldest = nullptr;
    for (Record &r : g_rec)
    {
        if (r.key == key) { r.stamp = ++g_clock; return &r; }
        if (r.key == 0) { if (empty == nullptr) empty = &r; }
        else if (oldest == nullptr || r.stamp < oldest->stamp) oldest = &r;
    }
    if (!create) return nullptr;
    Record *r = (empty != nullptr) ? empty : oldest;
    if (empty == nullptr) ++g_n_evicted;
    std::memset(r, 0, sizeof(*r));
    r->key = key; r->stamp = ++g_clock;
    ++g_n_lists;
    return r;
}
static void ClearLocked(uint64_t key)
{
    if (Record *r = FindLocked(key, false)) std::memset(r, 0, sizeof(*r));
}
// 换了根签名 = 旧根参数全部作废(D3D12 语义)
static void ClearArgs(Record &r)
{
    std::memset(r.args, 0, sizeof(r.args));
    r.const_used = 0;
    r.args_broken = false;
}
static Layout *FindLayoutLocked(uint64_t rs, bool create)
{
    Layout *empty = nullptr, *oldest = nullptr;
    for (Layout &l : g_lay)
    {
        if (l.rs == rs) { l.stamp = ++g_clock; return &l; }
        if (l.rs == 0) { if (empty == nullptr) empty = &l; }
        else if (oldest == nullptr || l.stamp < oldest->stamp) oldest = &l;
    }
    if (!create) return nullptr;
    Layout *l = (empty != nullptr) ? empty : oldest;
    std::memset(l, 0, sizeof(*l));
    l->rs = rs; l->stamp = ++g_clock;
    return l;
}
static uint8_t KindFromDescType(reshade::api::descriptor_type t)
{
    using reshade::api::descriptor_type;
    switch (t)
    {
    case descriptor_type::constant_buffer:
    case descriptor_type::constant_buffer_with_dynamic_offset: return K_CBV;
    case descriptor_type::shader_resource_view:            // == texture_shader_resource_view
    case descriptor_type::buffer_shader_resource_view:     return K_SRV;
    case descriptor_type::unordered_access_view:           // == texture_unordered_access_view
    case descriptor_type::buffer_unordered_access_view:    return K_UAV;
    default: return K_UNKNOWN;   // shader_storage_buffer 分不清是 SRV 还是 UAV
    }
}
// 这个堆放 heaps[] 的哪一格。锁外调(GetDesc 是 D3D12 调用, 不触发事件, 但也别占着锁)。
static uint8_t HeapSlot(ID3D12DescriptorHeap *h)
{
    if (h == tl_heap[0]) return tl_slot[0];
    if (h == tl_heap[1]) return tl_slot[1];
    const D3D12_DESCRIPTOR_HEAP_DESC d = h->GetDesc();
    const uint8_t slot = (d.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) ? 1 : 0;
    tl_heap[1] = tl_heap[0]; tl_slot[1] = tl_slot[0];
    tl_heap[0] = h;          tl_slot[0] = slot;
    return slot;
}
static void MarkBroken(Record &r) { if (!r.args_broken) { r.args_broken = true; ++g_n_broken; } }

// ------------------------------------------------------------ 事件处理器
static void OnBindPipeline(reshade::api::command_list *cl, reshade::api::pipeline_stage stages,
                           reshade::api::pipeline p)
{
    if ((cfg_enabled <= 0 && !grade_observation) || cl == nullptr) return;
    const uint64_t native = cl->get_native();
    if (g_ours_binding) { g_ours_native = native; TakeGameSnapshot(cl, native); return; }
    if (!IsD3D12(cl)) return;
    // D3D12 只有一个「当前管线」槽: SetPipelineState(ReShade 报 all, 含 compute 位) 和
    // SetPipelineState1(报 all_ray_tracing, 不含 compute 位) 都往这一个槽里放,
    // 所以两种都记 —— 记的就是「游戏此刻绑着的那个」。
    const uint32_t s = static_cast<uint32_t>(stages);
    const bool has_cs = (s & static_cast<uint32_t>(reshade::api::pipeline_stage::compute_shader)) != 0;
    const bool has_rt = (s & static_cast<uint32_t>(reshade::api::pipeline_stage::ray_tracing_shader)) != 0;
    if (has_cs) { tl_last_game_wrap = cl; tl_last_game_native = native; }
    std::lock_guard<std::mutex> lk(g_mu);
    Record *r = FindLocked(native, true);
    if (has_rt && !has_cs) { r->so  = reinterpret_cast<ID3D12StateObject *>(p.handle);   r->pso = nullptr; }
    else                   { r->pso = reinterpret_cast<ID3D12PipelineState *>(p.handle); r->so  = nullptr; }
}

static void OnBindTables(reshade::api::command_list *cl, reshade::api::shader_stage stages,
                         reshade::api::pipeline_layout layout, uint32_t first, uint32_t count,
                         const reshade::api::descriptor_table *tables, uint32_t, const uint32_t *)
{
    if ((cfg_enabled <= 0 && !grade_observation) || cl == nullptr) return;
    const uint64_t native = cl->get_native();
    if (g_ours_binding) { g_ours_native = native; TakeGameSnapshot(cl, native); return; }
    if (!HasCompute(stages) || !IsD3D12(cl)) return;
    tl_last_game_wrap = cl; tl_last_game_native = native;

    // 先在锁外把表解析成堆(要调设备和 GetDesc), 临界区只做赋值。
    // 一次 SetComputeRootDescriptorTable 只有 1 张表; 8 是留余量。
    enum { MAXT = 8 };
    ID3D12DescriptorHeap *heap_of[MAXT] = {};
    uint8_t slot_of[MAXT] = {};
    uint32_t n = (tables != nullptr) ? count : 0;
    if (n > MAXT) n = MAXT;
    unsigned unresolved = 0;
    if (n > 0)
    {
        reshade::api::device *dev = cl->get_device();
        for (uint32_t i = 0; i < n; ++i)
        {
            reshade::api::descriptor_heap h = { 0 }; uint32_t off = 0;
            if (dev != nullptr && tables[i].handle != 0)
                dev->get_descriptor_heap_offset(tables[i], 0, 0, &h, &off);
            heap_of[i] = reinterpret_cast<ID3D12DescriptorHeap *>(h.handle);
            if (heap_of[i] != nullptr) slot_of[i] = HeapSlot(heap_of[i]);
            else ++unresolved;
        }
    }

    bool say = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Record *r = FindLocked(native, true);
        ID3D12RootSignature *rs = reinterpret_cast<ID3D12RootSignature *>(layout.handle);
        if (rs != r->rs) { r->rs = rs; ClearArgs(*r); }
        if (unresolved != 0) { g_n_unresolved += unresolved; if (!g_said_unresolved) { g_said_unresolved = true; say = true; } }
        for (uint32_t i = 0; i < n; ++i)
        {
            if (heap_of[i] != nullptr) r->heaps[slot_of[i]] = heap_of[i];
            if (!g_level2) continue;
            const uint32_t param = first + i;
            if (param >= MAX_PARAMS) { MarkBroken(*r); continue; }
            RootArg &a = r->args[param];
            a.kind = K_TABLE; a.value = tables[i].handle; a.heap = heap_of[i];
            a.cfirst = a.cend = a.cpool = 0;
        }
    }
    if (say) STATERESTORE_LOG("[staterestore] 有描述符表认不出所在的堆(ReShade 没登记那个堆) —— 那一格堆还不回去");
}

// 档 2: 根 CBV / SRV / UAV(按 GPU 地址绑的那种)
static void OnPushDescriptors(reshade::api::command_list *cl, reshade::api::shader_stage stages,
                              reshade::api::pipeline_layout layout, uint32_t param,
                              const reshade::api::descriptor_table_update &u)
{
    if ((cfg_enabled < 2 && !grade_observation) || cl == nullptr) return;
    const uint64_t native = cl->get_native();
    if (g_ours_binding) { g_ours_native = native; return; }
    if (!HasCompute(stages) || !IsD3D12(cl)) return;

    using reshade::api::descriptor_type;
    // 只有这几种的 descriptors 才是 buffer_range(资源 + 偏移), 能拼回 GPU 地址;
    // 其它种类(resource_view 句柄)在 D3D12 上不可能是根视图, 拼了就是乱地址。
    const bool is_range = (u.type == descriptor_type::constant_buffer ||
                           u.type == descriptor_type::constant_buffer_with_dynamic_offset ||
                           u.type == descriptor_type::shader_storage_buffer ||
                           u.type == descriptor_type::shader_storage_buffer_with_dynamic_offset);
    uint8_t  kind = K_UNKNOWN;
    uint64_t va   = 0;
    if (is_range && u.count == 1 && u.descriptors != nullptr)
    {
        const auto *br = static_cast<const reshade::api::buffer_range *>(u.descriptors);
        ID3D12Resource *res = reinterpret_cast<ID3D12Resource *>(br->buffer.handle);
        if (res != nullptr) { va = res->GetGPUVirtualAddress() + br->offset; kind = KindFromDescType(u.type); }
    }

    bool say = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Record *r = FindLocked(native, true);
        ID3D12RootSignature *rs = reinterpret_cast<ID3D12RootSignature *>(layout.handle);
        if (rs != r->rs) { r->rs = rs; ClearArgs(*r); }
        if (kind == K_UNKNOWN || kind == K_CBV)
        {
            // shader_storage_buffer 分不清 SRV/UAV → 问根签名建的时候学到的种类(更可信)
            if (Layout *L = FindLayoutLocked(layout.handle, false))
                if (param < L->nparams && L->kind[param] != K_NONE) kind = L->kind[param];
        }
        if (param >= MAX_PARAMS || va == 0 || !(kind == K_CBV || kind == K_SRV || kind == K_UAV))
        {
            MarkBroken(*r);
            if (!g_said_broken) { g_said_broken = true; say = true; }
        }
        else
        {
            RootArg &a = r->args[param];
            a.kind = kind; a.value = va; a.heap = nullptr; a.cfirst = a.cend = a.cpool = 0;
        }
    }
    if (say) STATERESTORE_LOG("[staterestore] 有根视图还不回去(地址认不出 / 分不清 SRV·UAV / 参数号>=%d) —— 那条列表放弃根参数重放, 只还堆+根签名+PSO", MAX_PARAMS);
}

// 档 2: 32 位根常量。同一参数可能分几次、按不同偏移设, 池里按 [cfirst,cend) 存, 不够就扩。
static void OnPushConstants(reshade::api::command_list *cl, reshade::api::shader_stage stages,
                            reshade::api::pipeline_layout layout, uint32_t param,
                            uint32_t first, uint32_t count, const void *values)
{
    if ((cfg_enabled < 2 && !grade_observation) || cl == nullptr) return;
    const uint64_t native = cl->get_native();
    if (g_ours_binding) { g_ours_native = native; return; }
    if (!HasCompute(stages) || !IsD3D12(cl) || values == nullptr || count == 0) return;

    std::lock_guard<std::mutex> lk(g_mu);
    Record *r = FindLocked(native, true);
    ID3D12RootSignature *rs = reinterpret_cast<ID3D12RootSignature *>(layout.handle);
    if (rs != r->rs) { r->rs = rs; ClearArgs(*r); }
    if (param >= MAX_PARAMS || first + count > 64) { MarkBroken(*r); return; }   // 根签名总共才 64 DWORD

    RootArg &a = r->args[param];
    const uint32_t nf = first, ne = first + count;
    if (a.kind != K_CONST)
    {
        const uint32_t size = ne - nf;
        if (r->const_used + size > MAX_CONSTS) { MarkBroken(*r); return; }
        a.kind = K_CONST; a.value = 0; a.heap = nullptr;
        a.cfirst = static_cast<uint8_t>(nf); a.cend = static_cast<uint8_t>(ne);
        a.cpool = static_cast<uint8_t>(r->const_used);
        r->const_used += size;
        std::memset(&r->consts[a.cpool], 0, size * sizeof(uint32_t));
    }
    else if (nf < a.cfirst || ne > a.cend)
    {
        // 范围扩了: 另起一段, 把旧值搬过去(旧段留在池里不回收, 换根签名时整池清零)
        const uint32_t f2 = (nf < a.cfirst) ? nf : a.cfirst;
        const uint32_t e2 = (ne > a.cend) ? ne : a.cend;
        const uint32_t size = e2 - f2;
        if (r->const_used + size > MAX_CONSTS) { MarkBroken(*r); return; }
        uint32_t *dst = &r->consts[r->const_used];
        std::memset(dst, 0, size * sizeof(uint32_t));
        std::memcpy(dst + (a.cfirst - f2), &r->consts[a.cpool], (a.cend - a.cfirst) * sizeof(uint32_t));
        a.cpool = static_cast<uint8_t>(r->const_used);
        r->const_used += size;
        a.cfirst = static_cast<uint8_t>(f2); a.cend = static_cast<uint8_t>(e2);
    }
    std::memcpy(&r->consts[a.cpool + (nf - a.cfirst)], values, count * sizeof(uint32_t));
}

// 档 2: 根签名建好时记下每个参数是什么种类
static void OnInitLayout(reshade::api::device *dev, uint32_t param_count,
                         const reshade::api::pipeline_layout_param *params, reshade::api::pipeline_layout layout)
{
    if ((cfg_enabled < 2 && !grade_observation) || dev == nullptr || params == nullptr || layout.handle == 0) return;
    if (dev->get_api() != reshade::api::device_api::d3d12) return;
    using reshade::api::pipeline_layout_param_type;
    std::lock_guard<std::mutex> lk(g_mu);
    Layout *L = FindLayoutLocked(layout.handle, true);
    L->nparams = static_cast<uint8_t>((param_count > MAX_PARAMS) ? MAX_PARAMS : param_count);
    for (uint32_t i = 0; i < L->nparams; ++i)
    {
        const reshade::api::pipeline_layout_param &p = params[i];
        uint8_t k = K_UNKNOWN;
        switch (p.type)
        {
        case pipeline_layout_param_type::push_constants:              k = K_CONST; break;
        case pipeline_layout_param_type::push_descriptors:            k = KindFromDescType(p.push_descriptors.type); break;
        case pipeline_layout_param_type::push_descriptors_with_ranges:
            k = (p.descriptor_table.count > 0 && p.descriptor_table.ranges != nullptr)
                ? KindFromDescType(p.descriptor_table.ranges[0].type) : K_UNKNOWN; break;
        case pipeline_layout_param_type::push_descriptors_with_ranges_and_flags:
            k = (p.descriptor_table_with_flags.count > 0 && p.descriptor_table_with_flags.ranges != nullptr)
                ? KindFromDescType(p.descriptor_table_with_flags.ranges[0].type) : K_UNKNOWN; break;
        case pipeline_layout_param_type::descriptor_table:
        case pipeline_layout_param_type::descriptor_table_with_flags:  k = K_TABLE; break;
        default: break;
        }
        L->kind[i] = k;
    }
}
static void OnDestroyLayout(reshade::api::device *, reshade::api::pipeline_layout layout)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (Layout *L = FindLayoutLocked(layout.handle, false)) std::memset(L, 0, sizeof(*L));
}

// Reset 之前 / 销毁之前: 记录作废。这两处不看 cfg —— 陈旧指针任何档位都不许留。
static void OnReset(reshade::api::command_list *cl)
{
    if (cl != nullptr) { GameSnap *g = SnapFor(cl->get_native(), false); if (g) { g->valid = false; g->block.clear(); } }
    if (cl != nullptr && tl_last_game_native == cl->get_native()) { tl_last_game_wrap = nullptr; tl_last_game_native = 0; }
    if (cl == nullptr) return;
    std::lock_guard<std::mutex> lk(g_mu);
    ClearLocked(cl->get_native());
}
static void OnDestroy(reshade::api::command_list *cl)
{
    if (cl != nullptr) { GameSnap *g = SnapFor(cl->get_native(), false); if (g) { g->key = 0; g->proxy = 0; g->valid = false; g->wrap = nullptr; g->block.clear(); } }
    if (cl != nullptr && tl_last_game_native == cl->get_native()) { tl_last_game_wrap = nullptr; tl_last_game_native = 0; }
    if (cl == nullptr) return;
    std::lock_guard<std::mutex> lk(g_mu);
    ClearLocked(cl->get_native());
}

// ------------------------------------------------------------ 还回去
static void ReplayArgs(ID3D12GraphicsCommandList *cl, const Record &r, UINT nheaps, ID3D12DescriptorHeap *const *heaps)
{
    for (uint32_t i = 0; i < MAX_PARAMS; ++i)
    {
        const RootArg &a = r.args[i];
        switch (a.kind)
        {
        case K_TABLE:
        {
            // 句柄必须落在此刻绑着的堆里。游戏换过堆却没重绑这一项的话, 它自己也不会再用它,
            // 我们放回去反而是非法调用。
            bool ok = false;
            for (UINT h = 0; h < nheaps; ++h) if (heaps[h] == a.heap) ok = true;
            if (!ok) break;
            D3D12_GPU_DESCRIPTOR_HANDLE gh; gh.ptr = a.value;
            cl->SetComputeRootDescriptorTable(i, gh);
            break;
        }
        case K_CBV:   cl->SetComputeRootConstantBufferView(i, a.value); break;
        case K_SRV:   cl->SetComputeRootShaderResourceView(i, a.value); break;
        case K_UAV:   cl->SetComputeRootUnorderedAccessView(i, a.value); break;
        case K_CONST: cl->SetComputeRoot32BitConstants(i, a.cend - a.cfirst, &r.consts[a.cpool], a.cfirst); break;
        default: break;
        }
    }
}

// state_block 是“这帧确实抓到了计算态”的第一道门；真正重放用同一时刻由事件
// 记录下来的原生 D3D12 值。DD2 上把 state_block 里的 descriptor_table 再交回
// ReShade 会在代理转换处 0xC0000005，而原生句柄正是 D3D12 要的值。
// 这里做严格完整性检查：根签名的每一个参数都必须有同种类的实值，任何一项
// 认不出来就返回 false，让 allow_frame 在动命令列表之前整帧跳过。
static bool CopyStrictRecord(ID3D12GraphicsCommandList *cl, Record &out)
{
    if (cl == nullptr) return false;
    const uint64_t direct = reinterpret_cast<uint64_t>(cl);
    std::lock_guard<std::mutex> lk(g_mu);
    Record *r = (g_ours_native != 0) ? FindLocked(g_ours_native, false) : nullptr;
    if (r == nullptr) r = FindLocked(direct, false);
    if (r == nullptr && tl_last_game_native != 0) r = FindLocked(tl_last_game_native, false);
    if (r == nullptr || r->rs == nullptr || (r->pso == nullptr && r->so == nullptr) || r->args_broken)
        return false;
    Layout *L = FindLayoutLocked(reinterpret_cast<uint64_t>(r->rs), false);
    if (L == nullptr || L->nparams == 0) return false;
    for (uint32_t i = 0; i < L->nparams; ++i)
    {
        const uint8_t want = L->kind[i];
        if (want == K_NONE) continue;
        if (want == K_UNKNOWN || r->args[i].kind != want) return false;
        if (want == K_TABLE)
        {
            ID3D12DescriptorHeap *h = r->args[i].heap;
            if (h == nullptr || (h != r->heaps[0] && h != r->heaps[1])) return false;
        }
    }
    out = *r;
    return true;
}

// ★官方 state_block 回放 + SEH 兜底★ (2026-09-04 14:2x 实机定案)
//   实测(古墓丽影暗影 DX12, 就地插入路, 候选 r6):
//     restorestate=0 → 稳, 神经渲染正常出帧;
//     restorestate=1 → 帧 1 之后 3 毫秒 DXGI_ERROR_DEVICE_REMOVED(INVALID_CALL) + 游戏崩;
//     restorestate=3 → 同一签名, 两次两崩(闸门先跳 600 帧, 放行的第一帧就掉设备)。
//   两个档在 r6 里都是【原生重放】(SetDescriptorHeaps/SetComputeRootSignature/ReplayArgs/SetPipelineState)。
//   而同一天 11:52 的构建用【官方 state_block::apply】在同一游戏跑满 100.9 秒、还回 7801 次、零异常。
//   → 结论: 把游戏的计算态用原生调用重新绑回去这件事本身在古墓上是非法的; 官方回放是安全的那条。
//   DD2 上官方回放会 0xC0000005(Codex 13:0x 实测), 所以这里包 SEH: 炸一次就把本局的
//   神经渲染永久关掉(allow_frame 从此一律跳帧), 绝不给它炸第二次的机会。
//   ★SEH 必须独占一个函数★ —— 同一函数里有需要析构的 C++ 对象时 MSVC 拒编(C2712)。
static bool     g_apply_failed = false;
static unsigned g_n_apply_fail = 0;

static bool ApplyBlockGuarded(rsu::state_block *blk, reshade::api::command_list *wrap)
{
    __try { blk->apply(wrap); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 把 cl 上捕获到的游戏计算态绑回去。没记录 = 什么都不做。
// 顺序: 堆 → 根签名(→ 档 2 根参数) → 管线。先复制一份快照再出锁 —— 下面每个 D3D12 调用
// 都会经代理再触发事件、再来拿锁。
static void Restore(ID3D12GraphicsCommandList *cl)
{    // ★档 3★ state_block 作严格闸门，原生记录负责重放（原因见 CopyStrictRecord）。
    if (cfg_enabled == 3)
    {
        const uint64_t k = (g_ours_native != 0) ? g_ours_native : reinterpret_cast<uint64_t>(cl);
        GameSnap *g = SnapFor(k, false);
        Record snap;
        if (g != nullptr && g->valid && g->wrap != nullptr)
        {
            // 严格记录仍是前置条件(证明这帧确实抓全了计算态), 重放才交给官方 state_block。
            if (!g_apply_failed && CopyStrictRecord(cl, snap))
            {
                if (ApplyBlockGuarded(&g->block, g->wrap)) ++g_n_restores;
                else
                {
                    g_apply_failed = true; ++g_n_apply_fail;
                    STATERESTORE_LOG("[staterestore] 官方回放炸了(0xC0000005 一类), 本局起永久跳过神经渲染");
                }
            }
            else ++g_n_empty;
        }
        else ++g_n_empty;
        // 每 600 次写一行, 没面板也能从日志确认它真的在还
        if (((g_n_restores + g_n_empty) % 600) == 1)
            STATERESTORE_LOG("[staterestore] 档3: 已还回 %u 次, 空转 %u 次", g_n_restores, g_n_empty);
        if (g != nullptr) g->valid = false;     // 下一趟重新拷
        return;
    }

    if (cl == nullptr || cfg_enabled <= 0) return;
    Record snap;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        Record *r = (g_ours_native != 0) ? FindLocked(g_ours_native, false) : nullptr;
        if (r == nullptr) r = FindLocked(reinterpret_cast<uint64_t>(cl), false);
        if (r != nullptr) { r->proxy = reinterpret_cast<uint64_t>(cl); snap = *r; found = true; ++g_n_restores; }
        else ++g_n_noop;
    }
    if (!found) return;

    ID3D12DescriptorHeap *heaps[2] = {};
    UINT n = 0;
    if (snap.heaps[0] != nullptr) heaps[n++] = snap.heaps[0];
    if (snap.heaps[1] != nullptr) heaps[n++] = snap.heaps[1];
    if (n > 0) cl->SetDescriptorHeaps(n, heaps);

    if (snap.rs != nullptr)
    {
        cl->SetComputeRootSignature(snap.rs);
        if (cfg_enabled >= 2 && g_level2 && !snap.args_broken) ReplayArgs(cl, snap, n, heaps);
    }

    if (snap.pso != nullptr) cl->SetPipelineState(snap.pso);
    else if (snap.so != nullptr)
    {
        ID3D12GraphicsCommandList4 *cl4 = nullptr;
        if (SUCCEEDED(cl->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void **>(&cl4))) && cl4 != nullptr)
        { cl4->SetPipelineState1(snap.so); cl4->Release(); }
    }
}

// ------------------------------------------------------------ 对外
// RAII 信封: 建在我们第一次绑之前, 出作用域(含所有早退)把游戏的计算态还回去。
struct Envelope
{
    ID3D12GraphicsCommandList *cl;
    bool prev,restore;
    explicit Envelope(ID3D12GraphicsCommandList *c,bool gameState=true) : cl(c), prev(g_ours_binding),restore(gameState)
    {
        g_ours_binding = true;
        if (!prev) { g_ours_native = 0; g_ours_proxy = reinterpret_cast<uint64_t>(c); }
    }
    ~Envelope()
    {
        if(restore)Restore(cl);
        g_ours_binding = prev;
        if (!prev) { g_ours_native = 0; g_ours_proxy = 0; }
    }
    Envelope(const Envelope &) = delete;
    Envelope &operator=(const Envelope &) = delete;
};

static bool StateBlockCanRestore(reshade::api::command_list *cl)
{
    if (cl == nullptr || !IsD3D12(cl)) return false;
    const rsu::state_tracking *st = cl->get_private_data<rsu::state_tracking>();
    if (st == nullptr) return false;

    bool have_compute_pipeline = false;
    for (const auto &it : st->pipelines)
    {
        const uint32_t stages = static_cast<uint32_t>(it.first);
        if ((stages & static_cast<uint32_t>(reshade::api::pipeline_stage::compute_shader)) != 0 &&
            it.second.handle != 0)
        { have_compute_pipeline = true; break; }
    }

    bool have_compute_layout = false;
    for (const auto &it : st->descriptor_tables)
        if (HasCompute(it.first) && it.second.first.handle != 0)
        { have_compute_layout = true; break; }

    // 换计算根签名会让根参数全部失效。没有计算 pipeline 或 layout，state_block 就不是
    // 一封能封口的快照；宁可少跑一帧模型，也不能猜一套状态绑回游戏。
    return have_compute_pipeline && have_compute_layout;
}

// 在任何自己的 barrier/copy/bind 之前调用。restorestate=0 的普通游戏不受影响；
// 要求还原的游戏只有在确认能把计算态完整还回去时才允许这一帧神经渲染。
static bool allow_frame(ID3D12GraphicsCommandList *cl)
{
    if (cfg_enabled <= 0) return true;
    if (cl == nullptr) return false;

    bool ok = false;
    if (cfg_enabled == 3)
    {
        GameSnap *g = SnapForProxy(reinterpret_cast<uint64_t>(cl));
        reshade::api::command_list *wrap = (g != nullptr && g->wrap != nullptr) ? g->wrap : tl_last_game_wrap;
        // ★★放宽这道闸是错的, 已实测撤回 (2026-09-04 16:08, 龙之信条 2)★★
        //   我曾把判据放宽成「只问官方 state_block 能不能还」, 理由是还原本来就走官方回放,
        //   拿我们自己那份严格原生记录当门槛显得多余; 并且以为「万一炸了有 SEH 兜住,
        //   最坏退化成安全跳过」。
        //   实测: 龙信 2 上第 1 帧就 0xC0000005 —— SEH 确实兜住了 CPU 侧的异常,
        //   但【设备已经在 GPU 侧被移除了】: Fatal D3D error (24, DXGI_ERROR_DEVICE_REMOVED,
        //   0x887a0005) DeviceRemovedReason 0x887a0006, 42 秒崩。
        //   也就是说 SEH 只能防「进程当场挂掉」, 防不住「已经把脏状态提交给了驱动」。
        //   对照: 严格记录当闸门时, 同一游戏连跳 8400 帧、90 秒零事故。
        //   → 结论: 闸门必须比还原机制【更严】, 严格记录是那个保险丝, 不能省。
        Record strict;
        ok = !g_apply_failed && StateBlockCanRestore(wrap) && CopyStrictRecord(cl, strict);
    }
    else
    {
        const uint64_t k = reinterpret_cast<uint64_t>(cl);
        std::lock_guard<std::mutex> lk(g_mu);
        Record *r = FindLocked(k, false);
        if (r == nullptr && tl_last_game_native != 0) r = FindLocked(tl_last_game_native, false);
        ok = (r != nullptr && r->rs != nullptr);
    }

    if (ok) return true;
    unsigned skipped = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        skipped = ++g_n_skipped;
    }
    if (skipped == 1 || (skipped % 600) == 0)
        STATERESTORE_LOG("[staterestore] 没捕获到完整计算态，已整帧跳过神经渲染 %u 次", skipped);
    return false;
}

// 这条列表上有没有捕获到游戏的根签名(给「没抓到就别插这一帧」那种闸用; 按代理指针或原生指针都认)
static bool captured(ID3D12GraphicsCommandList *cl)
{
    if (cl == nullptr) return false;
    const uint64_t k = reinterpret_cast<uint64_t>(cl);
    std::lock_guard<std::mutex> lk(g_mu);
    for (const Record &r : g_rec)
        if (r.key != 0 && (r.key == k || r.proxy == k)) return r.rs != nullptr;
    return false;
}

#ifdef K033_BETA2_RESHADE_HOST
// Private copy of the existing tracker. No native hook, queue or guessed last
// command-list association is added for grade. A missing exact identity skips.
class GradeEnvelope {
    ID3D12GraphicsCommandList* command=nullptr;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> original;
    reshade::api::command_list* wrap=nullptr;
    rsu::state_block block;
    uint64_t native=0,stamp=0,previous_native=0,previous_proxy=0;
    bool prepared=false,armed=false,previous_binding=false;
public:
    explicit GradeEnvelope(ID3D12GraphicsCommandList* list):command(list){
        if(!list||!g_registered||!g_level2||g_apply_failed||g_ours_binding)return;
        // Official ReShade 6.8.0 IID_UnwrappedObject, already shared by our
        // typed-device identity helper. Own the QI result; do not cast layouts.
        auto identity=identity033::Canonical(list);
        if(!identity||FAILED(identity.As(&original)))return;
        const auto direct=reinterpret_cast<uint64_t>(original.Get());
        // This thread's event wrapper is accepted only after exact owning
        // unwrap identity equality. Do not read the cross-thread g_snap table.
        if(tl_last_game_wrap&&tl_last_game_native==direct){wrap=tl_last_game_wrap;native=direct;}
        if(!wrap||wrap->get_native()!=native||!StateBlockCanRestore(wrap))return;
        // CopyStrictRecord must use this proven identity, never its fallback.
        Record strict;const auto saved=g_ours_native;g_ours_native=native;
        bool exact=false;{std::lock_guard<std::mutex> lock(g_mu);for(const auto& item:g_rec)if(item.key==native)exact=true;}
        const bool valid=exact&&CopyStrictRecord(list,strict)&&strict.key==native;
        g_ours_native=saved;if(!valid)return;
        const auto* tracker=wrap->get_private_data<rsu::state_tracking>();if(!tracker)return;
        block=*static_cast<const rsu::state_block*>(tracker);stamp=strict.stamp;prepared=true;
    }
    bool Current()const {
        if(!prepared||armed||g_apply_failed)return false;
        std::lock_guard<std::mutex> lock(g_mu);
        for(const auto& item:g_rec)if(item.key==native)return item.stamp==stamp;
        return false;
    }
    void Arm(){
        previous_binding=g_ours_binding;previous_native=g_ours_native;previous_proxy=g_ours_proxy;
        g_ours_binding=true;g_ours_native=native;g_ours_proxy=reinterpret_cast<uint64_t>(command);armed=true;
    }
    bool Finish(){
        if(!armed)return true;
        const bool ok=ApplyBlockGuarded(&block,wrap);if(!ok){g_apply_failed=true;++g_n_apply_fail;}
        g_ours_binding=previous_binding;g_ours_native=previous_native;g_ours_proxy=previous_proxy;armed=false;prepared=false;
        return ok;
    }
    ~GradeEnvelope(){if(armed)Finish();}
};
#endif
static void register_events()
{
    if (g_registered || (cfg_enabled <= 0 && !grade_observation)) return;
    g_registered = true;
    reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
    reshade::register_event<reshade::addon_event::bind_descriptor_tables>(OnBindTables);
    reshade::register_event<reshade::addon_event::reset_command_list>(OnReset);
    reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroy);
    if (cfg_enabled >= 2 || grade_observation)
    {
        g_level2 = true;
        reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
        reshade::register_event<reshade::addon_event::push_constants>(OnPushConstants);
        reshade::register_event<reshade::addon_event::init_pipeline_layout>(OnInitLayout);
        reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(OnDestroyLayout);
    }
    STATERESTORE_LOG("[staterestore] 状态信封已挂 (restorestate=%d%s)", cfg_enabled, g_level2 ? ", 含根参数重放" : "");
    // ★必须在我们自己的处理器之后注册官方 tracker★ —— 同一事件按注册顺序调用,
    //   我们先拷「游戏的」快照, 它再把我们的绑定记进去。
    rsu::state_tracking::register_events();
}
static void unregister_events()
{
    rsu::state_tracking::unregister_events();
    if (!g_registered) return;
    g_registered = false;
    if (g_level2)
    {
        g_level2 = false;
        reshade::unregister_event<reshade::addon_event::destroy_pipeline_layout>(OnDestroyLayout);
        reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(OnInitLayout);
        reshade::unregister_event<reshade::addon_event::push_constants>(OnPushConstants);
        reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
    }
    reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroy);
    reshade::unregister_event<reshade::addon_event::reset_command_list>(OnReset);
    reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(OnBindTables);
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
    std::lock_guard<std::mutex> lk(g_mu);
    std::memset(g_rec, 0, sizeof(g_rec));
    std::memset(g_lay, 0, sizeof(g_lay));
    for (GameSnap &g : g_snap)
    {
        g.key = 0; g.proxy = 0; g.wrap = nullptr; g.valid = false;
        g.block.clear();
    }
    tl_last_game_wrap = nullptr; tl_last_game_native = 0;
    g_ours_proxy = 0; g_ours_native = 0; g_ours_binding = false;
}

// 一行状态, 给面板 / 定期日志(只在面板线程调, 静态缓冲)
static const char *note()
{
    static char b[192];
    unsigned lists, evicted, restores, noop, skipped, unresolved, broken;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        lists = g_n_lists; evicted = g_n_evicted; restores = g_n_restores; noop = g_n_noop;
        skipped = g_n_skipped; unresolved = g_n_unresolved; broken = g_n_broken;
    }
    if (!g_registered)
        std::snprintf(b, sizeof(b), "未挂 (restorestate=%d)", cfg_enabled);
    else
        std::snprintf(b, sizeof(b), "档 %d · 记 %u 条列表(踢 %u) · 还 %u 次 · 空转 %u · 跳帧 %u · 堆认不出 %u · 参数放弃 %u",
                      cfg_enabled, lists, evicted, restores, noop, skipped, unresolved, broken);
    return b;
}
static bool active() { return g_registered; }

} // namespace staterestore
