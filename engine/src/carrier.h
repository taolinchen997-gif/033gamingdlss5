#include "config_store.h"
#if defined(K033_BETA2_RESHADE_HOST)
#include "beta2_shared_settings.h"
#include "yanyun_hotkey_store.h"
#endif
#include <algorithm>
#include <atomic>
#include "exposure_policy.h"
#include "nr_feature_policy.h"
#include "nr_skin_policy.h"
// =====================================================================
//  033 · 载体管线 (v0.3)
//
//  目标: 给【所有路线, 含直挂】装上"工作分辨率"杠杆。
//
//  做法(D3D12 零拷贝路径 + 计算着色器缩放/匹配残差):
//    每帧在 reshade_finish_effects 里:
//      backbuffer ─copy─▶ capture(全尺寸) ─缩小─▶ color(小) ─我们自己的 DLAA 契约 evaluate─▶
//      output(小) ─与同尺寸输入做差─▶ Lanczos3 放大残差 ─叠回原始全尺寸图─▶ backbuffer
//    renodx 会钩住我们这份契约的 CreateFeature/EvaluateFeature, 把 NR 插进去 ——
//    于是 NR 在【小尺寸】上跑, 开销 ∝ 像素数: 75% 省一半, 50% 剩四分之一。
//
//  为什么要 capture 中转: ①backbuffer 不能可靠地被 shader 采样(Feeder 1771-1775);
//  ②ReShade D3D12 后端不会缩放拷贝(device_caps::blit=false), 缩放只能自己算;
//  ③对 backbuffer 的一切状态转换都走 ReShade 的 barrier/copy_resource, 让它的状态追踪不乱。
//
//  继承 Feeder 的全部保命机制: SEH 包裹 create/evaluate/release, 异常后 AbortCommands 绝不执行,
//  allocator 环 + fence 超时, 三次失败自动停, create_delay 宽限(renodx 异步装钩, 同设备+classic
//  renodx 要 ≥300 帧), g_ngx_dying 闸门(设备销毁/进程退出后不再碰 NGX), 每帧路径串行化。
//  ★绝不调 Shutdown1★(会拆游戏自己的 NGX 会话, 实测游戏被杀)。
//
//  本头文件被 dlss5_033.cpp 包含, 依赖它定义的 g_ngx / g_hs / do_handshake / Log / game_dir。
//  移植底本: E:\033插件\ref\DLSS5-Feeder\dlss5-feed.cpp (MIT), 行号见注释。
// =====================================================================
#pragma once
#include "nr_layer_settings.h"
#include "scale.h"

namespace carrier
{
using namespace reshade::api;

// ------------------------------------------------------------ 配置
struct Cfg
{
    nrlayers::Model extra[2]={nrlayers::Neutral(1),nrlayers::Neutral(2)};
    pregrade::Settings pre;
    int mas=0; // OptiScaler capability adoption, independent resolve implementation; opt-in.
    float mas_still=0.20f, mas_moving=0.05f, mas_threshold=8.0f;
    int enabled = 1;         // cfg: carrier=0 可关
    // cfg: mode=1 只做传输(拷贝→缩小→放大→回写, 不碰 NGX, 验链路)
    //      mode=2 建 DLAA 契约, 指望 renodx 把 NR 插进来(老做法, 靠别人)
    //      mode=3 ★我们自己走转发器直接建 feature 18★ —— 不依赖 renodx, 见 nrfwd.h
    int mode    = 3;
    // ★4K/5K 安装默认 75%★ 模型基准是【整帧】(modelfull=1), 所以 75% 在 5120x2160 上
    //   是 3840x1620 = 620 万像素: 比整帧 1106 万省 1.78 倍, 又比钉在游戏渲染
    //   分辨率(372 万)清楚。业主拍板: 整帧太贵。
    //   面板上按 +50 显示, 所以这一档写作「125」。1440p 及以下安装器写 100%(显示 150)。
    //   低档现在用匹配残差合成减轻边缘光晕，但仍不是“肉眼无损”。
    // 2026-09-12 业主：「现在 SR 默认层数用户进去皆设置成 70%」。出厂满分辨率跑是评论区
    // 「性能损耗很大」的大头。只改编译期默认：ApplyNr 是「有值才覆盖」，所以自己调过的人不受影响。
    int work    = 70;        // Model dimension scale, 25..200 percent.
    int passwork = 70;       // Extra-pass dimensions relative to first pass, 50..100.
    int passwork3 = 70;      // Third layer has its own dimensions relative to layer 1.
    int passes  = 1;         // Independent NR features; never replay one history in the same frame.
    int create_delay = 60;  // Feeder v0.11: 同设备 D3D12 + classic renodx 至少 300 帧
    int log_frames   = 3;
    int reset_every  = 1;    // 最小档 MV 全零 → 时域历史没意义, 每帧 Reset 让 DLAA 当单帧过程(NR 照插)
    int preset       = 0;    // = v3.2 的 NRPreset 0。模型: 0 默认 / 1 一号 / 2 二号 / 3 三号
    // ★默认 100%★ 我们这条路是差值合成:
    //   最终 = 原画 + (模型输出 − 模型输入) × blend
    // 满档时模型输入就是原画, 所以 blend=100 刚好等于「直接用模型输出」
    // —— 跟上一版(主插件)的做法完全一致。
    // 以前默认 85 是为了缩尺寸时压一压放大差值的伪影, 但默认已改回满档,
    // 再按住 15% 就是白白弱化效果(用户反馈“没有老版本惊艳”)。
    int blend   = 100;  // 差值叠加强度 %: 100=足量(等于模型输出), 50=只叠一半, 0=关掉效果只留原画
    // ---- 画质调校 (mode>=3 才用; 全部只在【建 feature 时】被模型读一次, 改了必须重建) ----
    // ★★ 默认值必须是 1.0, 不是 2.0 ★★
    //   2.0 是滑杆【上限】, 不是默认。参照 OptiScaler 的 Config.h:
    //     DlssNrIntensity{1.0} LocalStructure{1.0} LocalTone{1.0} SkinStructure{-1.0}
    //   我们以前四项全默认 2.0 = 顶格跑, 于是过冲: 评论区「人物拖影很厉害, 衣摆
    //   跑起来糊成一团」「边缘闪烁+光晕」「画质变差」「不如 3.2」全是这个。
    //   顶格还会把预设 1/2/3 的差别淹掉 —— 也就是「预设换 123 完全没区别」。
    // ══════════════════════════════════════════════════════════════════
    //  ★★出厂画质参数 = 逐项复刻 v3.2 包给 renodx 写的那份 [RenoDX.DLSS5]★★
    //  (2026-09-04 夜, 业主实测 3.2 画质更好后拍板"完全复刻")
    //  原文就在游戏目录的 ReShade.ini 里:
    //      NeuralUplift=1     NRColorStrength=1      NREnableUpscaling=0
    //      NRIntensity=2      NRLocalStructure=2     NRLocalTone=2
    //      NRSkinStructure=2  NRStyle=2              NRPreset=0
    //      NRPaperWhiteScale=3.16                    NRTransferStrength=1
    //  ★别再"照 OptiScaler 的 Config.h 默认值改回 1.0"★ —— 我今天就是这么干的,
    //  把四项顶格改成 1.0、风格 2 改成 0、皮肤 2 改成 -1、白点 3.16 当成 1.0,
    //  然后画质就不如 3.2 了。OptiScaler 的默认是它自己的, 不是 renodx 出货的。
    // ══════════════════════════════════════════════════════════════════
    float intensity        = 2.0f;   // = NRIntensity 2   // 整体强度      (0..2)  ★出厂默认 —— 业主 2026-09-04 定的那一套★
    float local_structure  = 2.0f;   // = NRLocalStructure 2   // 局部结构强度  (0..2)
    // ★层次(LocalTone) 出厂 1.0 —— 跟参考实现 Config.h 的 DlssNrLocalTone{1.0f} 一致★
    //   2026-09-04 夜我一度把它删掉并强制 0, 因为顶格 2.0 会暗场沸腾。那是错的:
    //   顶格才是病, 不是这一项本身。它是「局部明暗过渡」—— 让皮肤看起来像皮肤
    //   的正是它。按到 0 等于关掉模型四项主能力之一, 业主实测「人脸渲染太差」。
    float local_tone       = 2.0f;   // = NRLocalTone 2
    // ★-1 不是「关掉」, 是「跟随局部结构」= 模型自己的默认★ 以前钳在 0 以上,
    //   这个值我们根本表达不出来。
    //   2026-09-13: 声明处原本是 2.0f(最强档)。SharedLoad() 会改成 -1, 但业主截图里
    //   这一项显示 2.00, 说明有路径没走到那一步 —— 而"皮肤细节拉满"正是法令纹的来源。
    //   声明处就写 -1, 两条路一致。-1 不是"关掉", 是模型自己的默认(跟随局部结构)。
    float skin_structure   = -1.0f;  // = NRSkinStructure -1  // 皮肤质感强度  (-1..2; -1=跟随结构)
    // ★肤色提亮★ (0..1) 抬皮肤的中间调, 不动高光、不动真黑、不改色相。
    //   业主 2026-09-13「少了肤色提亮功能」—— 原来那套靠 CPU 人脸检测, 会掉设备被下线;
    //   现在用逐像素肤色权重在合成阶段做, 出厂给 0.35(原 portrait_strength 的默认值)。
    float skin_lift        = 0.35f;
    // ★白点★ 只对 HDR(浮点格式)有意义: 模型在显示参考的图上训练, 要先按白点归一。
    //   SDR 画面本来就是显示参考的, 这一项不参与。
    float whitepoint       = 3.16f;  // Fixed white scale; persisted range matches the controls ABI.
    int white_source       = exposurepolicy::Legacy; // Preserve existing config until explicitly changed.
    float white_trim       = 1.f;
    // ★高光护标★ 合成后的比值夹在 1/g..g, 一个标量作用于整个三元组(绩不逐通道)。
    //   参考实现默认 2.0。这是治屏闪的一环。
    float guard            = 3.0f;   // 高光护栏 (1..8)
    // ★颜色强度★ 0 = 画面保持游戏自己的色相, 只让光影带上模型的判断;
    //   1 = 模型的颜色也一起到达(默认)。抱怨「颜色不对/偏色」的往下拉。
    float colour           = 1.0f;   // (0..1)
    // ★锐化★ 对比度自适应(RCAS): 平坦区域几乎不动, 边缘才出力,
    //   而且结果死死夹在邻域范围内 —— 不会像普通锐化那样冲出白边。
    //   默认 0.3: 只在【处理精度低于 100%】时生效；满分辨率路径由着色器强制旁路。
    float natural_look     = 0.0f;   // Optional output-only natural lighting grade
    float sharpen          = 0.0f;   // 锐化 (0..1)
    // ★人脸加成★ 按肤色加权: 认出皮肤的像素, 在那里让模型那张图
    //   透过来更多、锐化也给更足。
    //   ★做不到「给人脸单独开更高分辨率」★—— NGX 的 feature 是按一个固定
    //   分辨率创建的, 而 feature 18 全进程只能有一个(两个同时存在 = 设备被移除)。
    float faceboost        = 0.0f;   // (0..1)
    int retired_effect     = 0; // ABI slot retained at zero; removed effect has no renderer
    int portrait_enabled   = 0;
    float portrait_strength = 0.35f;
    // ★出厂风格 = 0(原味)★ 参考实现 OptiScaler Config.h 的默认就是 DlssNrStyle{0}。
    //   Style 换的是【不同的模型】(社区做过逐字节校验: 0/1/2 输出不同, 3 是 2 的别名)。
    //   我们以前出厂写 2(电影) —— 差异视图上满屏大面积变色, 那是调色模型的行为,
    //   不是"加细节"该有的样子, 人脸首当其冲。
    //   2026-09-13: 上面这段注释从一开始就写着出厂该用 0, 代码却一直留着 2。
    //   正常路径上共用设置的默认层是 style 0, 所以玩家拿到的是 0; 但共用设置
    //   初始化失败时 SharedLoad 直接 return, 那条回退路上用的就是这里的值 ——
    //   等于"偶发地把调色模型当出厂值", 人脸最先遭殃。改成 0, 两条路一致。
    //   2026-09-13 改 1(自然): OptiScaler 自己 dlssnr/DlssNr_Menu.cpp 的 HelpMarker 写明
    //   0(Default/standard) 是最强的一档、会过饱和显得风格化, 而 1(Natural) "肤色和明暗
    //   平衡更接近游戏原本渲染的样子"。业主转述的抱怨正是叠层后人物发黑、法令纹重,
    //   所以出厂走 1。玩家在面板上一次点击就能改回去。
    int   style            = 1;      // Natural: 保住肤色那一档。
    int   auto_mask        = 1;      // 自动遮罩
    // ★合成方式★ 0 = 比值合成(我们自己那套, 默认) / 1 = 直接替换(RenoDX 4.7 那条路)
    //   业主反馈「3.2 版用 renodx 时人脸没问题」—— 这个开关就是拿来当场对照的。
    int   compose          = 1;      // ★出厂 = 直接替换★ RenoDX 4.7 就是这条路
    // ★编码曲线★ 0 = 柔和滚降(我们原来那条) / 1 = 可逆桥 Neutwo / 2 = 混合
    //   出厂 1: 「直接替换」这条路必须要有【精确逆变换】才成立, 柔和滚降没有逆。
    //   Neutwo 是 RenoDX 那条桥, 移植自 OptiScaler 的开源复刻。
    int   curve            = 1;

    // ══════════════════════════════════════════════════════════════════
    //  ★★完全复刻 RenoDX★★ (业主 2026-09-04 夜: 「复刻是最安全的」)
    //  打开时整条链按 renodx 4.7 走, 我们自己加的东西【一律不参与】:
    //    · 白点【固定】—— 不读游戏的曝光纹理。这是新查出来的算法差异:
    //      我们每帧去读游戏曝光, 游戏的自动曝光一动, 我们整条编码/解码的尺度
    //      就跟着动 —— 整层神经渲染在帧与帧之间"呼吸"。renodx 用死值 3.16。
    //    · 锐化 0(renodx 全文搜 sharp 零命中)
    //    · 时域稳定 关(上游实测两次都是死路)
    //    · 人脸加成 0(renodx 没有这东西)
    //    · 编码 = 可逆桥 Neutwo, 合成 = 直接替换 + 精确解码
    //  唯一学不来的: 100% 以下 renodx 根本没有对应做法(它永远 1:1)。
    // ══════════════════════════════════════════════════════════════════
    int   replica          = 1;
    float global_tone       = 1.0f;
    float diffuse_white     = 203.0f;
    int   colorbridge       = -1; // -1 auto, 0 SDR, 1 scene-linear BT.709, 2 PQ, 3 scRGB (80 nits/unit)
    int   requireguides     = 1; // injection requires depth and motion resources

    // ── 测量工具(抄自 OptiScaler) ───────────────────────────────────
    //  它的设计文档原话:「两样同时在变的时候, 你分不清是设置的效果还是场景的效果」。
    //  今天一整天所有的画质判断都是不同时刻、不同机位用眼睛比的 —— 一次都不算数。
    int   applymodel       = 1;      // 0 = 一切照跑但跳过模型(同帧对照基准) = DlssNrApplyModel
    int   holdframe        = 0;      // 1 = 冻结喂给模型的画面, 改设置时只有设置在变 = DlssNrHoldFrame
    int   comparepct       = 0;      // 0 = 关; 1..99 = 分屏对比, 左原画右处理后 = DlssNrCompare
    // ★残差放大滤波★ 0 = 双线性(出厂, 跟全生态一致) / 1 = Lanczos3(更锐, 会出噪点)
    int   resample         = 0;
    int   ui_correct       = 1;      // 界面校正
    int   hotkey           = 0x7A;   // 总开关热键：默认 F11（虚拟键码 122），单按；S37 起玩家可在面板换键。
    // ★就地插入★ 1=钩住游戏自己的 DLSS, 在那里做神经渲染(跟上一版同一个位置)
    // 0=老做法, 在交换链后缓冲上做(那张图已经色调映射+后处理+UI 了)
    // 只对【游戏自带 DLSS】的游戏有意义; 其他游戏没有更干净的输入可拿。
    // ★默认关★ 就地插入会把游戏卡死(审判之眼实测)。
    // 原因几乎可以定性: 我们在【NGX 求值的钩子里】又去调 NGX 建 feature,
    // 运行库内部拿着锁, 重入 = 死锁。要重新启用得先把「建」挪到钩子外面,
    // 钩子里只允许【求值】。没改好之前不许默认打开。
    int   inject           = 1;   // ★默认开★ 游戏自带 DLSS 就插进它的渲染分辨率(实测省 3 倍像素)
    // ★接入方式★ 1 = 自动(推荐): 先试就地插入, 抓不全游戏状态就自动让位给交换链;
    //   0 = 用户在面板上手动指定了哪条路, 不再自动切。
    int   autoroute        = 1;
    // ★用户在面板上点过的引擎选择★ -1 = 没点过(听安装器的)
    //   ★绝不能拿 carrier/inject 当判据★:
    //     carrier 就是 F12 总开关, 关掉会被自动保存 -> 「按了 F12 就退游戏」会被
    //       误判成「他选了主插件」;
    //     inject  在路线 D(游戏自带 DLSS, 主插件当引擎)的安装里【根本没写】,
    //       代码里默认 1 -> 会把全新安装误判成「他选了 033」, 然后把主插件的钩子
    //       关掉, 两边都不做。这条差点上线(2026-09-05 01:0x 自查发现)。
    int   engine           = -1;
    // ★压力自测★ 1 = 神经渲染跑起来之后自动跑一遍「测试」(它会连着换 5 个档位,
    //   也就是连着重建 5 次)。专门用来验「调参数会不会崩」, 出厂 0。
    int   autobench        = 0;
    int   injcaps          = 1;   // 就地插入用哪块参数块: 0=核心能力块 1=游戏自己那块
    int   injsl           = 1;   // Streamline 游戏(燕云/剑星那类)也做就地插入
    // ★把游戏的 DLSS Reset 透传给 feature 18 吗★ 0 = 不透传(出厂) / 1 = 透传(旧行为)
    //  (2026-09-06 燕云实测: 80 个采样里 reset=1 占 24 个 —— 30%。)
    //  游戏那个 Reset 说的是「我的【超分】时域历史失效了」(切镜头/传送/动态分辨率)。
    //  而 feature 18 是【另一个 feature】: 它跑在 DLSS 输出之上, 有自己独立的时域状态。
    //  拿前者当后者用, 结果就是每三帧就把 NR 的累积清空一次 —— 模型永远收敛不了,
    //  逐帧输出性格不同, 那就是业主看到的闪烁。燕云不可能 30% 的帧都在切镜头。
    //  不透传的代价: 真发生切镜头时 NR 会残留一两帧鬼影 —— 远比 30% 闪烁轻。
    //  ★留成开关是为了能 A/B★ 想验回旧行为: cfg 里写 injreset=1。
    int   injreset        = 0;
                                 // ★曾经默认关过, 那是误判★: 当时以为 Streamline 有毒,
                                 // 真凶其实是「feature 18 中途换手」。换手改掉之后燕云实测
                                 // ★就地插入成功★, ReShade 错误 0。真出问题还有 g_inject_dead 兜底。
    // ★游戏的 DLSS Output 到我们手里时是什么状态★
    //   D3D12 没有“查一个资源当前状态”这回事, 只能假设。DLSS 自己的
    //   compute 写完通常就停在 UNORDERED_ACCESS, 所以默认按它算 ——
    //   古墓/审判/燕云三个游戏实测都对。但假设错了 = 非法状态转换,
    //   轻则驱动报错重则丢设备。某个游戏一插就死就改这个。
    //   0=UAV(默认) 1=COPY_SOURCE 2=PIXEL_SR 3=NON_PIXEL_SR 4=RENDER_TARGET 5=COMMON
    // 帧生成开着时不碰后缓冲(★默认关★, 只当排障开关用)
    //   本来是照着「帧生成接管交换链, 我们再写就是抢同一块表面」这个想法加的,
    //   龙之信条2 的那组对照只否定了“关掉我们的后缓冲写入就能救”这个假设:
    //       干净游戏 ......................... 活 91 秒
    //       只放 ReShade(挂 d3d12 / 挂 dxgi) .. 崩 10.1 / 8.7 秒
    //       ReShade + 我们的插件(任何档位) .... 一样崩
    //   它没有试 REFramework 前置，不能据此说 ReShade 与 RE 引擎架构上无解；
    //   当前安装器会先放 REF 处理 Capcom 反修改层，DD2 仍按实验支持对待。
    //   fgsafe 本身没有正向证据，就不该默认开着 —— 开着等于在每一个开了 DLSS 帧生成的
    //   游戏上白白把我们自己关掉。真遇到疑似冲突, 再让用户写 fgsafe=1 排查。
    int   fgsafe          = 0;
    // ★插出来的帧不做 NR —— 已判死, 默认关★ (2026-09-05 业主实测「疯狂闪烁」)
    //   想法是对的: 帧生成时一半的 present 是插出来的, 在它们身上再跑一遍模型
    //   确实是重复劳动(鬼武者实测 画面37帧/秒 我们做了38次NR)。
    //   ★但交换链这条路做不了★ 它处理的是【已经要显示的那一帧】——
    //   跳过一帧 = 那一帧直接显示未处理的原图, 于是处理过/没处理过的画面
    //   以一半帧率交替出现, 就是肉眼可见的疯狂闪烁。
    //   ★就地插入那条路才有资格跳★: 它写的是游戏 DLSS 的输出, 在插帧【之前】,
    //   插出来的帧天然继承处理结果。那条路上根本不会碰到插帧, 也就不用跳。
    //   结论: 交换链路上这份开销是结构性的, 省不掉。代码留着当记录, 默认 0。
    int   skipgen         = 0;
    int   outstate        = 0;
    // 游戏曝光纹理到达 Stage 时的状态。-1=不猜/不额外做屏障(默认，沿用 NGX 的可读态)；
    // 0..5 与 outstate 同表。按游戏确认后写入，读前转到 NON_PIXEL_SR，读后原样还回。
    int   expstate        = -1;
    // 模型跑在哪个分辨率上: 1=整帧(默认, 跟参考实现一致, 最锐)
    //   0=钉在引导图(游戏渲染分辨率, 便宜但软)。嫌卡先拉「处理精度」, 不够再改这个。
    int   modelfull       = 1;
    // ★时域稳定(只在就地插入路生效)★ 0 = 关(出厂); 1..100 = 强度。
    //   治的是「怎么调都闪」那种细节沸腾: 模型每帧对一张微微移动的输入图重新
    //   做一次空间判断, 所以稳输入而不是稳输出(稳输出上游实测两次都失败)。
    int   stabilize       = 0;
    int   guides           = 1;      // 1=用 DLSS5_Feed.fx 提供的真深度/真运动矢量; 0=用全零占位
    int   mvscale          = 100;    // 运动矢量缩放 %: DLSS5_MV 本来就是像素单位, 100 = 原样不动
};
static Cfg cfg;
static int pending_engine_choice=-1;
static configstore::Debounce config_save;
#if defined(K033_BETA2_RESHADE_HOST)
static bool shared_ready=false;
static K033_Settings shared_grade{};
static K033_NrSettings shared_nr{};
static void SharedLoad(){
    cfg.enabled=0;cfg.hotkey=hotkey033::Load(); // S37: the player's own key, F11 unless chosen otherwise
    // Model-only skin strength follows structure; removed pre-NR controls stay off.
    cfg.skin_structure=-1;cfg.extra[0].skin=cfg.extra[1].skin=-1;
    if(k033beta2::Initial(shared_grade,shared_nr)!=K033_OK)return;
    k033beta2::ApplyGrade(cfg,shared_grade);k033beta2::ApplyNr(cfg,shared_nr);shared_ready=true;
}
static void SharedPoll(){
    if(!shared_ready)return;
    const auto grade=k033beta2::Grade(cfg);const auto nr=k033beta2::Nr(cfg);
    if(std::memcmp(&grade,&shared_grade,sizeof(grade))){
        if(k033beta2::OfferGrade(grade)==K033_OK)shared_grade=grade;
    }else {K033_Settings incoming{};if(k033beta2::ReceiveGrade(incoming)==K033_OK){k033beta2::ApplyGrade(cfg,incoming);shared_grade=incoming;}}
    if(std::memcmp(&nr,&shared_nr,sizeof(nr))){
        if(k033beta2::OfferNr(nr)==K033_OK)shared_nr=nr;
    }else {K033_NrSettings incoming{};if(k033beta2::ReceiveNr(incoming)==K033_OK){k033beta2::ApplyNr(cfg,incoming);shared_nr=incoming;}}
    k033beta2::SettingsStatus saved;
    if(k033beta2::Inspect(saved)==K033_OK){config_save.has_saved=saved.saved||saved.nr_saved;
        config_save.dirty=saved.queued!=saved.saved||saved.nr_queued!=saved.nr_saved;}
}
#endif
static reshade::api::color_space source_space = reshade::api::color_space::unknown;
static int EffectiveCurve() { return cfg.replica ? 1 : cfg.curve; }
static int EffectiveCompose() { return cfg.replica ? 1 : cfg.compose; }
static float EffectiveSharpen() { return cfg.replica ? 0.0f : cfg.sharpen; }
static float EffectiveWhite() { return exposurepolicy::FixedWhite(cfg.white_source,cfg.replica!=0,cfg.whitepoint,cfg.white_trim); }
// Output textures inside a game need their own transfer-function contract.
// A PQ swapchain does not prove that an upstream DLSS texture also stores PQ.
static int EncodeMode(DXGI_FORMAT f, bool backbuffer) {
    if (cfg.colorbridge >= 0) return cfg.colorbridge;
    if (backbuffer && source_space == reshade::api::color_space::scrgb) return 3;
    if (backbuffer && source_space == reshade::api::color_space::hdr10_pq) return 2;
    if (f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT ||
        f == DXGI_FORMAT_R11G11B10_FLOAT) return 1;
    return backbuffer && source_space == reshade::api::color_space::hdr10_pq ? 2 : 0;
}
static int ResolveMode(DXGI_FORMAT f, bool backbuffer) {
    const int mode = EncodeMode(f, backbuffer); return mode == 0 ? 1 : (mode >= 2 ? mode : 0);
}

static void load_cfg()
{
#if defined(K033_BETA2_RESHADE_HOST)
    SharedLoad();nrfeatures::Restrict(cfg);return;
#else
    nrfeatures::Restrict(cfg);
    const std::string path = game_dir() + "\\dlss5-033.cfg";
    FILE *f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return;
    nrlayers::Reader layerReader;
    bool hasPasswork3=false;
    char line[256];
    while (std::fgets(line, sizeof(line), f))
    {
        char *eq = std::strchr(line, '=');
        if (eq == nullptr) continue;
        *eq = 0;
        if(layerReader.Read(line,eq+1))continue;
        const int v = std::atoi(eq + 1);
        if      (!std::strcmp(line, "carrier"))      cfg.enabled = v;
        else if (!std::strcmp(line, "mode"))         cfg.mode = v;
        else if (!std::strcmp(line, "work"))         cfg.work = v;
        else if (!std::strcmp(line, "passwork")) cfg.passwork = (v>=50 && v<=100) ? v : 100;
        else if (!std::strcmp(line, "passwork3")) {cfg.passwork3 = (v>=50 && v<=100) ? v : 100;hasPasswork3=true;}
        else if (!std::strcmp(line, "passes"))       cfg.passes = nrfeatures::ClampPasses(v);
        else if (!std::strcmp(line, "create_delay")) cfg.create_delay = v;
        else if (!std::strcmp(line, "log_frames"))   cfg.log_frames = v;
        else if (!std::strcmp(line, "reset_every"))  cfg.reset_every = v;
        else if (!std::strcmp(line, "engine"))       cfg.engine = v;
        else if (!std::strcmp(line, "preset"))       cfg.preset = v;
        else if (!std::strcmp(line, "blend"))        cfg.blend = v;
        else if (!std::strcmp(line, "intensity"))    cfg.intensity = v / 100.0f;
        else if (!std::strcmp(line, "structure"))    cfg.local_structure = v / 100.0f;
        else if (!std::strcmp(line, "globaltone"))  cfg.global_tone = v / 100.0f;
        else if (!std::strcmp(line, "diffusewhite")) cfg.diffuse_white = static_cast<float>(v);
        else if (!std::strcmp(line, "colorbridge")) cfg.colorbridge = v;
        else if (!std::strcmp(line, "requireguides")) cfg.requireguides = v;
        else if (!std::strcmp(line, "tone"))        cfg.local_tone = v / 100.0f;
        else if (!std::strcmp(line, "skin"))         cfg.skin_structure = v / 100.0f;
        else if (!std::strcmp(line, "whitepoint"))   cfg.whitepoint = v / 100.0f;
        else if (!std::strcmp(line, "whitesource")) cfg.white_source = exposurepolicy::CheckedSource(v);
        else if (!std::strcmp(line, "whitetrim")) cfg.white_trim = exposurepolicy::Trim(v/100.f);
        else if (!std::strcmp(line, "guard"))        cfg.guard = v / 100.0f;
        else if (!std::strcmp(line, "colour"))       cfg.colour = v / 100.0f;
        else if (!std::strcmp(line, "naturallook"))cfg.natural_look=v/100.f;
        else if (!std::strcmp(line, "sharpen"))      cfg.sharpen = v / 100.0f;
        else if (!std::strcmp(line, "pregrade"))     cfg.pre.enabled=v!=0;
        else if (!std::strcmp(line, "preexposure"))  cfg.pre.exposure=v/100.f;
        else if (!std::strcmp(line, "precontrast"))  cfg.pre.contrast=v/100.f;
        else if (!std::strcmp(line, "presaturation"))cfg.pre.saturation=v/100.f;
        else if (!std::strcmp(line, "prewarmth"))    cfg.pre.warmth=v/100.f;
        else if (!std::strcmp(line, "pretint"))      cfg.pre.tint=v/100.f;
        else if (!std::strcmp(line, "prestyle"))cfg.pre.style=static_cast<uint32_t>(v);
        else if (!std::strcmp(line, "prestylestrength"))cfg.pre.styleStrength=v/100.f;
        else if (!std::strcmp(line, "prehighlights"))cfg.pre.highlights=v/100.f;
        else if (!std::strcmp(line, "mas"))          cfg.mas = v != 0;
        else if (!std::strcmp(line, "masstill"))     cfg.mas_still = v / 100.0f;
        else if (!std::strcmp(line, "masmoving"))    cfg.mas_moving = v / 100.0f;
        else if (!std::strcmp(line, "masthreshold")) cfg.mas_threshold = v / 100.0f;
        // 旧 cfg 里可能存着 4.6/2.6 那批超过 2 的值(老版本的默认),
        // 现在上限是 2, 读进来得钳住, 否则面板显示和实际值对不上。
        else if (!std::strcmp(line, "style"))        cfg.style = v;
        else if (!std::strcmp(line, "automask"))     cfg.auto_mask = v;
        else if (!std::strcmp(line, "compose"))      cfg.compose = v;
        else if (!std::strcmp(line, "resample"))     cfg.resample = v;
        else if (!std::strcmp(line, "curve"))        cfg.curve = v;
        else if (!std::strcmp(line, "replica"))      cfg.replica = v;
        else if (!std::strcmp(line, "applymodel"))   cfg.applymodel = v;
        else if (!std::strcmp(line, "holdframe"))    cfg.holdframe = v;
        else if (!std::strcmp(line, "comparepct"))   cfg.comparepct = v;
        else if (!std::strcmp(line, "uicorrect"))    cfg.ui_correct = v;
        else if (!std::strcmp(line, "hotkey"))       cfg.hotkey = v;
        else if (!std::strcmp(line, "inject"))       cfg.inject = v;
        else if (!std::strcmp(line, "autoroute"))    cfg.autoroute = v;
        else if (!std::strcmp(line, "autobench"))    cfg.autobench = v;
        else if (!std::strcmp(line, "injcaps"))      cfg.injcaps = v;
        else if (!std::strcmp(line, "injsl"))        cfg.injsl = v;
        else if (!std::strcmp(line, "injreset"))     cfg.injreset = v;
        else if (!std::strcmp(line, "fgsafe"))       cfg.fgsafe = v;
        else if (!std::strcmp(line, "skipgen"))      cfg.skipgen = v;
        else if (!std::strcmp(line, "outstate"))     cfg.outstate = v;
        else if (!std::strcmp(line, "expstate"))     cfg.expstate = v;
        else if (!std::strcmp(line, "modelfull"))    cfg.modelfull = v;
        else if (!std::strcmp(line, "stabilize"))    cfg.stabilize = v;
        else if (!std::strcmp(line, "guides"))       cfg.guides = v;
        else if (!std::strcmp(line, "mvscale"))      cfg.mvscale = v;
        else if (!std::strcmp(line, "restorestate")) staterestore::cfg_enabled = v;
        else if (!std::strcmp(line, "gputime"))      gputime::cfg_enabled = v;       // 0 = 不打任何时间戳   // 0 关 / 1 还堆+根签名+PSO / 2 再重放根参数
    }
    std::fclose(f);
    layerReader.Finish(cfg);
    if(!hasPasswork3)cfg.passwork3=cfg.passwork;
    auto clamp2 = [](float &f) { if (f < 0.0f) f = 0.0f; else if (f > 2.0f) f = 2.0f; };
    clamp2(cfg.intensity); clamp2(cfg.local_structure); clamp2(cfg.local_tone);
    // 皮肤那一项允许 -1(跟随局部结构, 模型自己的默认), 所以下限不是 0
    if (cfg.skin_structure < -1.0f) cfg.skin_structure = -1.0f;
    else if (cfg.skin_structure > 2.0f) cfg.skin_structure = 2.0f;
    if (cfg.global_tone < 0) cfg.global_tone = 0; else if (cfg.global_tone > 2) cfg.global_tone = 2;
    if (cfg.diffuse_white < 1) cfg.diffuse_white = 1; else if (cfg.diffuse_white > 10000) cfg.diffuse_white = 10000;
    if (cfg.colorbridge < -1 || cfg.colorbridge > 3) cfg.colorbridge = -1;
    cfg.whitepoint=exposurepolicy::White(cfg.whitepoint);
    if (cfg.guard      < 1.0f)  cfg.guard      = 1.0f;  else if (cfg.guard      > 8.0f) cfg.guard      = 8.0f;
    if (cfg.colour     < 0.0f)  cfg.colour     = 0.0f;  else if (cfg.colour     > 1.0f) cfg.colour     = 1.0f;
    if (cfg.sharpen    < 0.0f)  cfg.sharpen    = 0.0f;  else if (cfg.sharpen    > 1.0f) cfg.sharpen    = 1.0f;
    cfg.natural_look=std::isfinite(cfg.natural_look)?std::clamp(cfg.natural_look,0.f,1.f):0.f;
    cfg.pre=pregrade::Checked(&cfg.pre);
    cfg.mas_still=std::clamp(cfg.mas_still,0.0f,1.0f);
    cfg.mas_moving=std::clamp(cfg.mas_moving,0.0f,1.0f);
    cfg.mas_threshold=std::clamp(cfg.mas_threshold,0.01f,128.0f);
    nrfeatures::Restrict(cfg); // Removed input-skin controls never revive from an old config.
    // ★处理精度范围 25..200★ 跟参考实现的 DlssNrWorkingScale{0.25..2.0} 对齐。
    //   小于 100 = 模型跑得比画面小, 便宜; 大于 100 = 【超采样】, 模型在一张比画面
    //   还大的图上去噪, 出来再缩回显示尺寸 —— 参考实现原话: 这才是"超采样换来更少
    //   的噪点而不是更多"。开销按面积走, 200% 是 100% 的四倍, 心里有数再开。
    if (cfg.work < 25 || cfg.work > 200) cfg.work = 70;   // 同一个概念只留一个数，见上面的默认值
    if (cfg.stabilize < 0) cfg.stabilize = 0; else if (cfg.stabilize > 100) cfg.stabilize = 100;
    if (cfg.blend < 0 || cfg.blend > 200) cfg.blend = 100;
    if (cfg.create_delay < 60) cfg.create_delay = 60;
#endif
}

// 存回 dlss5-033.cfg。只写我们管的键, 其它行(nrscale 之类)原样保留。
// ★只改文件, 不碰内存★ (2026-09-04 夜: 面板上切引擎当场卡死)
//   换引擎必须【下次启动】才生效 —— 当场把 enabled/inject 打开, 我们的引擎
//   立刻起来跟还在跑的 RenoDX 抢同一个 feature 18, 当场 0xC0000005。
//   所以这里只把三行写进 cfg 文件, 本局一切照旧。
static void write_engine_choice(bool own)
{
#if defined(K033_BETA2_RESHADE_HOST)
    (void)own;return; // One 033 engine; no legacy engine-choice file writes.
#else
    const std::string path = game_dir() + "\\dlss5-033.cfg";
    const char* keys[]={"carrier","inject","autoroute","engine"};
    if(!configstore::Update(path,keys,4,[&](FILE* f) {
        std::fprintf(f,"engine=%d\ncarrier=%d\ninject=%d\nautoroute=%d\n",
                     own?1:0,own?1:0,own?1:0,own?1:0);
    })) {Log("[carrier] engine choice save failed; original preserved");return;}
    pending_engine_choice=own?1:0;
    cfg.engine = own ? 1 : 0;
    Log("[carrier] 引擎选择已写入(下次启动生效): %s", own ? "033 自研" : "主插件 RenoDX");
#endif
}

static bool save_cfg()
{
#if defined(K033_BETA2_RESHADE_HOST)
    SharedPoll();return shared_ready; // Enqueued is not reported as disk persistence.
#else
    const std::string path = game_dir() + "\\dlss5-033.cfg";
    // Preserve foreign keys, comments and blank lines byte for byte.
    static const char *mine[] = { "nrlayersversion","nrlayer2.style","nrlayer2.preset","nrlayer2.intensity","nrlayer2.structure","nrlayer2.tone","nrlayer2.skin","nrlayer2.globaltone","nrlayer2.automask","nrlayer2.uicorrect","nrlayer3.style","nrlayer3.preset","nrlayer3.intensity","nrlayer3.structure","nrlayer3.tone","nrlayer3.skin","nrlayer3.globaltone","nrlayer3.automask","nrlayer3.uicorrect","carrier","mode","work","passes","passwork","passwork3","modelfull","create_delay","log_frames","reset_every",
                                  "preset","blend","intensity","structure","tone","skin","style",
                                  "autoroute",
                                  "stabilize","globaltone","diffusewhite","colorbridge","requireguides",
                                  "compose","curve","replica","resample","applymodel","holdframe","comparepct",
                                  "whitepoint","whitesource","whitetrim","guard","colour","sharpen","naturallook","postbeauty","faceboost","portrait","portraitstrength","mas","masstill","masmoving","masthreshold",
                                  "automask","uicorrect","hotkey","guides","mvscale","pregrade","preexposure","precontrast","presaturation","prewarmth","pretint","prehighlights","prestyle","prestylestrength" };
    const bool saved=configstore::Update(path,mine,sizeof(mine)/sizeof(mine[0]),[&](FILE* f) {
    nrlayers::Save(f,cfg);
    std::fprintf(f, "carrier=%d\nmode=%d\nwork=%d\nblend=%d\n",
                 pending_engine_choice>=0 ? pending_engine_choice : cfg.enabled, cfg.mode, cfg.work, cfg.blend);
    std::fprintf(f, "passes=%d\npasswork=%d\npasswork3=%d\nmodelfull=%d\n",cfg.passes,cfg.passwork,cfg.passwork3,cfg.modelfull);
    std::fprintf(f, "preset=%d\nstyle=%d\nautomask=%d\nuicorrect=%d\n",
                 cfg.preset, cfg.style, cfg.auto_mask, cfg.ui_correct);
    std::fprintf(f, "compose=%d\n", cfg.compose);
    std::fprintf(f, "curve=%d\n", cfg.curve);
    std::fprintf(f, "replica=%d\n", cfg.replica);
    // Both keys are removed from the preserved lines above; write their actual
    // values so saving cannot silently enable fallback or disable stabilization.
    std::fprintf(f, "autoroute=%d\nstabilize=%d\n", pending_engine_choice>=0 ? pending_engine_choice : cfg.autoroute, cfg.stabilize);
    std::fprintf(f, "globaltone=%d\ndiffusewhite=%d\ncolorbridge=%d\nrequireguides=%d\n",
                 static_cast<int>(cfg.global_tone * 100 + 0.5f), static_cast<int>(cfg.diffuse_white + 0.5f),
                 cfg.colorbridge, cfg.requireguides);
    std::fprintf(f, "resample=%d\napplymodel=%d\nholdframe=%d\ncomparepct=%d\n",
                 cfg.resample, cfg.applymodel, cfg.holdframe, cfg.comparepct);
    // ★四舍五入必须对负数也成立★ 皮肤默认 -1.0, 用 +0.5f 会存成 -99
    auto pct = [](float f) { return (int)(f >= 0.0f ? f * 100.0f + 0.5f : f * 100.0f - 0.5f); };
    std::fprintf(f, "intensity=%d\nstructure=%d\ntone=%d\nskin=%d\n",
                 pct(cfg.intensity), pct(cfg.local_structure),
                 pct(cfg.local_tone), pct(cfg.skin_structure));
    std::fprintf(f, "whitepoint=%d\nguard=%d\ncolour=%d\nsharpen=%d\n", pct(cfg.whitepoint), pct(cfg.guard), pct(cfg.colour), pct(cfg.sharpen));
    std::fprintf(f,"whitesource=%d\nwhitetrim=%d\n",cfg.white_source,pct(cfg.white_trim));
    std::fprintf(f,"pregrade=%u\npreexposure=%d\nprecontrast=%d\npresaturation=%d\nprewarmth=%d\npretint=%d\nprehighlights=%d\n",
                 cfg.pre.enabled,pct(cfg.pre.exposure),pct(cfg.pre.contrast),pct(cfg.pre.saturation),pct(cfg.pre.warmth),pct(cfg.pre.tint),pct(cfg.pre.highlights));
    std::fprintf(f,"naturallook=%d\n",pct(cfg.natural_look));
    std::fprintf(f,"prestyle=%u\nprestylestrength=%d\n",cfg.pre.style,pct(cfg.pre.styleStrength));
    std::fprintf(f,"mas=%d\nmasstill=%d\nmasmoving=%d\nmasthreshold=%d\n",cfg.mas,pct(cfg.mas_still),pct(cfg.mas_moving),pct(cfg.mas_threshold));
    std::fprintf(f, "hotkey=%d\nguides=%d\nmvscale=%d\ncreate_delay=%d\nlog_frames=%d\nreset_every=%d\n",
                 cfg.hotkey, cfg.guides, cfg.mvscale, cfg.create_delay, cfg.log_frames, cfg.reset_every);
    });
    Log(saved ? "[carrier] 设置已保存" : "[carrier] 保存失败: 保留原文件并重试");
    return saved;
#endif
}

// ------------------------------------------------------------ 状态
static const int kFrames = 3;   // Feeder [632]: 三槽 allocator 环

struct State
{
    bool  session_ready = false;
    bool  disabled      = false;
    bool  dying         = false;   // 设备销毁/进程退出后置位: 不再碰 NGX (Feeder 168)
    std::string disabled_why;
    ID3D12Device       *dev   = nullptr;   // 游戏的设备(不持有)
    ID3D12CommandQueue *queue = nullptr;   // 游戏的队列(AddRef 持有)
    command_queue      *rs_queue = nullptr;
    NVSDK_NGX_Parameter *params = nullptr;
    NVSDK_NGX_Handle    *feature = nullptr;   // mode=2 的 DLAA 契约
    void                *nr_feat = nullptr;   // mode=3 的 feature 18 (转发器给的句柄)

    ID3D12CommandAllocator    *alloc[kFrames] = {};
    UINT64                     alloc_fence[kFrames] = {};
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence               *fence = nullptr;
    HANDLE                     fence_event = nullptr;
    UINT64                     fence_value = 0;
    int                        slot = 0;

    scale::Blitter blit;

    // 私有纹理
    ID3D12Resource *capture = nullptr;  // 全尺寸, backbuffer 的可采样副本; 也是回写前的落点 (SR/UAV 切换)
    ID3D12Resource *resolved = nullptr; // 全尺寸, 差值合成的落点 (mode>=3); 常驻 copy_source
    ID3D12Resource *color   = nullptr;  // 小, NGX 输入, 常驻 SR
    ID3D12Resource *output  = nullptr;  // 小, NGX 输出, 常驻 UAV
    resource        mv      = {};       // 小, 全零 R16G16_FLOAT (ReShade 建, 带初值)
    resource        depth   = {};       // 小, 全零 R32_FLOAT   (ReShade 建, 带初值)
    UINT  w = 0, h = 0;                 // 契约尺寸(小)
    UINT  bb_w = 0, bb_h = 0;
    DXGI_FORMAT bb_fmt = DXGI_FORMAT_UNKNOWN, color_fmt = DXGI_FORMAT_UNKNOWN, model_fmt = DXGI_FORMAT_UNKNOWN;
    bool  hdr = false;
    bool  depth_inverted = true;

    bool  frame_ready = false;
    bool  need_reset  = true;
    unsigned pre_signature=0;
    int   built_work = 0;               // 建资源时用的 work%; 面板改了就重建
    unsigned built_tune = 0;            // 建 feature 时的调校指纹; 变了就重建
    bool  out_in_copy = false;          // 100% 短路时 output 停在 copy_source, 下一帧要转回 UAV
    int   create_grace = 0;
    int   consecutive_fails = 0;
    UINT64 frames_done = 0;
    NVSDK_NGX_Result last_eval = NVSDK_NGX_Result_Success;
    double last_ms = 0.0;
    volatile LONG busy = 0;             // 每帧路径串行化 (Smooth Motion 会一帧多次 Present)
};
static State g;

template <typename T> static void SafeRelease(T *&p) { if (p) { p->Release(); p = nullptr; } }

static void Disable(const char *why)
{
    if (g.disabled) return;
    g.disabled = true;
    g.disabled_why = why;
    Log("[carrier] 停止: %s (游戏照常渲染)", why);
}
static void Fail(const char *what)
{
    Log("[carrier] 失败: %s", what);
    if (++g.consecutive_fails >= 3) Disable("连续三次失败");
}

// 调校指纹。模型只在建 feature 时读一次调校参数, 之后再设全被忽略 ——
// 所以面板一动就得重建, 否则控件看起来"没反应"(OptiScaler 也栽过这个, 见
// DlssNr_Dx12.cpp 里 TuningMatchesFeature 那段注释)。
// 整份设置的指纹 —— 面板拿它判断"用户是不是改了东西", 改了就自动落盘。
// (业主要求只留一个按钮, 保存这件事不该再让人操心)
static unsigned CfgSig()
{
    unsigned h = 2166136261u;
    auto mix = [&h](unsigned v) { h = (h ^ v) * 16777619u; };
    auto mixf = [&mix](float f) { mix(static_cast<unsigned>(static_cast<int>(f * 1000.0f))); };
    mix(static_cast<unsigned>(cfg.enabled));   mix(static_cast<unsigned>(cfg.mode));
    mix(static_cast<unsigned>(cfg.work));      mix(static_cast<unsigned>(cfg.modelfull));
    mix(static_cast<unsigned>(cfg.passes));
    mix(static_cast<unsigned>(cfg.passwork));
    mix(static_cast<unsigned>(cfg.passwork3));
    mix(static_cast<unsigned>(cfg.inject));    mix(static_cast<unsigned>(cfg.autoroute));
    mix(static_cast<unsigned>(cfg.replica));   mix(static_cast<unsigned>(cfg.curve));
    mix(static_cast<unsigned>(cfg.compose));   mix(static_cast<unsigned>(cfg.resample));
    mix(static_cast<unsigned>(cfg.style));     mix(static_cast<unsigned>(cfg.preset));
    mix(static_cast<unsigned>(cfg.auto_mask)); mix(static_cast<unsigned>(cfg.ui_correct));
    mix(static_cast<unsigned>(cfg.blend));     mix(static_cast<unsigned>(cfg.applymodel));
    mix(static_cast<unsigned>(cfg.holdframe)); mix(static_cast<unsigned>(cfg.comparepct));
    mixf(cfg.intensity); mixf(cfg.local_structure); mixf(cfg.local_tone);
    mixf(cfg.skin_structure); mixf(cfg.colour); mixf(cfg.whitepoint);
    mix(cfg.white_source);mixf(cfg.white_trim);
    mixf(cfg.guard); mixf(cfg.sharpen);
    mixf(cfg.global_tone); mixf(cfg.diffuse_white); mix(static_cast<unsigned>(cfg.colorbridge));
    mix(static_cast<unsigned>(cfg.requireguides));
    mix(pregrade::Signature(cfg.pre));
    mix(cfg.mas); mixf(cfg.mas_still); mixf(cfg.mas_moving); mixf(cfg.mas_threshold);
    mix(cfg.stabilize); mix(cfg.guides); mix(cfg.mvscale); mix(cfg.hotkey);
    mix(cfg.create_delay); mix(cfg.log_frames); mix(cfg.reset_every); mixf(cfg.faceboost);
    mix(cfg.portrait_enabled);mixf(cfg.portrait_strength);
    mix(nrlayers::Signature(cfg,true));
    return h;
}

// Pumped by present even when the overlay is closed; normal shutdown flushes pending edits.
static void PollConfig(bool force=false) {
#if defined(K033_BETA2_RESHADE_HOST)
    (void)force;SharedPoll();return;
#else
    const auto now=GetTickCount64();
    if(config_save.Due(CfgSig(),now,force)) config_save.Completed(save_cfg(),now);
#endif
}

static unsigned TuneSig(bool includePasses = true)
{
    unsigned h=nrlayers::Signature(cfg);
    if(includePasses){h=(h^unsigned(cfg.passes))*16777619u;h=(h^unsigned(cfg.passwork))*16777619u;h=(h^unsigned(cfg.passwork3))*16777619u;}
    return h;
}

// ------------------------------------------------------------ 格式 (Feeder [748])
static DXGI_FORMAT TypedColorFormat(DXGI_FORMAT f)
{
    if (f == DXGI_FORMAT_R32G32B32A32_FLOAT || f == DXGI_FORMAT_R32G32B32A32_TYPELESS)
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
static bool IsHdrFormat(DXGI_FORMAT t) { return t == DXGI_FORMAT_R16G16B16A16_FLOAT || t == DXGI_FORMAT_R32G32B32A32_FLOAT || t == DXGI_FORMAT_R11G11B10_FLOAT; }

// ------------------------------------------------------------ 命令列表环 (Feeder [898-983])
// fence 等待 (Feeder v0.11 [1416-1464] 的模式): 250ms 切片, 片间查设备是否被移除, 等完再核对完成值
static bool WaitFence(UINT64 target, DWORD total_ms, const char *what)
{
    if (g.fence->GetCompletedValue() >= target) return true;
    ResetEvent(g.fence_event);
    g.fence->SetEventOnCompletion(target, g.fence_event);
    DWORD waited = 0;
    while (waited < total_ms)
    {
        if (WaitForSingleObject(g.fence_event, 250) == WAIT_OBJECT_0) break;
        waited += 250;
        if (FAILED(g.dev->GetDeviceRemovedReason()))
        {
            Log("[carrier] 设备已被移除 (0x%08X), 等 %s 时发现", g.dev->GetDeviceRemovedReason(), what);
            Disable("显卡设备丢失");
            return false;
        }
    }
    if (g.fence->GetCompletedValue() < target)
    {
        // ★这里不 Disable★ —— 等不到不一定是「GPU 停了」, 也可能只是帧生成让它
        //   暂时落后。由调用方决定是「跳过这一帧」还是「真的停」。
        Log("[carrier] GPU %lu ms 内没完成: %s", total_ms, what);
        return false;
    }
    return true;
}

static void PumpPark();          // 定义在下面的停车场那一段
static int  g_retire_miss = 0;   // allocator 槽连续等不到的次数

static bool BeginCommands()
{
    PumpPark();     // 停车场每帧扫一次: fence 过了才真放
    const UINT64 retire = g.alloc_fence[g.slot];
    // ★超时不再一棍子打死★ —— 开着帧生成的游戏 GPU 会周期性落后好几帧,
    //   老版本 2 秒等不到就 Disable("GPU 停止完成工作"), 等于因为一次抖动
    //   把整局的神经渲染永久关掉(龙之信条 2 实测)。改成: 跳过这一帧, 连续
    //   八次都等不到才认为真的停了。
    if (retire != 0 && !WaitFence(retire, 500, "allocator 槽退休"))
    {
        if (++g_retire_miss < 8) return false;      // 只是这一帧不做
        Disable("GPU 连续 8 帧没能完成工作");
        return false;
    }
    g_retire_miss = 0;
    if (g.alloc[g.slot] == nullptr) return false;
    if (FAILED(g.alloc[g.slot]->Reset())) return false;
    return SUCCEEDED(g.list->Reset(g.alloc[g.slot], nullptr));
}
static UINT64 EndCommands()
{
    g.list->Close();
    ID3D12CommandList *lists[] = { g.list };
    g.queue->ExecuteCommandLists(1, lists);
    gputime::AfterSubmit(g.list, g.queue);
    const UINT64 v = ++g.fence_value;
    g.queue->Signal(g.fence, v);
    g.alloc_fence[g.slot] = v;
    g.slot = (g.slot + 1) % kFrames;
    return v;
}
static void CloseListGuarded() { __try { g.list->Close(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
// NGX 在记录时炸了: 这条列表【绝不能】送到 GPU。关掉、扔掉、换一条新的。
static void AbortCommands()
{
    if (g.list == nullptr) return;
    CloseListGuarded();
    SafeRelease(g.list);
    if (g.alloc[g.slot] != nullptr && g.dev != nullptr &&
        SUCCEEDED(g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.alloc[g.slot], nullptr,
                                           __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&g.list))))
        g.list->Close();
    else
        Log("[carrier] 换不出新的命令列表");
}
// 返回 true 才代表 GPU 真的干完了。★超时绝不能假装干完★ ——
//   老版本超时后照样把 alloc_fence 清零、照样往下拆资源, 那就是「在 GPU 底下抽桌布」:
//   龙之信条 2(开着 DLSS 帧生成, GPU 落后好几帧)实测 5 秒排不空 → 强拆 →
//   Fatal D3D error (24, DXGI_ERROR_DEVICE_REMOVED) 当场崩。
static bool DrainGpu()
{
    if (g.queue == nullptr || g.fence == nullptr) return true;
    const UINT64 v = ++g.fence_value;
    g.queue->Signal(g.fence, v);
    bool done = true;
    if (g.fence->GetCompletedValue() < v && g.fence_event != nullptr)
    {
        g.fence->SetEventOnCompletion(v, g.fence_event);
        // ★只等一小会儿★ —— 反正等不到就停车(下面), 没必要死等。
        //   等太久 = 调个参数游戏就卡住不动几秒, 业主实测「调一下就会卡」正是这个。
        if (WaitForSingleObject(g.fence_event, 300) != WAIT_OBJECT_0)
        {
            Log("[carrier] 排空队列超时 —— 不硬拆, 旧资源转入停车场延迟释放");
            done = false;
        }
    }
    if (done) { for (int i = 0; i < kFrames; ++i) g.alloc_fence[i] = 0; }
    return done;
}

// ------------------------------------------------------------ 停车场
//  规矩(社区拿设备挂死换来的): 绝不在 GPU 底下释放。排空不掉就把旧件挂在这儿,
//  等 fence 真过了再放; 顺带留一个帧数下限, 免得 fence 永远不动时无限占着显存。
struct Parked
{
    ID3D12Resource   *res   = nullptr;
    NVSDK_NGX_Handle *feat  = nullptr;
    void             *nrf   = nullptr;
    UINT64            fence = 0;
    int               ttl   = 240;      // 兜底: 240 帧后无论如何放掉
};
static std::vector<Parked> g_park;

static void ParkRes(ID3D12Resource *&r, UINT64 f)
{ if (r != nullptr) { Parked p; p.res = r; p.fence = f; g_park.push_back(p); r = nullptr; } }

static void PumpPark()
{
    if (g_park.empty() || g.fence == nullptr) return;
    const UINT64 done = g.fence->GetCompletedValue();
    for (size_t i = 0; i < g_park.size();)
    {
        Parked &p = g_park[i];
        const bool passed = (p.fence == 0) || (done >= p.fence);
        if (!passed && --p.ttl > 0) { ++i; continue; }
        if (p.res  != nullptr) p.res->Release();
        if (p.feat != nullptr && !g.dying) { __try { g_ngx.release(p.feat); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (p.nrf  != nullptr) nrfwd::release(p.nrf, g.dying);
        g_park.erase(g_park.begin() + static_cast<long>(i));
    }
}

// ------------------------------------------------------------ SEH 包裹的 NGX 调用 (函数体内不放 C++ 对象)
static NVSDK_NGX_Result SafeCreate(NVSDK_NGX_Handle **out, DWORD *code)
{
    *code = 0;
    __try { return g_ngx.create(g.list, NVSDK_NGX_Feature_SuperSampling, g.params, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7FFFFFFF); }
}
static NVSDK_NGX_Result SafeEvaluate(DWORD *code)
{
    *code = 0;
    __try { return g_ngx.evaluate(g.list, g.feature, g.params, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *code = GetExceptionCode(); return static_cast<NVSDK_NGX_Result>(0x7FFFFFFF); }
}
static void SafeReleaseFeature(NVSDK_NGX_Handle *&h)
{
    if (h == nullptr) return;
    if (!g.dying) { __try { g_ngx.release(h); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    h = nullptr;
}

// ------------------------------------------------------------ 资源
static bool MakeTex(ID3D12Resource **out, const char *name, UINT w, UINT h, DXGI_FORMAT fmt, bool uav, D3D12_RESOURCE_STATES initial)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const HRESULT hr = g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr,
                                                      __uuidof(ID3D12Resource), reinterpret_cast<void **>(out));
    if (FAILED(hr)) { Log("[carrier] %s: CreateCommittedResource 0x%08X", name, hr); return false; }
    Log("[carrier] %-7s %ux%u fmt=%d%s", name, w, h, fmt, uav ? " (UAV)" : "");
    return true;
}
// 用 ReShade 建带初值的纹理(它替我们做上传); 返回的 handle 就是原生 ID3D12Resource*
static bool MakeInitTex(device *dev, resource *out, const char *name, UINT w, UINT h, format fmt, uint32_t bpp)
{
    std::string zeros(static_cast<size_t>(w) * h * bpp, '\0');
    subresource_data init = {};
    init.data = zeros.data();
    init.row_pitch = w * bpp;
    init.slice_pitch = init.row_pitch * h;
    const resource_desc desc(w, h, 1, 1, fmt, 1, memory_heap::default_, resource_usage::shader_resource | resource_usage::copy_dest);
    if (!dev->create_resource(desc, &init, resource_usage::shader_resource, out))
    { Log("[carrier] %s: ReShade create_resource 失败", name); return false; }
    Log("[carrier] %-7s %ux%u (全零, ReShade 建)", name, w, h);
    return true;
}
static void ReleaseFrameResources(device *dev)
{
    const bool drained = DrainGpu();
    if (!drained)
    {
        // GPU 还在用这些东西。挂进停车场, 等 fence 过了 PumpPark 再放。
        const UINT64 f = g.fence_value;
        if (g.feature != nullptr) { Parked p; p.feat = g.feature; p.fence = f; g_park.push_back(p); g.feature = nullptr; }
        if (g.nr_feat != nullptr) { Parked p; p.nrf  = g.nr_feat; p.fence = f; g_park.push_back(p); g.nr_feat = nullptr; }
        ParkRes(g.capture, f); ParkRes(g.resolved, f); ParkRes(g.color, f); ParkRes(g.output, f);
        Log("[carrier] 已停车 %zu 件, 等 GPU 用完再放", g_park.size());
    }
    else
    {
        SafeReleaseFeature(g.feature);
        nrfwd::release(g.nr_feat, g.dying);
        SafeRelease(g.capture);
        SafeRelease(g.resolved);
        SafeRelease(g.color);
        SafeRelease(g.output);
    }
    if (dev != nullptr)
    {
        if (g.mv.handle)    { dev->destroy_resource(g.mv);    g.mv = {}; }
        if (g.depth.handle) { dev->destroy_resource(g.depth); g.depth = {}; }
    }
    g.frame_ready = false;
}

// ★★ feature 18 只能有一个 ★★
//   同一个 NGX 能力块上同时存在两个 feature 18 = 第二个创建时运行库直接
//   访问违例(0xC0000005)。实测(审判之眼 21:47): 交换链先建成 @5120x2160,
//   16 毫秒后就地插入再建 → 当场崩。所以两条路必须互斥:
//     g_inject_hooked  就地插入已挂上游戏的 DLSS → 交换链让位并交出 feature
//     g_inject_dead    就地插入试过但没成 → 交换链接回来, 别让用户一点效果没有
// ★这张画面是不是已经「显示参考」了★
//   浮点格式 = HDR 线性, 值能远超 1 → 要编码成 sRGB 代理图再喂模型
//   UNORM     = 已经色调映射过的成品图 → 再编码一次是二次压缩, 纯伤害
//   参考实现读游戏 DLSS 的创建标志; 我们按格式判, 结论一致且不依赖参数块。
// cfg.outstate → 真正的 D3D12 状态位
static D3D12_RESOURCE_STATES OutputArrivalState()
{
    switch (cfg.outstate)
    {
    case 1:  return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case 2:  return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case 3:  return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case 4:  return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case 5:  return D3D12_RESOURCE_STATE_COMMON;
    default: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
}

// 曝光贴图没有可查询的“当前状态”。没有逐游戏证据时返回 false，不凭空发一条
// StateBefore 可能错误的屏障；配置了 expstate 才按表过渡并在读完后还回。
static bool ExposureArrivalState(D3D12_RESOURCE_STATES &state)
{
    switch (cfg.expstate)
    {
    case 0: state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; return true;
    case 1: state = D3D12_RESOURCE_STATE_COPY_SOURCE; return true;
    case 2: state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; return true;
    case 3: state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; return true;
    case 4: state = D3D12_RESOURCE_STATE_RENDER_TARGET; return true;
    case 5: state = D3D12_RESOURCE_STATE_COMMON; return true;
    default: state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; return false;
    }
}

static int PassthroughFor(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return 0;
    default:
        return 1;
    }
}

static bool g_inject_hooked = false;
static bool g_inject_dead   = false;
// “开场一段时间还没看见游戏 DLSS”只是超时回退；但交换链一旦建成 feature，
// 本局不能再中途换手（旧实测会移除设备）。因此超时必须按墙钟算、给慢启动游戏
// 足够时间，而不能拿高帧率标题画面的 240 帧当成 2-4 秒。
static bool g_inject_timed_out = false;

// ★让位的条件不能是「钩子装上了」★
//   钩子只要驱动在就装得上, 跟这个游戏到底用不用 DLSS 没关系。
//   审判之眼实测: 它标题画面根本不调 DLSS, 我们却把 feature 18 让出去了,
//   结果两边都不做 —— 用户一点效果都没有, 比不改还糟。
//   真正的信号是【游戏自己的 DLSS 求值被拦到了】: 拦到了才说明有宿主可插。
//   再加一道宽限: 让位之后若干帧内插入还没出帧, 就判它不行, 把位子收回来。
static unsigned g_inject_grace = 0;
// ★交换链一旦真建过 feature 18, 这一局就不再交接★
//   血泪(审判之眼 23:35 / 23:37, 两次一模一样): 交换链已经跑了 8 帧、
//   资源和命令队列都在飞, 这时候把 feature 18 换手 ——
//     [carrier] 已交出 → [hostnr] ★就绪★ → DXGI_ERROR_DEVICE_REMOVED (INVALID_CALL)
//   设备当场没了。先建后放、先放后建都一样, 问题不在顺序, 在【中途换手】本身。
//   燕云那串崩溃多半也是它。
//   所以改成: 开局先让插入等一小会儿(开销可忽略, 那几帧本来也没画面),
//   谁先拿到就一直是谁, 一局之内不再换。
static bool     g_carrier_owned = false;   // 交换链建过了 → 插入这局别想了
// ★主动让位不是失败★
//   建 feature 返回 false 有两种含义: 真出错, 和「这局归就地插入/还在等」。
//   以前一律当失败计数, 三次就把交换链【永久停用】—— 审判之眼实测
//   「[carrier] 停止: 连续三次失败」。那是给自己埋雷: 万一插入后来挂了,
//   交换链已经停用, 两边都不做。
static bool     g_yielding = false;

// ★让位之后的宽限期★ (2026-09-04 傍晚, 龙之信条 2 实测)
//   就地插入认输时会先把自己的 feature 18 还掉, 但 NGX 那边释放不是瞬间完成的。
//   实测: 让位 2 毫秒后交换链就去建第二个 feature 18 → 运行库当场 0xC0000005,
//   交换链自己也跟着停用, 结果两边都不做, 比不让位还糟。
//   (同一条规矩在审判之眼上栽过一次: 两个 feature 18 同时存在必崩。)
//   所以让位后先空转这么多帧, 等旧的真放干净再建。
static int      g_yield_grace = 0;

// ★交接之后, 交换链先按住不动★
//   2026-09-05 00:35 实测: 就地插入去建, 运行库回「被别人占着」—— 它认得出来;
//   250 毫秒后交换链去建, 却【建成功了】, 然后设备就没了。
//   两条路用的参数块不一样(插入用游戏那块, 交换链用核心能力块), 所以
//   「建得成」在交换链这边根本不是「没人占着」的证据。
//   结论: 交接完先让就地插入去试 —— 它试不出来就老老实实报错, 不会闷头建。
//   交换链在这段时间里一帧都不许碰 feature 18。
static int  g_handover_hold      = 0;

// ★留到进程退出时才写的 NeuralUplift★
//   在游戏里写它 = 当场叫醒 RenoDX 去重新 Init 运行库 = 掉显卡(00:41 实测)。
//   退出时写就没这个问题, 而且下次启动它读到的就是对的。
static int  g_uplift_on_exit     = -1;   // -1 = 没有待办
// ★RenoDX 的真·总开关是 EnableHooks, 不是 NeuralUplift★
//   00:50 实测: NeuralUplift 置 0 之后它照样在游戏第一次调 DLSS 时建了 feature 18。
//   那个键只管它的后处理。EnableHooks=0 才让它整个不挂钩。
//   一样留到退出时写 —— 在游戏里写就是当场给它下命令。
static int  g_hooks_on_exit      = -1;   // -1 = 没有待办; 0 = 关掉它; 2 = NGX-only
// 开机时发现 EnableHooks 跟引擎选择对不上(上一局没干净退出) —— 已经改好, 但要再重进一次
static bool g_hooks_mismatch     = false;
static void FlushUpliftOnExit()
{
    if (g_uplift_on_exit < 0) return;
    reshade::set_config_value(nullptr, "RenoDX.DLSS5", "NeuralUplift",
                              g_uplift_on_exit ? "1" : "0");
    Log("[carrier] 退出时写入 NeuralUplift=%d", g_uplift_on_exit);
    g_uplift_on_exit = -1;
}

static void FlushHooksOnExit()
{
    if (g_hooks_on_exit < 0) return;
    char b[8]; std::snprintf(b, sizeof(b), "%d", g_hooks_on_exit);
    reshade::set_config_value(nullptr, "RenoDX.DLSS5", "EnableHooks", b);
    reshade::set_config_value(nullptr, "RenoDX.DLSS5", "NeuralUplift",
                              g_hooks_on_exit ? "1" : "0");
    Log("[carrier] 退出时写入 EnableHooks=%d —— 下次启动%s",
        g_hooks_on_exit,
        g_hooks_on_exit ? "由主插件做神经渲染" : "主插件整个不挂钩, feature 18 归我们");
    g_hooks_on_exit = -1;
}

// ★帧生成是不是真的在跑★
//   查的是【模块有没有被加载】, 不是文件在不在 —— 很多游戏带着 dlssg 的文件
//   却没开。只有真开了, 游戏才会把它加载进来。
//   延迟判定: 这些模块是游戏起来之后才加载的, 所以每次问, 一旦看见就记住。
// ★帧生成每个真帧递出几帧★ 由 mfg 那边每帧填(它读的是运行库自己报的
//   numFramesActuallyPresented)。1 = 没在插帧。carrier.h 在 mfg 之前被包含,
//   够不着 mfg::, 所以走这个外部开关, 跟 g_inject_live 一个套路。
static unsigned g_fg_presented = 1;
// 诊断: 这一局跳掉了多少插帧(面板/日志用)
static unsigned long long g_fg_skipped = 0;

static bool FrameGenRunning()
{
    static bool seen = false;
    if (seen) return true;
    if (GetModuleHandleW(L"nvngx_dlssg.dll") != nullptr ||
        GetModuleHandleW(L"sl.dlss_g.dll")   != nullptr)
    {
        seen = true;
        Log("[carrier] 检测到 DLSS 帧生成在跑 —— 交换链这条路让开, 不碰后缓冲");
        Log("          (帧生成会接管交换链, 我们再往后缓冲写就是抢同一块表面 → 闪退)");
        Log("          就地插入那条路不受影响: 它写的是游戏 DLSS 的输出, 在帧生成之前。");
    }
    return seen;
}
static ULONGLONG g_wait_started_ms = 0;    // 等游戏亮出 DLSS 的墙钟起点
static const ULONGLONG kInjectWaitMs = 60000;

static bool ShouldYieldToInject()
{
    // 刚让位: 继续空着, 等旧 feature 18 真的放干净(见 g_inject_grace 那段注释)
    if (g_handover_hold > 0)
    {
        if (--g_handover_hold == 0) Log("[carrier] 交接按压期结束, 交换链恢复正常判断");
    }
    if (g_yield_grace > 0)
    {
        if (--g_yield_grace == 0) Log("[carrier] 让位宽限期结束, 现在由交换链接手");
        return true;
    }
    if (!cfg.inject || g_inject_dead || g_carrier_owned) return false;
    return nrscale::game_eval > 0;       // 只认游戏那扇门的求值
}

// 开局先别建, 给「游戏自己的 DLSS」一个亮相的机会。
// 必须按墙钟而不是帧数：DD2 的标题/加载段能在几秒内跑完 240 帧，真正的
// DLSS 求值却约 20 多秒后才到。等候期间游戏照常渲染，只是 NR 尚未接管。
static bool WaitingForGameDlss()
{
    if (!cfg.inject || g_inject_dead || g_carrier_owned) return false;
    if (nrscale::game_eval > 0) return false;          // 已经亮了, 走让位那条
    const ULONGLONG now = GetTickCount64();
    if (g_wait_started_ms == 0) g_wait_started_ms = now;
    if (now - g_wait_started_ms >= kInjectWaitMs)
    {
        if (!g_inject_dead)
        {
            g_inject_dead = true;                      // 等够了没见着 → 交换链自己来
            g_inject_timed_out = true;
            Log("[carrier] 等了 60 秒没见游戏调 DLSS → 本局由交换链自己做");
        }
        return false;
    }
    return true;
}

// ★★RenoDX 在【本次启动】到底上没上场★★
//   它在自己加载的那一刻读一次 NeuralUplift, 读到 1 就占住 feature 18 一整局。
//   我们后来把 ini 改成 0 只对【下次启动】有效。
//   2026-09-05 00:35 燕云实测(游戏起不来): 我们在 StartSession 里把它改成 0,
//   然后自己就建了 —— 而 RenoDX 早在加载时就读到 1 并占住了。两个同时活 -> 死。
//   ★所以判据必须是「它开机时看到的值」, 不是「现在 ini 里写的值」★
static int  g_renodx_boot_uplift = -1;    // -1 = 还没读到
static bool g_renodx_present     = false;
static bool g_renodx_let_go      = false; // handover 走完一次让位才置 1

static bool RenodxOwns18()
{
    if (!g_renodx_present || g_renodx_let_go) return false;
    return g_renodx_boot_uplift != 0;
}

// 必须在任何人写 NeuralUplift 【之前】调
static void SnapshotRenodxBoot(bool present)
{
    if (g_renodx_boot_uplift >= 0) return;
    g_renodx_present = present;
    char cur[16] = {}; size_t n = sizeof(cur);
    // ★看 EnableHooks 不看 NeuralUplift★ 后者只管它的后处理;
    //   00:50 实测 NeuralUplift=0 时它照样抢了 feature 18。
    const bool got = reshade::get_config_value(nullptr, "RenoDX.DLSS5", "EnableHooks", cur, &n);
    // 没设置过就按【开着】算 —— 那是 RenoDX 自己的默认, 宁可保守
    g_renodx_boot_uplift = got ? std::atoi(cur) : 2;
    if (present)
        Log("[carrier] RenoDX 开机时的 EnableHooks = %d %s", g_renodx_boot_uplift,
            g_renodx_boot_uplift ? "→ 这一局 feature 18 归它, 我们一帧都不许建"
                                 : "→ 它这一局没上场, feature 18 归我们");
}
// 只放 feature 18, 别的资源留着 —— 让位给就地插入时用
static void ReleaseNrFeatureOnly()
{
    if (g.nr_feat == nullptr) return;
    DrainGpu();                       // 可能还在飞, 先等干净
    nrfwd::release(g.nr_feat, g.dying);
    g.nr_feat = nullptr;
    g.frame_ready = false;
    Log("[carrier] 已交出 feature 18 → 让就地插入独占");
}

// ------------------------------------------------------------ 建契约 (Feeder [1528], 参数名=官方头原文)
static bool CreateDlaaFeature(UINT w, UINT h, bool *crashed)
{
    *crashed = false;

    // ---- mode=3: 我们自己建 feature 18, 不经过任何人 ----
    // 尺寸就是【小尺寸】; 模型只做 1:1, 缩放是我们自己在前后两趟做的。
    if (cfg.mode >= 3)
    {
        // 就地插入已经挂上了, 那 feature 18 归它 —— 我们不能再建第二个(会崩)
        if (ShouldYieldToInject())
        {
            static bool said = false;
            if (!said) { said = true; Log("[carrier] 不建 feature 了: 游戏自己在调 DLSS, feature 18 归就地插入"); }
            g_yielding = true;
            return false;
        }
        if (WaitingForGameDlss()) { g_yielding = true; return false; }   // 开局先等一下, 别抢
        // ★★硬闸: RenoDX 这一局占着 feature 18 就绝不建★★
        //   闸门必须装在这里 —— 上一版我装到 CreateDlaaFeature 上, 那是 DLAA 契约,
        //   根本不是建 NR 的地方, 于是 00:35 那次照样建了第二个, 游戏起不来。
        if (RenodxOwns18() || nrscale::other_owns() || g_handover_hold > 0)
        {
            static bool said = false;
            if (!said)
            {
                said = true;
                Log("[carrier] 不建 feature 18: %s",
                    g_handover_hold > 0 ? "刚换完引擎, 先让就地插入去试(交换链按住)"
                                        : "主插件这一局占着 (要换手请在面板上点引擎单选)");
            }
            g_yielding = true;              // 让位不算失败, 别把自己计到停用
            return false;
        }
        g_yielding = false;

        if (!nrfwd::init(g.dev, engine033::Module()))
        { Disable(nrfwd::note()); return false; }

        if (!BeginCommands()) { Log("[carrier] 起不了命令列表"); return false; }

        DWORD seh = 0;
        void *f = nrfwd::create(g.dev, g.list, w, h, cfg.preset,
                                cfg.intensity, cfg.style,
                                cfg.local_structure, cfg.local_tone,
                                cfg.skin_structure, cfg.auto_mask, cfg.ui_correct,
                                &seh, cfg.global_tone);
        if (seh != 0)
        {
            AbortCommands();
            Log("[carrier] NR 建特性抛异常 0x%08lX (已兜住, 没有提交)", seh);
            *crashed = true;
            return false;
        }
        if (f == nullptr)
        {
            AbortCommands();
            Log("[carrier] NR 建特性失败: snippet Init=0x%08X CreateFeature=0x%08X",
                nrfwd::last_init(), nrfwd::last_create());
            return false;
        }
        // 建 feature 的初始化工作记在这条列表里, 必须真送到 GPU 才算数
        EndCommands();
        g.nr_feat = f;
        // ★这两行必须有★: 少了 frame_ready, 上面的 needs_build 每帧都成立,
        // 于是每帧重建一次资源和 feature —— 实测直接掉到 8 帧, GPU 却只有 14%
        // (卡的不是渲染, 是每 220ms 一次的 DrainGpu + 重建)。
        g.need_reset = true;
        g.frame_ready = true;
        g_carrier_owned = true;   // 这局归交换链, 中途不再换手(换手会丢设备)
        Log("[carrier] ★NR feature 18 建成 @ %ux%u (float 槽 %d)★", w, h, nrfwd::float_slot());
        return true;
    }

    unsigned int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if (g.depth_inverted) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (g.hdr)            flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

    g.params->Set(NVSDK_NGX_Parameter_CreationNodeMask,   1u);
    g.params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    g.params->Set(NVSDK_NGX_Parameter_Width,     w);
    g.params->Set(NVSDK_NGX_Parameter_Height,    h);
    g.params->Set(NVSDK_NGX_Parameter_OutWidth,  w);
    g.params->Set(NVSDK_NGX_Parameter_OutHeight, h);
    g.params->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<unsigned int>(NVSDK_NGX_PerfQuality_Value_DLAA));
    g.params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
    g.params->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0u);
    if (cfg.preset > 0) g.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, static_cast<unsigned int>(cfg.preset));

    if (!BeginCommands()) { Log("[carrier] 起不了命令列表"); return false; }
    DWORD code = 0;
    NVSDK_NGX_Handle *h_out = nullptr;
    const NVSDK_NGX_Result r = SafeCreate(&h_out, &code);
    if (code != 0)
    {
        AbortCommands();
        Log("[carrier] CreateFeature 抛异常 0x%08lX (已兜住, 没有提交)", code);
        *crashed = true;
        return false;
    }
    const UINT64 v = EndCommands();
    if (g.fence->GetCompletedValue() < v)
    {
        g.fence->SetEventOnCompletion(v, g.fence_event);
        if (WaitForSingleObject(g.fence_event, 4000) != WAIT_OBJECT_0) { Log("[carrier] 建特性 4 秒没完成"); Disable("建特性挂起"); return false; }
    }
    if (r != NVSDK_NGX_Result_Success || h_out == nullptr) { Log("[carrier] CreateFeature 失败 0x%08X", static_cast<unsigned int>(r)); return false; }
    g.feature = h_out;
    Log("[carrier] 契约就绪: %ux%u DLAA flags=%u (%s%sMVLowRes AutoExposure)", w, h, flags, g.hdr ? "HDR " : "SDR ", g.depth_inverted ? "DepthInverted " : "");
    g.need_reset = true;
    g.frame_ready = true;
    return true;
}

static bool BuildResources(device *dev, UINT bb_w, UINT bb_h, DXGI_FORMAT bb_fmt)
{
    // ★该让位就【一张纹理都别建】★
    //   以前的顺序是: 先把整套资源建出来(5120x2160 + 3840x1620 好几张),
    //   再进 CreateDlaaFeature 才发现该让给就地插入 —— 于是全扔掉, 下一帧再来一遍。
    //   审判之眼实测日志里「建资源: backbuffer 5120x2160」出现了 54 次。
    //   那是每帧一整套显存分配 + 释放, 纯烧性能。判断挪到最前面。
    if (cfg.fgsafe && FrameGenRunning()) { g_yielding = true; return false; }
    if (ShouldYieldToInject() || WaitingForGameDlss())
    {
        g_yielding = true;
        return false;
    }
    g_yielding = false;

    Log("[carrier] 建资源: backbuffer %ux%u fmt=%d → 工作 %d%%", bb_w, bb_h, bb_fmt, cfg.work);
    ReleaseFrameResources(dev);
    g.bb_w = bb_w; g.bb_h = bb_h; g.bb_fmt = bb_fmt;
    g.w = (bb_w * cfg.work / 100) & ~1u;
    g.h = (bb_h * cfg.work / 100) & ~1u;
    if (g.w < 64 || g.h < 64) { Disable("工作尺寸太小"); return false; }
    g.color_fmt = TypedColorFormat(bb_fmt);
    g.model_fmt = cfg.mode >= 3 ? DXGI_FORMAT_R16G16B16A16_FLOAT : g.color_fmt;
    g.hdr = IsHdrFormat(g.color_fmt);
    if (g.color_fmt == DXGI_FORMAT_UNKNOWN) { Disable("不支持的 backbuffer 格式"); return false; }

    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { g.color_fmt };
    if (SUCCEEDED(g.dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) && (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0)
        Log("[carrier] 注意: 格式 %d 不支持 UAV typed store, NGX 输出可能失败", g.color_fmt);

    if ((cfg.mode >= 3 && !MakeTex(&g.resolved, "resolved", bb_w, bb_h, g.color_fmt, true, D3D12_RESOURCE_STATE_COPY_SOURCE)) ||
        !MakeTex(&g.capture, "capture", bb_w, bb_h, g.color_fmt, true, D3D12_RESOURCE_STATE_COPY_DEST) ||
        !MakeTex(&g.color,   "color",   g.w, g.h, g.model_fmt, true, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !MakeTex(&g.output,  "output",  g.w, g.h, g.model_fmt, true, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
        !MakeInitTex(dev, &g.mv,    "mv",    g.w, g.h, format::r16g16_float, 4) ||
        !MakeInitTex(dev, &g.depth, "depth", g.w, g.h, format::r32_float,    4))
    { ReleaseFrameResources(dev); return false; }

    if (cfg.mode < 2)
    {   // 传输模式: 不建 NGX 特性, 只验拷贝/缩放/屏障链路
        g.frame_ready = true; g.need_reset = true;
        Log("[carrier] 传输模式就绪 (mode %d, 不碰 NGX)", cfg.mode);
        return true;
    }
    // ★不要再把 mode>=3 跳过这里★
    // CreateDlaaFeature 这个名字容易骗人: 它开头就有一段 if(cfg.mode >= 3),
    // 【feature 18 的创建(nrfwd::init + nrfwd::create) 全在里面】, 根本不碰 DLAA。
    // 我曾以为它只建超分而跳过, 结果神经渲染压根没创建 ——
    // 表现是面板卡在「启动中/转发组件未初始化」, 日志里只有
    // 「NR 求值失败 0x00000000」(巧妇难为无米之炊)。巫师 3 实测。
    bool crashed = false;
    if (!CreateDlaaFeature(g.w, g.h, &crashed)) { if (crashed) Disable("建特性时崩了(renodx 可能不兼容)"); return false; }
    return true;
}

// ------------------------------------------------------------ 会话 (Feeder [1777])
static bool InitSession(effect_runtime *rt)
{
    device *dev_api = rt->get_device();
    auto *dev = reinterpret_cast<ID3D12Device *>(dev_api->get_native());
    g.rs_queue = rt->get_command_queue();
    auto *queue = g.rs_queue ? reinterpret_cast<ID3D12CommandQueue *>(g.rs_queue->get_native()) : nullptr;
    if (dev == nullptr || queue == nullptr) { Disable("拿不到游戏的 D3D12 设备/队列"); return false; }
    g.dev = dev;
    queue->AddRef(); g.queue = queue;

    load_ngx_once();
    // 只卡【真正用得上的】那几个; Shutdown1 缺了不影响(我们从来不调它)
    if (g_ngx.missing_need > 0)
    {
        char b[128];
        std::snprintf(b, sizeof(b), "驱动的 NGX 核心少了 %d 个必需入口(见日志「少了这些入口」那行)",
                      g_ngx.missing_need);
        Disable(b);
        return false;
    }
    if (g_hs.r_init != NVSDK_NGX_Result_Success) do_handshake(dev);      // 只 Init 不 Shutdown
    if (g_hs.r_init != NVSDK_NGX_Result_Success) { Disable("NGX 初始化失败"); return false; }
    if (g_ngx.alloc(&g.params) != NVSDK_NGX_Result_Success || g.params == nullptr) { Disable("NGX 参数块分配失败"); return false; }
    {   // ★这道闸以前查的是【DLSS 超分】可不可用, 查错东西了★
        //   我们自研这条路做的是【神经渲染】(feature 18), 跟超分是两个 feature,
        //   完全不需要超分可用。没有原生 DLSS 的游戏(审判之逝实测)这一项就是 0,
        //   于是我们自己把自己关掉 —— 明明神经渲染是能跑的。
        //   现在只记录、不拦路: 真建不出来 feature 的时候, CreateFeature 会带着
        //   具体错误码停下来, 那才是有信息量的失败。
        NVSDK_NGX_Parameter *caps = nullptr;
        if (g_ngx.cap(&caps) == NVSDK_NGX_Result_Success && caps != nullptr)
        {
            int avail = 0;
            caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
            Log("[carrier] NGX 能力: SuperSampling.Available=%d (只作参考; 神经渲染不看这项)", avail);
            if (!avail && cfg.mode != 3)
            {
                // 老路(mode 1/2)是靠 DLAA 契约的, 那条确实要超分可用
                Disable("这块显卡/驱动上 DLSS 不可用"); return false;
            }
        }
    }

    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&g.fence)))) { Disable("建不了 fence"); return false; }
    g.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (int i = 0; i < kFrames; ++i)
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(&g.alloc[i])))) { Disable("建不了 allocator"); return false; }
    if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&g.list)))) { Disable("建不了命令列表"); return false; }
    g.list->Close();

    if (!scale::Create(g.blit, dev)) { Log("[carrier] 缩放着色器: %s", g.blit.error.c_str()); Disable("缩放着色器建不出来"); return false; }

    char v[16] = {}; size_t vs = sizeof(v);
    g.depth_inverted = true;
    if (rt->get_preprocessor_definition("RESHADE_DEPTH_INPUT_IS_REVERSED", v, &vs)) g.depth_inverted = std::atoi(v) != 0;

    // renodx 世代识别 + 只在【未设置】时播种三个键 (Feeder v0.11 [287-353] 的做法, 不覆盖用户的选择):
    //   EnableHooks=2       NGX-only —— 我们直接调 NGX, 不走 Streamline; v4.5+ 是"每次 present 重扫、懒接管"引擎
    //   NeuralUplift=1      神经渲染开
    //   NREnableUpscaling=0 我们发布的是 1:1 DLAA(哪怕缩了尺寸也是 1:1, 放大自己做), v4.6 的超分闩锁碰不到
    {
        const std::string rdx = game_dir() + "\\renodx-dlss5.addon64";
        const bool present = GetFileAttributesA(rdx.c_str()) != INVALID_FILE_ATTRIBUTES;
        const bool v47 = present && file_contains(rdx, "NRGlobalTone");
        const bool v46 = v47 || (present && file_contains(rdx, "NRToggleKey"));
        Log("[carrier] renodx-dlss5.addon64: %s%s", present ? "在场, 引擎=" : "不在场(那就只有 DLAA 没有 NR)",
            !present ? "" : v47 ? "v4.7+ (每次present重扫, 懒接管)" : v46 ? "v4.6+" : "classic");
        struct { const char *key, *val, *why; } seeds[] = {
            { "EnableHooks",       "2", "NGX-only, 我们直接调 NGX" },
            { "NeuralUplift",      "1", "神经渲染开" },
            { "NREnableUpscaling", "0", "我们发布 1:1 DLAA, 别让超分闩锁挡路" },
        };
        for (const auto &s : seeds)
        {
            char cur[16] = {}; size_t n = sizeof(cur);
            if (!reshade::get_config_value(nullptr, "RenoDX.DLSS5", s.key, cur, &n))
            {
                reshade::set_config_value(nullptr, "RenoDX.DLSS5", s.key, s.val);
                Log("[carrier] RenoDX.DLSS5 %s 未设置, 写 %s (%s) —— renodx 下次启动才读", s.key, s.val, s.why);
            }
            else
                Log("[carrier] RenoDX.DLSS5 %s=%s (用户设的, 不动)", s.key, cur);
        }
        // ★★按我们的引擎选择对齐 RenoDX 的开关 —— 只在【启动这一刻】做★★
        //   面板上换引擎只写 dlss5-033.cfg, 一行 live 状态都不改; RenoDX 的
        //   NeuralUplift 留到这里补。
        //   理由(2026-09-05 00:13 燕云实测, 一条日志说完):
        //     00:13:11.423  面板切到 RenoDX -> 我们当场把 NeuralUplift 改成 1
        //     00:13:12.660  RenoDX: signed DLSSNR runtime initialized   ← 它立刻醒了
        //     00:13:12.746  RenoDX: created inline NR resources
        //     --- 进程到此为止 ---
        //   它是「每次 present 重扫、懒接管」的, 在游戏里改它的配置 = 当场叫醒它
        //   去建 feature 18, 而那东西还在我们手里 —— 两个同时活必崩。
        //   现在这一刻谁都还没建, 怎么写都安全。
        //   ★判据用 cfg.inject 不用 cfg.enabled★: enabled 就是 F12 那个总开关,
        //   关掉会被自动保存, 拿它当引擎选择会把「按了 F12 就退出」误判成「选了 RenoDX」。
        //   inject 只有安装器和面板的引擎单选改, save_cfg 也不覆盖它。
        SnapshotRenodxBoot(present);   // ★必须在下面写 NeuralUplift 之前★
        // ★只有用户在面板上点过引擎才对齐★ 没点过就一个字都不动 ——
        //   安装器怎么配的就怎么跑, 我们没资格替他改主意。
        if (present && cfg.engine >= 0)
        {
            const bool ours = (cfg.engine != 0);
            const char *want = ours ? "0" : "1";
            char cur[16] = {}; size_t cn = sizeof(cur);
            const bool got = reshade::get_config_value(nullptr, "RenoDX.DLSS5", "NeuralUplift", cur, &cn);
            if (!got || std::strcmp(cur, want) != 0)
            {
                reshade::set_config_value(nullptr, "RenoDX.DLSS5", "NeuralUplift", want);
                Log("[carrier] 按引擎选择对齐: NeuralUplift %s -> %s ★这是写给下次启动的★",
                    got ? cur : "未设置", want);
            }
            // ★★EnableHooks 也要在这儿兜一次 —— 「退出时写」靠不住★★
            //   2026-09-05 01:00 实测: 用户在面板上选了主插件, dlss5-033.cfg 确实写成了
            //   carrier=0/inject=0, 但 DLL_PROCESS_DETACH 里那次 EnableHooks 写入【没发生】
            //   (日志里连那行都没有) —— 游戏是被结束进程的, 不是干净卸载 DLL 退出的。
            //   结果: 配置说「主插件做」, 而 EnableHooks 还停在 0(它整个不挂钩)
            //   —— 两边都不做, 玩家看到的就是「装了没效果」。
            //   所以启动时按 cfg 再对一次: 最坏就是多重进一次游戏, 不会卡在没人做的状态。
            {
                const char *hw = ours ? "0" : "2";
                char hcur[16] = {}; size_t hn = sizeof(hcur);
                const bool hgot = reshade::get_config_value(nullptr, "RenoDX.DLSS5", "EnableHooks", hcur, &hn);
                // ★主插件驾驶时, 已有的 1 不许降成 2★ (2026-09-05 死亡空间实测)
                //   1 = 连 Streamline 一起钩, 是 2 的超集。走 Streamline 递引导图的游戏
                //   (死亡空间这类)只有 1 才出帧 —— 那是监督器/用户特意设的, 这里一刷回 2
                //   就又是「装了没效果」。只在「没设 / 设成 0」时才写 2。
                const bool keep1 = (!ours && hgot && std::strcmp(hcur, "1") == 0);
                if (!keep1 && (!hgot || std::strcmp(hcur, hw) != 0))
                {
                    reshade::set_config_value(nullptr, "RenoDX.DLSS5", "EnableHooks", hw);
                    Log("[carrier] 按引擎选择对齐: EnableHooks %s -> %s ★下次启动生效★",
                        hgot ? hcur : "未设置", hw);
                    g_hooks_mismatch = true;    // 面板上提示「再重进一次就正常」
                }
            }
            // ★这一局谁做, 由开机快照说了算, 不由我们刚写的值说了算★
            if (ours && RenodxOwns18())
            {
                Log("[carrier] 但这一局 feature 18 还在主插件手上 —— 想现在就换, 面板上点一下引擎单选(会走几秒交接)");
            }
        }
        char st[16] = {}; size_t sn = sizeof(st);
        if (v46 && reshade::get_config_value(nullptr, "RenoDX.DLSS5", "NRStyle", st, &sn) && std::atoi(st) == 2)
            Log("[carrier] 注意: NRStyle=2 —— Feeder 作者在 Smooth Motion 开着时遇到过启动崩溃; 本机 SM 没开, 先不动");
    }

    g.session_ready = true;
    Log("[carrier] 会话就绪 (设备 %p, 队列 %p, depth_inverted=%d, blit=%d)", dev, queue, g.depth_inverted, dev_api->check_capability(device_caps::blit));
    return true;
}
static void ShutdownSession(device *dev)
{
    ReleaseFrameResources(dev);
    if (g.params) { if (!g.dying) g_ngx.destroy(g.params); g.params = nullptr; }
    // ★不调 NVSDK_NGX_D3D12_Shutdown1★ —— 会拆掉游戏自己的会话
    scale::Destroy(g.blit);
    SafeRelease(g.list);
    for (int i = 0; i < kFrames; ++i) SafeRelease(g.alloc[i]);
    SafeRelease(g.fence);
    if (g.fence_event) { CloseHandle(g.fence_event); g.fence_event = nullptr; }
    SafeRelease(g.queue);
    g.dev = nullptr; g.rs_queue = nullptr;
    g.session_ready = false;
}

// ------------------------------------------------------------ 每帧 (Feeder [2725-2962] + capture 中转 + 两次缩放)
// ------------------------------------------------------------ 一键测速
//  人工拖滑块比不出东西: 换档位的同时场景也在变, 实测同一个 75% 档
//  一段是 67.9 帧另一段 38.2 帧 —— 差的是场景不是档位。
//  所以让程序自己来: 站着别动, 它依次把档位切到 100/75/60/50,
//  每档先丢掉 0.8 秒(等重建和缓存稳定), 再数 3 秒的帧, 算中位帧时间。
namespace bench
{
// 第 0 档是【整个引擎关掉】—— 那才是真正的基准线。
// 不测它的话, 我们比别人多做的那三件事(拷原画/差值合成/拷回去)就永远看不见:
// 它们不随档位变, 全藏在"基础帧时间"里。
static const int kScales[5] = { 0, 100, 75, 60, 50 };   // 0 = 关闭
static int    state = 0;          // 0 空闲 1 预热 2 计时
static int    step  = 0;          // 0..4
static double fps[5] = { 0, 0, 0, 0, 0 };
static double ms[5]  = { 0, 0, 0, 0, 0 };
static int    saved_work = 75;
static int    saved_en = 1;
static LARGE_INTEGER t0 = {}, freq = {};
static int    frames = 0;
static const double kWarm = 0.8, kMeas = 3.0;

static void start()
{
    if (state != 0) return;
    QueryPerformanceFrequency(&freq);
    saved_work = cfg.work;
    saved_en   = cfg.enabled;
    step = 0; state = 1; frames = 0;
    for (int i = 0; i < 5; ++i) { fps[i] = 0; ms[i] = 0; }
    cfg.enabled = 0;                 // 第 0 档: 整个关掉
    QueryPerformanceCounter(&t0);
    Log("[bench] 开始测速: 站着别动, 约 15 秒");
}
static void stop() { if (state != 0) { state = 0; cfg.work = saved_work; cfg.enabled = saved_en; Log("[bench] 已取消"); } }

static void tick()
{
    if (state == 0) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    const double el = double(now.QuadPart - t0.QuadPart) / double(freq.QuadPart);
    ++frames;
    if (state == 1)                       // 预热: 等重建 + 缓存稳定
    {
        if (el >= kWarm) { state = 2; frames = 0; t0 = now; }
        return;
    }
    if (el < kMeas) return;               // 还在数
    fps[step] = frames / el;
    ms[step]  = 1000.0 * el / frames;
    // 日志里两个数都写: 面板上的档位名(内部值+50) 和 真实的分辨率占比。
    // 只写一个, 以后看日志算平方律会算错。
    if (kScales[step] == 0) Log("[bench] 关闭  → %.1f 帧 (%.2f ms/帧)", fps[step], ms[step]);
    else                    Log("[bench] %3d档 (实际 %d%%) → %.1f 帧 (%.2f ms/帧)",
                                kScales[step] + 50, kScales[step], fps[step], ms[step]);
    if (++step >= 5)
    {
        state = 0; cfg.work = saved_work; cfg.enabled = saved_en;
        Log("[bench] 测完: 关闭=%.1f  150档=%.1f  125档=%.1f  110档=%.1f  100档=%.1f 帧",
            fps[0], fps[1], fps[2], fps[3], fps[4]);
        // 我们比"完全不做"多花的固定开销 = 100% 档的帧时间 减去 关闭时的帧时间,
        // 再减掉神经渲染本身。神经渲染那部分能从 100% 和 50% 两点推出来:
        //   ms(s) = base + NR*s^2   =>   NR = (ms100 - ms50) / 0.75
        if (ms[0] > 0.0 && ms[1] > 0.0 && ms[4] > 0.0)
        {
            const double nr_full = (ms[1] - ms[4]) / 0.75;
            const double ours    = ms[1] - nr_full - ms[0];
            Log("[bench] 拆账: 关闭 %.2f ms | 神经渲染满档 %.2f ms | 我们的固定开销 %.2f ms",
                ms[0], nr_full, ours);
        }
        return;
    }
    cfg.enabled = 1;                      // 第 0 档之后一律开着
    cfg.work = kScales[step];             // 下一档, 下一帧自动重建
    state = 1; frames = 0; t0 = now;
}
} // namespace bench

// ------------------------------------------------------------ 真引导图
//  模型要 color / depth / motion。我们自己拿不到游戏的深度和运动矢量,
//  但包里那个 DLSS5_Feed.fx 已经在干这件事了 —— 它读 ReShade 的 DEPTH 语义
//  (带正反深度修正)和运动矢量提供者(VORT / LumeniteFX / texMotionVectors),
//  写进它自己的两张纹理:
//        texture DLSS5_Depth { R32F  }
//        texture DLSS5_MV    { RG16F }
//  我们按名字把这两张要过来直接喂给模型, 不用自己重造一遍。
//
//  引导图是满分辨率、模型跑在 75% —— 这是允许的, evaluate 分开收 guideW/guideH
//  (renodx 日志里 "NR input 5120x2160 (guides 3413x1440)" 就是这么用的)。
//
//  拿不到就退回全零占位, 效果差一点但不会坏。
struct Guides
{
    ID3D12Resource *depth = nullptr;
    ID3D12Resource *mv    = nullptr;
    UINT w = 0, h = 0;
    bool live = false;
    int  tried = 0;                  // 找过几次(避免每帧刷日志)
    std::string note = "还没找";
};
static Guides gd;

static ID3D12Resource *FxTexture(effect_runtime *rt, device *dev_api, const char *fx,
                                 const char *name, UINT *out_w, UINT *out_h)
{
    const effect_texture_variable v = rt->find_texture_variable(fx, name);
    if (v.handle == 0) return nullptr;
    resource_view srv = {}, srv_srgb = {};
    rt->get_texture_binding(v, &srv, &srv_srgb);
    if (srv.handle == 0) return nullptr;
    const resource res = dev_api->get_resource_from_view(srv);
    if (res.handle == 0) return nullptr;
    const resource_desc d = dev_api->get_resource_desc(res);
    if (out_w) *out_w = d.texture.width;
    if (out_h) *out_h = d.texture.height;
    return reinterpret_cast<ID3D12Resource *>(res.handle);
}

// 每帧刷新: 特效重编译后句柄会变, 所以不长期缓存
// 一次性: 把 DLSS5_Feed.fx 里所有纹理变量的名字打出来, 免得瞎猜
static void DumpFxTextures(effect_runtime *rt)
{
    static bool done = false;
    if (done) return;
    done = true;
    int n = 0;
    rt->enumerate_texture_variables("DLSS5_Feed.fx",
        [&n](effect_runtime *r, effect_texture_variable v)
        {
            char nm[128] = {};
            r->get_texture_variable_name(v, nm);
            resource_view srv = {}, srgb = {};
            r->get_texture_binding(v, &srv, &srgb);
            Log("[carrier]   纹理[%d] %s  srv=%llu", n++, nm, (unsigned long long) srv.handle);
        });
    if (n == 0) Log("[carrier] DLSS5_Feed.fx 一个纹理都枚举不到(名字对不上? 特效没启用?)");
}

// 老游戏(32位)那条路: 我们跑在 Feeder 的 host64 进程里, 深度和运动矢量是
// host 从游戏那边开过来的匿名共享纹理。我们给 host 加了个导出把指针交出来
// (改的是 dlss5-feed-host64.cpp, MIT, 见 工具\署名)。同进程直接取, 不用共享内存。
using PFN_FeedGuides = int(__cdecl *)(void **, void **, void **, void **, unsigned *, unsigned *);
static bool GuidesFromFeedHost()
{
    static PFN_FeedGuides fn = nullptr;
    static bool looked = false;
    if (!looked)
    {
        looked = true;
        fn = reinterpret_cast<PFN_FeedGuides>(
            GetProcAddress(GetModuleHandleW(nullptr), "dlss5_feed_033_guides"));
        if (fn != nullptr) Log("[carrier] 检测到 Feeder host, 引导图走它");
    }
    if (fn == nullptr) return false;

    void *dev = nullptr, *col = nullptr, *dep = nullptr, *mv = nullptr;
    unsigned w = 0, hh = 0;
    if (fn(&dev, &col, &dep, &mv, &w, &hh) != 1) return false;
    gd.depth = reinterpret_cast<ID3D12Resource *>(dep);
    gd.mv    = reinterpret_cast<ID3D12Resource *>(mv);
    gd.w = w; gd.h = hh;
    gd.live = true;
    gd.note = "Feeder host";
    return true;
}

static void RefreshGuides(effect_runtime *rt, device *dev_api)
{
    if (!cfg.guides) { gd.live = false; gd.note = "已关(cfg guides=0)"; return; }

    // 先试 Feeder host(老游戏路线), 拿到就不用找特效纹理了
    {
        const bool was = gd.live;
        if (GuidesFromFeedHost())
        {
            if (!was && gd.tried < 8)
            { ++gd.tried; Log("[carrier] ★引导图来自 Feeder host: %ux%u★", gd.w, gd.h); }
            return;
        }
    }

    DumpFxTextures(rt);

    UINT dw = 0, dh = 0, mw = 0, mh = 0;
    ID3D12Resource *d = FxTexture(rt, dev_api, "DLSS5_Feed.fx", "DLSS5_Depth", &dw, &dh);
    ID3D12Resource *m = FxTexture(rt, dev_api, "DLSS5_Feed.fx", "DLSS5_MV",    &mw, &mh);

    const bool ok = (d != nullptr && m != nullptr && dw > 0 && dh > 0 && dw == mw && dh == mh);
    const bool was = gd.live;
    gd.depth = ok ? d : nullptr;
    gd.mv    = ok ? m : nullptr;
    gd.w     = ok ? dw : 0;
    gd.h     = ok ? dh : 0;
    gd.live  = ok;
    if (ok) gd.note = "DLSS5_Feed.fx";
    else if (d == nullptr && m == nullptr) gd.note = "没找到 DLSS5_Feed.fx 的纹理(特效没开?)";
    else if (d == nullptr) gd.note = "只有运动矢量, 没有深度";
    else if (m == nullptr) gd.note = "只有深度, 没有运动矢量";
    else                   gd.note = "深度和运动矢量尺寸对不上";

    if ((ok != was || gd.tried == 0) && gd.tried < 8)
    {
        ++gd.tried;
        if (ok) Log("[carrier] ★接上真引导图: %ux%u (来自 DLSS5_Feed.fx)★", gd.w, gd.h);
        else    Log("[carrier] 引导图: %s —— 退回全零占位", gd.note.c_str());
    }
}

// ------------------------------------------------------------ 总开关
// 一个键开关整套神经渲染: F11。燕云定制版 S35 (业主 2026-09-24:「另外开启是F11，不是shift+f11」)
// 去掉了 2026-09-13 通用版为防误触加的 Shift —— 面板、安装器一直写的是 F11, 燕云里单按 F11 才对得上。
// Steam 默认 F12 截图保持独立；其他自定义冲突可以在
// dlss5-033.cfg 里改 hotkey=<VK十进制码>, 比如 F10 是 121, F11 是 122。
// ★★这一局 033 到底是不是引擎★★ (2026-09-05 业主实测:「F12 还是会唤起 033, 很恐怖」)
//   跟总开关无关, 只问一件事: 这一局谁在驾驶。
//     · inject != 0                 —— 安装器/面板明确让我们做就地插入
//     · 已经读到快照 且 主插件不在场 —— 那只能是我们
//   还没读到快照之前一律算「不是我们」: 宁可少醒一次, 不可多醒一次。
static bool WeAreEngine()
{
    if (cfg.inject != 0)          return true;
    if (g_renodx_boot_uplift < 0) return false;   // 开局那几帧, 快照还没拍
    return !g_renodx_present;
}

static void Toggle()
{
#if defined(K033_BETA2_RESHADE_HOST)
    if(!shared_ready)return;
#endif
    if(rendercore::Integrated()) {
        cfg.enabled = cfg.enabled ? 0 : 1;
        if(cfg.enabled)g.need_reset = true;
        Log("[033 hotkey] integrated NR %s vk=%d (single key, no Shift)",cfg.enabled?"on":"off",cfg.hotkey);
        return;
    }
    // ★★主插件在驾驶时, 一律不许把我们叫醒★★
    //   Steam 覆盖层默认就用 F12 截图 —— 玩家按它是家常便饭, 根本不算误触。
    //   叫醒的后果: 我们会去开一整套 D3D12 会话(NR 运行库 + 好几张 4K 纹理),
    //   一直走到 CreateDlaaFeature 才被那道硬闸拦下来。feature 18 没建成
    //   (那道闸是好的, 不会掉显卡), 但显存白占、面板还会当场长出一整块 033 的
    //   东西 —— 而 V5.0 里 033 本来就是锁着不给用的。
    //   ★只挡「开」这个方向★: 关(1->0)永远随便按, 那个方向怎么都不会出事。
    const bool want_on = (g.disabled || cfg.enabled == 0);
    if (want_on && !WeAreEngine())
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            Log("[carrier] 热键: 这一局引擎是主插件 RenoDX —— 不叫醒自研引擎(按键忽略)");
        }
        return;
    }

    if (g.disabled)                       // 之前自己停掉的, 一键当"再试一次"
    {
        g.disabled = false; g.consecutive_fails = 0; g.disabled_why.clear();
        cfg.enabled = 1;
        Log("[carrier] 热键: 重新启用");
        return;
    }
    cfg.enabled = cfg.enabled ? 0 : 1;
    if (cfg.enabled) g.need_reset = true; // 重新开的第一帧丢掉时域历史
    Log("[carrier] 热键: %s", cfg.enabled ? "开" : "关");
}

// 边沿触发: 只在"刚按下"那一帧动一次, 按住不会连发
static bool g_hotkey_was_down = false;
// S37 (业主「另外快捷键能改」): 面板换键。新键要先松开一次才生效 —— 选键时按下的那一下不算开关。
static void SetHotkey(int vk)
{
    cfg.hotkey = vk;
    g_hotkey_was_down = true;
}
// S39 (业主「F11的热键修改有问题，有些键不能用」): 字母、数字也能当开关键了。面板在等新键、或者面板里
// 有输入框正在打字时, 这一下不算开关; 面板关了(250 ms 没画)就不再拦。
static std::atomic<unsigned long long> g_hotkey_panel_ms{0};
static std::atomic<bool> g_hotkey_panel_blocks{false};
static void HotkeyPanelFrame(bool blocks)
{
    g_hotkey_panel_blocks.store(blocks);
    g_hotkey_panel_ms.store(GetTickCount64());
}
static void HotkeyTick()
{
    if (cfg.hotkey <= 0) return;
    const bool down = (GetAsyncKeyState(cfg.hotkey) & 0x8000) != 0;   // 单按(按着 Shift 也一样, F9 除外)
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    DWORD owner = 0;                                                    // 只认游戏窗口在前台时按的
    if (HWND front = GetForegroundWindow()) GetWindowThreadProcessId(front, &owner);
    const bool panelBlocks = g_hotkey_panel_blocks.load() && GetTickCount64() - g_hotkey_panel_ms.load() < 250;
    if (hotkey033::Fires(cfg.hotkey, down, g_hotkey_was_down, owner == GetCurrentProcessId(), shift, panelBlocks)) Toggle();
    g_hotkey_was_down = down;
}

// 每帧回调里那些 return 全是静默的, 用户永远停在「启动中」, 日志一个字没有,
// 只能靠猜(评论区实测: 这类问题占了"装了没效果"的一大半)。
// 每种原因只打一次, 不刷屏。
// 就地插入那条路一旦真的在出帧, 就把这个置 1, 让交换链那条路让位。
static bool g_inject_live = false;

static void BailOnce(const char *why)
{
    static std::string last;
    if (last == why) return;
    last = why;
    Log("[carrier] 没开工: %s", why);
}

static void OnFinishEffects(effect_runtime *rt, command_list *cl, resource_view rtv, resource_view)
{
    if (rendercore::Integrated()) return;
    // ★在 Feeder 帮手进程里直接不干活★
    // 那里 ReShade 的后缓冲是帮手自己那个小窗, 不是游戏画面 —— 在上面做降噪
    // 等于给 UI 窗口降噪, 白跑还多占一份 NGX。游戏画面由 hostnr 那条路处理。
    // ★在 Feeder 的宿主里就直接不干活★
    // 那里 ReShade 的后缓冲不是游戏画面(帮手是它自己那个窗口; D3D11 路线是
    // 游戏的 D3D11 后缓冲, 而 NR 跑在 Feeder 私有的 D3D12 会话上)。
    // 画面走 hostnr 那条路 —— 由宿主把真正的帧回调给我们。
    // 这里自己查, 不引用 hostnr: 它在本文件之后才被包含。
    static const bool in_feed = []() -> bool {
        if (GetProcAddress(GetModuleHandleW(nullptr), "dlss5_feed_033_set_nr") != nullptr) return true;
        HMODULE m = GetModuleHandleW(L"dlss5-feed.addon64");
        return m != nullptr && GetProcAddress(m, "dlss5_feed_033_set_nr") != nullptr;
    }();
    if (in_feed) { BailOnce("在 Feeder 宿主里(正常, 画面由老游戏那条路处理)"); return; }

    HotkeyTick();                          // ★必须在下面这行之前★: 关掉之后还得能按键开回来
    // ★autobench★ 神经渲染跑稳之后自动跑一遍测试(连着换 5 个档位 = 连着重建 5 次),
    //   专门用来验「调参数会不会崩」。出厂关, 只有 cfg 里显式写 autobench=1 才跑。
    if (cfg.autobench && bench::state == 0)
    {
        static unsigned s_ab_wait = 0;
        if (g.frames_done > 120 && ++s_ab_wait == 1)
        { Log("[bench] autobench=1: 自动开跑压力测试(连换 5 档)"); bench::start(); }
    }
    bench::tick();                         // ★也必须在前面★: 测速第一档就是"整个关掉",
                                           //   放到 return 后面它自己就再也不会被调用, 卡死在第 0 档
    // ★就地插入已经在干活的话, 这里就别再做一遍★
    // (两趟神经渲染叠着跑 = 白烧 GPU + 画面反而坑)
    // 这里不能直接引 inject(它包含在本文件之后), 用一个外部开关。
    if (g_inject_live) { BailOnce("已在游戏自己的 DLSS 里做了(那里画面更干净)"); return; }
    // A fixed injection test must not silently turn into a backbuffer test.
    // Keep this before session/feature creation, including failure and timeout paths.
    if (cfg.inject && !cfg.autoroute)
    { BailOnce("固定就地插入路径：交换链不接管，结果以 inject/hostnr 日志为准"); return; }
    // ★挂上钩子就得马上让位★ 不能等它出帧再让 —— 它建 feature 时我们要是还
    // 占着位, 它一建就是第二个 feature 18, 直接访问违例。
    if (cfg.fgsafe && FrameGenRunning())
    {
        ReleaseNrFeatureOnly();
        BailOnce("游戏开着 DLSS 帧生成 —— 交换链让开(帧生成接管了交换链, 抢着写会闪退)");
        return;
    }
    if (ShouldYieldToInject())
    {
        ReleaseNrFeatureOnly();   // 只有「还没建过」才会走到这里, 所以其实是个空操作
        // 宽限: 让出去之后给它一段时间出帧; 一直不出就收回来(见 present 里那段)
        if (g_inject_grace < 0xFFFFFFFFu) ++g_inject_grace;
        BailOnce("让位给就地插入(在游戏自己的渲染分辨率上做, 更快更干净)");
        return;
    }
    // ══════════════════════════════════════════════════════════════
    //  ★插出来的帧不做神经渲染★ (2026-09-05 面板量出来的)
    //
    //  帧生成在跑时, 一个真帧会 present 出 N 帧 —— 其中 N-1 帧是插值出来的。
    //  插帧是【从真帧插出来的】, 而真帧我们已经做过神经渲染了, 在插帧上再跑
    //  一遍模型纯属重复劳动。鬼武者实测: 画面 37 帧/秒, 我们做了 38 次 NR,
    //  模型 13.87ms 有一半是白花的。
    //
    //  ★怎么认出插帧★ 插帧那一次 present, 游戏【根本没渲染】, 所以不会有新的
    //  NGX 求值。nrscale::game_eval 只数游戏那扇门的求值 —— 它不动 = 这帧是插的。
    //  这个判据跟「游戏一帧调几次 NGX」无关, 所以比按次数取模稳。
    //
    //  ★两道保险, 保证最坏情况不比现在差★
    //    · 只在运行库确实报了 N>=2 时才启用 —— 没开帧生成的游戏一帧都不会被跳
    //    · 连续最多跳 N-1 帧 —— 万一 game_eval 因为别的原因不动, 也不会一直跳掉
    // ══════════════════════════════════════════════════════════════
    if (cfg.skipgen && g_fg_presented >= 2)
    {
        static int      s_last_eval = -1;
        static unsigned s_run       = 0;      // 连着跳了几帧
        const int ev = nrscale::game_eval;
        if (ev == s_last_eval && s_run < g_fg_presented - 1)
        {
            ++s_run;
            ++g_fg_skipped;
            return;                            // 这一帧是插的, 不做
        }
        s_last_eval = ev;
        s_run = 0;
    }
    if (!cfg.enabled) { BailOnce("总开关是关的(使用效果快捷键或面板开启)"); return; }
    if (g.disabled)   { BailOnce("已被停用, 原因见上面的『停止:』那行"); return; }
    if (g.dying) return;
    RefreshGuides(rt, rt->get_device());
    if (InterlockedCompareExchange(&g.busy, 1, 0) != 0) return;   // 重入直接丢帧
    struct Unbusy { ~Unbusy() { InterlockedExchange(&g.busy, 0); } } unbusy;

    device *dev_api = rt->get_device();
    if (dev_api->get_api() != device_api::d3d12)
    { BailOnce("这游戏不是 D3D12(自研引擎只做 D3D12; 别的接口走 Feeder 那条路)"); return; }

    const resource bb_res = dev_api->get_resource_from_view(rtv);
    auto *bb = reinterpret_cast<ID3D12Resource *>(bb_res.handle);
    if (bb == nullptr) { BailOnce("拿不到后缓冲"); return; }
    const D3D12_RESOURCE_DESC cd = bb->GetDesc();
    const UINT bw = static_cast<UINT>(cd.Width), bh = cd.Height;
    if (cd.SampleDesc.Count != 1) { BailOnce("后缓冲开了 MSAA, 接不上"); return; }

    auto *native_dev = reinterpret_cast<ID3D12Device *>(dev_api->get_native());
    if (g.session_ready && g.dev != native_dev) { Log("[carrier] 游戏重建了设备, 重开会话"); ShutdownSession(dev_api); }
    bool ok = g.session_ready || InitSession(rt);

    // hook-arming 宽限 (Feeder [2796-2810]): renodx 异步装钩, 早了 EXEC 0x0
    // ★调参数不能当场重建★ (2026-09-04 傍晚, 龙之信条 2 实测两次)
    //   重建 = 释放并重新分配好几张 5120x2160 / 3840x1620 的纹理 + 重建 feature 18。
    //   而 DD2 开着 DLSS 帧生成, GPU 落后好几帧 —— 在这个时候动显存, 轻则卡死,
    //   重则「失败: 建资源」→ 设备移除。业主实测: 面板上拖一下就崩, 拖一下就卡。
    //   所以【尺寸/格式变了】必须立刻重建(那是游戏改分辨率, 不重建就没法工作),
    //   而【用户调画质参数】一律防抖: 手停下来一会儿再重建, 拖动过程中不动显存。
    const bool size_changed = !g.frame_ready || bw != g.bb_w || bh != g.bb_h || cd.Format != g.bb_fmt;
    const bool tune_changed = (cfg.work != g.built_work) ||
                              (cfg.mode >= 3 && TuneSig() != g.built_tune);
    static int  s_tune_hold  = 0;
    static bool s_tune_armed = false;
    if (tune_changed)
    {
        if (!s_tune_armed) { s_tune_armed = true; s_tune_hold = 0; }
        else if (s_tune_hold < 45) ++s_tune_hold;      // 约 0.5-0.75 秒手停
    }
    else { s_tune_armed = false; s_tune_hold = 0; }
    const bool tune_ready = s_tune_armed && s_tune_hold >= 45;
    if (tune_ready) { s_tune_armed = false; s_tune_hold = 0; }
    const bool needs_build = size_changed || tune_ready;
    if (g.frame_ready && needs_build) g.create_grace = 0;
    // mode>=3 是我们自己调 NR, 不靠 renodx 的钩子, 所以不用押后
    if (cfg.mode >= 3) g.create_grace = cfg.create_delay;
    if (ok && needs_build && g.create_grace < cfg.create_delay)
    {
        if (++g.create_grace == 1) Log("[carrier] 押后 %d 帧再建特性(等 renodx 装好钩)", cfg.create_delay);
        ok = false;
    }
    if (ok && needs_build)
    {
        // ★这行日志不能打在这儿★ BuildResources 一进去就可能因为「该让位」直接返回,
        //   真正的分配根本没发生 —— 打在前面就成了假消息, 而且刷屏(实测 2 秒 113 条)。
        //   挪进 BuildResources 里, 过了让位判断之后再打。
        ok = BuildResources(dev_api, bw, bh, cd.Format);
        // 让位/等待不计失败 —— 否则三次就把自己永久停用了
        if (!ok) { if (!g_yielding) Fail("建资源"); }
        else { g.consecutive_fails = 0; g.built_work = cfg.work; g.built_tune = TuneSig(); }
    }
    if (!ok) return;

    const resource cap_res = { reinterpret_cast<uint64_t>(g.capture) };
    LARGE_INTEGER t0, t1, fq; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);

    // 1) backbuffer ─copy─▶ capture   (ReShade 追踪的 bb 状态此刻是 render_target [2826]; capture 常驻 copy_dest)
    {
        const resource       res[1]  = { bb_res };
        const resource_usage from[1] = { resource_usage::render_target };
        const resource_usage to[1]   = { resource_usage::copy_source };
        cl->barrier(1, res, from, to);
    }
    cl->copy_resource(bb_res, cap_res);
    {
        // bb 停在 copy_dest 等着接结果
        const resource       res[1]  = { bb_res };
        const resource_usage from[1] = { resource_usage::copy_source };
        const resource_usage to[1]   = { resource_usage::copy_dest };
        cl->barrier(1, res, from, to);
    }

    // 2) ReShade 已录的先送队列; 我们的列表紧跟其后(同队列, 天然有序) [2864-2867]
    g.rs_queue->flush_immediate_command_list();

    bool restored = false;
    if (!BeginCommands()) { Fail("命令列表"); }
    else
    {
        // ★GPU 分段计时(交换链路, 我们自己的命令列表, 天然安全)★
        //   只量自研 NR 路(mode>=3): 编码 → 模型 → 合成 三段。拷入/拷回在 ReShade 的
        //   列表上, 量不到, 所以这条路是三段, 面板按路线换标签。其它路线一律 Abort。
        gputime::EnsureDevice(g.dev);
        if (!gputime::ready()) gputime::Create(g.dev, g.queue);
        gputime::Begin(g.list);
        // 上一帧走了 100% 短路, output 停在 copy_source, 先转回 UAV
        if (g.out_in_copy)
        {
            scale::Barrier(g.list, g.output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            g.out_in_copy = false;
        }

        // 2a) capture(copy_dest→SR) ─缩小─▶ color(SR→UAV→SR)
        // ★100% 档短路★: 档位拉满时这一趟是 5120x2160 → 5120x2160 的一比一,
        // 等于把 1100 万像素原样抄一遍, 纯浪费。直接把 capture 当模型输入。
        const bool reduced = (g.w != g.bb_w || g.h != g.bb_h);
        const bool own_nr  = (cfg.mode >= 3);
        scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        // ★这一趟现在【任何档位都要跑】★
        //   以前只在缩小时跑(100% 档直接把原画喂模型)。现在这一趟还负责【编码】:
        //   白点归一 + 高光滚降 + sRGB —— 那才是模型训练时见到的图。
        //   100% 档跳过它, 模型就还是拿原始线性帧, 屏闪照旧、细节也上不来。
        //   同尺寸时着色器走 Load 直通, 只做编码, 不会引入任何重采样。
        const auto grade=pregrade::Checked(&cfg.pre);
        const unsigned gradeSig=pregrade::Signature(grade);
        if(g.pre_signature!=gradeSig){g.pre_signature=gradeSig;g.need_reset=true;}
        const int enc = EncodeMode(g.color_fmt, true);
        const bool need_pass = reduced || !own_nr || enc != 0 || g.model_fmt != g.color_fmt || grade.enabled;
        if (need_pass)
        {
            scale::Barrier(g.list, g.color, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Dispatch(g.blit, g.dev, g.list, g.capture, g.color_fmt, g.color, g.model_fmt, g.w, g.h, 0,
                            EffectiveWhite(), enc, nullptr, EffectiveCurve(), cfg.diffuse_white, &grade);
            scale::Barrier(g.list, g.color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        gputime::Stamp(g.list);   // ① 编码(100% 直通时这一段≈0)
        ID3D12Resource *model_in = need_pass ? g.color : g.capture;

        // 2b) 求值 (Feeder [2880-2895], 名字=官方头原文)
        // 占位运动矢量是全零, 时域历史没意义, 必须每帧 Reset 否则拖影;
        // 换成真运动矢量之后就别 Reset 了, 让模型累积 —— 那才是它的价值。
        const bool force_reset = cfg.reset_every && !(gd.live && cfg.mode >= 3);
        const int reset = (g.need_reset || force_reset) ? 1 : 0;
        g.need_reset = false;

        if (cfg.mode < 2)
        {
            gputime::Abort();
            // 传输模式: 跳过 NGX, 把缩小后的 color 直接当 output 放大回去 (验证链路 + 看缩放画质)
            scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Dispatch(g.blit, g.dev, g.list, g.color, g.color_fmt, g.capture, g.color_fmt, g.bb_w, g.bb_h, 1);
            scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            EndCommands();
            cl->copy_resource(cap_res, bb_res);
            {
                const resource       res[2]  = { bb_res, cap_res };
                const resource_usage from[2] = { resource_usage::copy_dest,     resource_usage::copy_source };
                const resource_usage to[2]   = { resource_usage::render_target, resource_usage::copy_dest };
                cl->barrier(2, res, from, to);
            }
            restored = true;
            const UINT64 n = ++g.frames_done;
            QueryPerformanceCounter(&t1);
            g.last_ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(fq.QuadPart);
            if (n <= static_cast<UINT64>(cfg.log_frames) || (n % 1800) == 0)
                Log("[carrier] 传输帧 %llu (%ux%u → %ux%u → 回写, CPU 侧 %.2f ms)", n, bw, bh, g.w, g.h, g.last_ms);
            return;
        }
        // ---- mode=3: 我们自己求值 feature 18 ----
        // 调校参数在 create 时已经给过了(模型只读一次), 这里只给资源和每帧的量。
        if (cfg.mode >= 3)
        {
            DWORD nseh = 0;
            // 有真引导图就用真的(满分辨率), 没有就退回全零占位(工作尺寸)
            ID3D12Resource *gd_depth = gd.live ? gd.depth
                                               : reinterpret_cast<ID3D12Resource *>(g.depth.handle);
            ID3D12Resource *gd_mv    = gd.live ? gd.mv
                                               : reinterpret_cast<ID3D12Resource *>(g.mv.handle);
            const UINT gw = gd.live ? gd.w : g.w;
            const UINT gh = gd.live ? gd.h : g.h;
            // ★DLSS5_MV 已经是像素单位★(DLSS5_Feed.fx 第 7 行: "motion vectors in PIXELS"),
            // 不要再乘宽高 —— 乘了等于把位移放大 5120 倍, 模型按它去对齐上一帧全错, 画面就闪。
            const float mvsx = cfg.mvscale / 100.0f;
            const float mvsy = cfg.mvscale / 100.0f;
            const int nr = nrfwd::evaluate(
                g.list, g.nr_feat, model_in, gd_depth, gd_mv, g.output,
                g.w, g.h, gw, gh, g.depth_inverted ? 1 : 0, reset,
                cfg.intensity, cfg.style, cfg.local_structure, cfg.local_tone,
                cfg.skin_structure, cfg.auto_mask, mvsx, mvsy, &nseh, nullptr, cfg.global_tone);
            g.last_eval = static_cast<NVSDK_NGX_Result>(nr);
            if (nseh != 0)
            {
                gputime::Abort();
                AbortCommands();
                Log("[carrier] NR 求值抛异常 0x%08lX (已兜住, 没有提交)", nseh);
                Disable("NR 求值崩了");
                g.frame_ready = false;
                return;
            }
            if (nr != NVSDK_NGX_Result_Success)
            {
                gputime::Abort();
                AbortCommands();
                Log("[carrier] NR 求值失败 0x%08X", static_cast<unsigned int>(nr));
                Fail("NR 求值");
                g.frame_ready = false;
                return;
            }
            // ★成功: 差值合成★
            // 不是"把模型的小图放大盖回去"(那样整张图都过一次往返, 必然发软),
            // 而是: 满分辨率原画一动不动, 只取「模型输出 − 模型看到的输入」这个
            // 差值放大后叠上去。放大的是那点修正, 不是图像本身。
            // 100% 档时 模型输入 == 原画, 差值 = 输出 − 原画, 结果恰好 == 模型输出。
            scale::UavBarrier(g.list, g.output);

            // ★★ 这条短路已经作废, 不许再走 ★★
            //   它成立的前提是「加法差值合成」: 100% 时 模型输入==原画,
            //   原画 + (输出−原画) == 输出, 所以直接拷 output 是等价的。
            //   现在两个前提都没了:
            //     ① 模型输入是【编码过】的代理图(白点归一+滚降+sRGB), 不再等于原画。
            //        直接把模型输出拷回画面 = 一张双重编码的惨白图。
            //     ② resolve 里现在有高光护栏 / 暗部地板 / 色域压缩 —— 那些正是
            //        治屏闪的东西。跳过 resolve 就等于把屏闪原样留着。
            //   代价是 100% 档多一趟全屏计算; 参考实现也是每帧都过 resolve 的。
            gputime::Stamp(g.list);   // ② 模型
            if (false)
            {
                const resource out_res = { reinterpret_cast<uint64_t>(g.output) };
                scale::Barrier(g.list, g.output,  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                g.out_in_copy = true;          // 下一帧开头要把它转回 UAV
                EndCommands();
                cl->copy_resource(out_res, bb_res);
                {
                    const resource       res[1]  = { bb_res };
                    const resource_usage from[1] = { resource_usage::copy_dest };
                    const resource_usage to[1]   = { resource_usage::render_target };
                    cl->barrier(1, res, from, to);
                }
                restored = true;
                g.consecutive_fails = 0;
                const UINT64 n = ++g.frames_done;
                QueryPerformanceCounter(&t1);
                g.last_ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(fq.QuadPart);
                if (n <= static_cast<UINT64>(cfg.log_frames) || (n % 1800) == 0)
                    Log("[carrier] NR 帧 %llu (%ux%u 直通, 无缩放无合成, CPU 侧 %.2f ms)",
                        n, bw, bh, g.last_ms);
                return;
            }

            const resource res_res = { reinterpret_cast<uint64_t>(g.resolved) };
            scale::Barrier(g.list, g.output,   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale::Barrier(g.list, g.resolved, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::DispatchResolve(g.blit, g.dev, g.list,
                                   g.capture,          // 满分辨率原画(SR 态, 缩小那趟之后就是)
                                   model_in,           // 模型看到的(100% 时它就是 capture 本身)
                                   g.output,           // 模型给出的(小, 刚转 SR 态)
                                   g.color_fmt, g.resolved, g.bb_w, g.bb_h,
                                   cfg.blend / 100.0f, EffectiveWhite(), cfg.guard,
                                   ResolveMode(g.color_fmt, true), cfg.colour, EffectiveSharpen(),
                                   nullptr, EffectiveCompose(), cfg.resample, EffectiveCurve(),
                                   cfg.applymodel, cfg.comparepct > 0 && cfg.comparepct < 100 ? cfg.comparepct / 100.0f : 0.0f, cfg.diffuse_white, nullptr, &grade,
                                   nrskin::Protection(cfg.passes), cfg.skin_lift);
            scale::Barrier(g.list, g.output,   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Barrier(g.list, g.resolved, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            // capture 还回常驻的 copy_dest —— 老的 DLAA 路径漏了这一步(N 卡不校验所以没炸)
            scale::Barrier(g.list, g.capture,  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            gputime::End(g.list);     // ③ 合成
            EndCommands();
            cl->copy_resource(res_res, bb_res);
            {
                const resource       res[1]  = { bb_res };
                const resource_usage from[1] = { resource_usage::copy_dest };
                const resource_usage to[1]   = { resource_usage::render_target };
                cl->barrier(1, res, from, to);
            }
            restored = true;
            g.consecutive_fails = 0;
            const UINT64 n = ++g.frames_done;
            if (gputime::ready() && gputime::freq_ok() && gputime::total() > 0.0 && (n % 120) == 0)
                Log("[carrier] GPU 侧: 编码 %.2f · 模型 %.2f · 合成 %.2f = %.2f ms (模型 %ux%u, 精度 %d%%)",
                    gputime::seg(0), gputime::seg(1), gputime::seg(2), gputime::total(), g.w, g.h, cfg.work);
            QueryPerformanceCounter(&t1);
            g.last_ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(fq.QuadPart);
            if (n <= static_cast<UINT64>(cfg.log_frames) || (n % 1800) == 0)
                Log("[carrier] NR 帧 %llu (%ux%u → 模型 %ux%u → 回写, CPU 侧 %.2f ms)",
                    n, bw, bh, g.w, g.h, g.last_ms);
            return;
        }

        gputime::Abort();   // renodx 那条路不量
        g.params->Set(NVSDK_NGX_Parameter_Color,  g.color);
        g.params->Set(NVSDK_NGX_Parameter_Output, g.output);
        g.params->Set(NVSDK_NGX_Parameter_Depth,  reinterpret_cast<ID3D12Resource *>(g.depth.handle));
        g.params->Set(NVSDK_NGX_Parameter_MotionVectors, reinterpret_cast<ID3D12Resource *>(g.mv.handle));
        g.params->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f);
        g.params->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
        g.params->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
        g.params->Set(NVSDK_NGX_Parameter_Reset, reset);
        g.params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
        g.params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
        g.params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,  g.w);
        g.params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, g.h);
        g.params->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,   1.0f);
        g.params->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);

        DWORD code = 0;
        const NVSDK_NGX_Result re = SafeEvaluate(&code);
        g.last_eval = re;
        if (code != 0)
        {
            AbortCommands();
            Log("[carrier] evaluate 抛异常 0x%08lX (已兜住, 没有提交)", code);
            Disable("求值崩了(renodx 可能不兼容这个游戏/分辨率)");
            g.frame_ready = false;
        }
        else if (re != NVSDK_NGX_Result_Success)
        {
            // 列表里已有缩放工作但无害; 照 Feeder: 失败就不回写, 后面把 bb 还回去
            AbortCommands();
            Log("[carrier] evaluate 失败 0x%08X", static_cast<unsigned int>(re));
            Fail("求值");
            g.frame_ready = false;
        }
        else
        {
            // 2c) output(UAV→SR) ─放大─▶ capture(SR→UAV→copy_source); 然后 output 回 UAV, capture 回 copy_dest 留给回写
            scale::UavBarrier(g.list, g.output);
            scale::Barrier(g.list, g.output,  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Dispatch(g.blit, g.dev, g.list, g.output, g.color_fmt, g.capture, g.color_fmt, g.bb_w, g.bb_h, 1);
            scale::Barrier(g.list, g.output,  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            scale::Barrier(g.list, g.capture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            EndCommands();

            // 3) capture ─copy─▶ backbuffer (记在 ReShade 新一条 immediate 上, 同队列排在我们之后 [2919])
            cl->copy_resource(cap_res, bb_res);
            {
                const resource       res[1]  = { bb_res };
                const resource_usage from[1] = { resource_usage::copy_dest };
                const resource_usage to[1]   = { resource_usage::render_target };
                cl->barrier(1, res, from, to);
            }
            // capture 回到常驻态 copy_dest —— 这条也录在 immediate 上, 排在回写拷贝之后
            {
                const resource       res[1]  = { cap_res };
                const resource_usage from[1] = { resource_usage::copy_source };
                const resource_usage to[1]   = { resource_usage::copy_dest };
                cl->barrier(1, res, from, to);
            }
            restored = true;
            const UINT64 n = ++g.frames_done;
            g.consecutive_fails = 0;
            QueryPerformanceCounter(&t1);
            g.last_ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(fq.QuadPart);
            if (n <= static_cast<UINT64>(cfg.log_frames) || (n % 1800) == 0)
                Log("[carrier] 第 %llu 帧已交付 (%ux%u → %ux%u, reset=%d, CPU 侧 %.2f ms)", n, bw, bh, g.w, g.h, reset, g.last_ms);
        }
    }

    if (!restored)
    {
        // 出了什么事都把 backbuffer 按 ReShade 期待的状态还回去 [2954-2961]; capture 也回常驻态
        // (失败分支里 capture 停在 copy_dest 没动 —— 我们的列表被丢弃了, 它的状态转换没执行)
        const resource       res[1]  = { bb_res };
        const resource_usage from[1] = { resource_usage::copy_dest };
        const resource_usage to[1]   = { resource_usage::render_target };
        cl->barrier(1, res, from, to);
    }
}

static void OnDestroyRuntime(effect_runtime *rt) { (void)rt; PollConfig(true); }   // Keep the feature across runtime recreation.

static void OnDestroyDevice(device *dev)
{
    if (g.session_ready && g.dev == reinterpret_cast<ID3D12Device *>(dev->get_native()))
    {
        Log("[carrier] 设备销毁: 进入 dying, 不再碰 NGX");
        gputime::Destroy();   // 计时用的查询堆/回读缓冲跟着设备一起走
        g.dying = true;      // renodx 可能已拆钩, 再 Release 会在外来线程抛 0xE06D7363
        ShutdownSession(dev);
    }
}
} // namespace carrier
