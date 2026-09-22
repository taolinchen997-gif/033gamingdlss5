// =====================================================================
//  inject.h —— 就地插入: 钩住游戏自己的 DLSS 求值, 在那里做神经渲染
//
//  ★为什么要有这一层★
//  我们原来的做法是在 ReShade 的最后一环(reshade_finish_effects)对
//  【交换链后缓冲】做神经渲染。那张图已经色调映射过、叠了后处理、
//  画了 UI、位深也压到 8 位了 —— 给神经渲染喂"成品图"。
//
//  上一版(主插件 renodx)不是这么干的: 它钩住游戏自己的 DLSS 调用,
//  在那里把神经渲染插进去, 拿到的是【游戏内部那张干净的渲染图】——
//  没做色调映射、没叠后处理、没有 UI, 通常还是 HDR 范围。
//
//  同样的模型、同样的参数, 输入不一样, 结果就不一样。用户反馈
//  "不如上个版本惊艳"、"脸部跟没开一样", 参数逐项对齐之后差距还在,
//  剩下的就是这一条。
//
//  ★插在哪一刻★
//  游戏调 NVSDK_NGX_D3D12_EvaluateFeature 做 DLSS 超分 —— 我们让它先跑完,
//  然后在【同一条命令列表】上, 对它刚写出来的 Output 就地做神经渲染。
//  同一条列表 = 不用跨队列同步、不用自己开围栏, 最省事也最安全。
//
//  ★为什么不需要"界面保护"了★
//  这一层没有 UI —— UI 是游戏后面才画的。所以自动遮罩/界面校正在这条路上
//  基本是多余的(留着不碍事)。
//
//  复用: 真正干活的还是 hostnr::Stage —— 它本来就是"在别人的命令列表上,
//  对一张给定的图就地做 NR", 跟这里的需求一模一样。
// =====================================================================
#pragma once

namespace inject
{

static bool     s_on        = false;   // 这条路是不是激活的
static long long s_frames   = 0;
static std::string s_note   = "未启用";

static bool active()      { return s_frames > 0; }
static bool owns_feature(){ return hostnr::feature() != nullptr; }
static const char *note() { return s_note.c_str(); }

// 这条路上的 return 以前全是静默的 —— 挂上了却不出帧时日志一个字没有, 只能猜。
// 照 carrier::BailOnce 的办法, 每种原因只说一次。
static void Why(const char *why)
{
    static std::string last;
    if (last == why) return;
    last = why;
    s_note = why;
    Log("[inject] 没开工: %s", why);
}

// 游戏 DLSS 求值完之后, 由 nrscale 的钩子回调到这里
static int __cdecl AfterGameDlss(ID3D12GraphicsCommandList *cmd, const NVSDK_NGX_Parameter *cp, const NVSDK_NGX_Handle *handle, const nrscale::FeatureInfo *external=nullptr)
{
    struct Attempt {
        bool completed = false;
        ~Attempt() { if (!completed) hostnr::invalidate_history(); }
    } attempt;
    if (!s_on) return 0;
    if (cmd == nullptr)             { Why("钩子给的命令列表是空的"); return 0; }
    if (cp == nullptr)              { Why("钩子给的参数块是空的");   return 0; }
    if (!carrier::cfg.enabled)      { Why("总开关是关的(使用效果快捷键或面板开启)"); return 0; }
    if (carrier::g_inject_timed_out)
    {
        // 超时后若交换链已经建成 feature，本局绝不能再中途换手：旧实测会
        // DXGI_ERROR_DEVICE_REMOVED。只有落在“刚超时、尚未建成”的窄窗口里才撤销回退。
        if (carrier::g_carrier_owned)
        {
            Why("游戏 DLSS 超时后才到；交换链已持有 feature，本局保持交换链路径");
            return 0;
        }
        carrier::g_inject_timed_out = false;
        carrier::g_inject_dead = false;
        carrier::g_inject_grace = 0;
        Log("[inject] 游戏 DLSS 在超时建造前到达：撤销回退，交给就地插入");
    }
    else if (carrier::g_inject_dead)
    {
        Why("就地插入本局已失败，交换链接管");
        return 0;
    }

    NVSDK_NGX_Parameter *p = const_cast<NVSDK_NGX_Parameter *>(cp);

    // 2026-09-12 评论区最大的一类「面板一直等待原生输入、NR 不生效」：NGX 的参数块里 typed 和 untyped 是
    // 【两个槽】—— 游戏用 Set(name, ID3D12Resource*) 放进去的东西，Get(name, void**) 取不出来；DLSS 光线重构
    // 还会把同一张图挂在 "DLSSD." 前缀的别名下。本分支的上游代码早就知道这件事（third_party 的
    // DlssNr_Dx12.cpp:1235-1270 一共试四种），而这里只试了 untyped+主键一种，取到空就在下面静默 return，
    // 于是 nrbeta2 一次都没被写过，面板永远停在初始值。按上游那套取：typed 主键 → typed 别名 → untyped 主键
    // → untyped 别名。取到的只可能是游戏自己放进去的东西，不凭空造。
    // 2026-09-12 傍晚复盘顺序：untyped 主键【必须排第一】。那是 6.0 之前一直在走的路 ——
    // 本来就正常出画的游戏，取到的还是跟从前逐字节相同的东西，这个修复对它们等于没发生。
    // 只有取到空（也就是本来就卡在「等待游戏原生输入」的那些）才继续往下试 typed 和 DLSSD 别名。
    // 反过来把 typed 排前面虽然跟上游一致，却会让所有本来好好的游戏都改走一条没验证过的新路：
    // 万一某个游戏的参数块对 typed 查询返回成功却给了个不相干的指针，就是我们把好游戏弄崩。
    // 修坏的，不碰好的。
    auto ngx_resource = [p](const char *key, const char *alias) -> void * {
        void *value = nullptr;
        ID3D12Resource *typed = nullptr;
        if (p->Get(key, &value) == NVSDK_NGX_Result_Success && value) return value;
        if (p->Get(key, &typed) == NVSDK_NGX_Result_Success && typed) return typed;
        if (alias && p->Get(alias, &value) == NVSDK_NGX_Result_Success && value) return value;
        if (alias && p->Get(alias, &typed) == NVSDK_NGX_Result_Success && typed) return typed;
        return nullptr;
    };

    // ---- 游戏摆在参数块里的资源 ----
    void *out = nullptr, *dep = nullptr, *mvc = nullptr, *exp = nullptr;
    out = ngx_resource(NVSDK_NGX_Parameter_Output,        "DLSSD.Output");
    dep = ngx_resource(NVSDK_NGX_Parameter_Depth,         "DLSSD.Depth");
    mvc = ngx_resource(NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors");
    const bool have_exp =
        p->Get(NVSDK_NGX_Parameter_ExposureTexture, &exp) == NVSDK_NGX_Result_Success && exp != nullptr;
    float pre_exp = 1.0f, exp_scale = 1.0f;
    const bool have_pre =
        p->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &pre_exp) == NVSDK_NGX_Result_Success;
    const bool have_scale =
        p->Get(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, &exp_scale) == NVSDK_NGX_Result_Success;
    if (out == nullptr)
    {
        // ★已经成功干过活就不许把状态覆盖成失败★ (鸣潮实测, 见 nrscale.h 里那段)
        //   开着帧生成的游戏每帧有两次求值, 第二次没有 Output —— 以前它每帧都把
        //   「就地插入成功」擦成「参数块里没有 Output」, 用户看到的诊断自相矛盾:
        //   一边说「一次都没调用」, 一边写着「已处理 1452 帧」。
        if (s_frames == 0 && s_note != "参数块里没有 Output")
        {
            s_note = "参数块里没有 Output";
            // 同一个道理：只在还没成功过的时候写状态，别把「已记录」擦成失败。以前这里直接 return，
            // nrbeta2 一次都没被写过，面板只能打初始值「等待游戏原生输入」，跟「根本没挂上」分不开。
            nrbeta2::Note(nrbeta2::State::NoOutputTexture, nrbeta2::Source::NgxDeclared);
            // 第一次遇到就把这个块里【有什么】记下来 —— 远程排障全靠这几行
            char have[256] = {};
            void *v = nullptr;
            std::snprintf(have, sizeof(have), "深度%s 运动%s 曝光%s 颜色%s",
                p->Get(NVSDK_NGX_Parameter_Depth,         &v) == NVSDK_NGX_Result_Success && v ? "有" : "无",
                (v = nullptr, p->Get(NVSDK_NGX_Parameter_MotionVectors, &v)) == NVSDK_NGX_Result_Success && v ? "有" : "无",
                (v = nullptr, p->Get(NVSDK_NGX_Parameter_ExposureTexture, &v)) == NVSDK_NGX_Result_Success && v ? "有" : "无",
                (v = nullptr, p->Get(NVSDK_NGX_Parameter_Color, &v)) == NVSDK_NGX_Result_Success && v ? "有" : "无");
            Log("[inject] %s —— 这个参数块里: %s", s_note.c_str(), have);
        }
        return 0;
    }

    // ---- 引导图(深度/运动矢量)的尺寸 = 游戏的【渲染】分辨率, 不是输出分辨率 ----
    unsigned rw = 0, rh = 0;
    p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,  &rw);
    p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &rh);
    if (rw == 0 || rh == 0) { p->Get(NVSDK_NGX_Parameter_Width, &rw); p->Get(NVSDK_NGX_Parameter_Height, &rh); }
    if (rw == 0 || rh == 0) {
        // 写一个能分辨的状态：这条说明已经进到游戏的 DLSS 里了，跟"根本没挂上"不是一回事。
        nrbeta2::Note(nrbeta2::State::NoRenderSize, nrbeta2::Source::NgxDeclared);
        Why("拿不到游戏的渲染分辨率"); return 0;
    }

    float sx = 1.0f, sy = 1.0f;
    const bool haveScaleX=p->Get(NVSDK_NGX_Parameter_MV_Scale_X, &sx)==NVSDK_NGX_Result_Success;
    const bool haveScaleY=p->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &sy)==NVSDK_NGX_Result_Success;
    // Preserve the game's declared scale, including zero. Inventing 1 changes
    // the motion contract; missing values retain the explicit defaults above.

    // 深度反转不是单独参数, 是创建标志里的一位
    const nrscale::FeatureInfo info = external ? *external : nrscale::feature_info(handle);
    unsigned flags = info.flags, rs = 0;
    const bool flagsKnown = info.haveFlags ||
        p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &flags) == NVSDK_NGX_Result_Success;
#ifdef K033_BETA2_RESHADE_HOST
    nrbeta2::State admission;
    if(!nrbeta2::NgxInput(dep!=nullptr,mvc!=nullptr,flagsKnown,haveScaleX&&haveScaleY,sx,sy,
        flagsKnown&&(flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)!=0,admission)){
        nrbeta2::Note(admission,nrbeta2::Source::NgxDeclared);Why(nrbeta2::Text(admission));return 0;
    }
    nrbeta2::Note(nrbeta2::State::WaitingModel,nrbeta2::Source::NgxDeclared);
#endif
    if (carrier::cfg.requireguides && (!flagsKnown || dep == nullptr || mvc == nullptr))
    { Why("NGX 引导契约不完整：缺深度、运动矢量或创建标志"); return 0; }
    if (!std::isfinite(sx) || !std::isfinite(sy)) { Why("NGX 运动矢量缩放不是有限数"); return 0; }
    const unsigned di = (flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) ? 1u : 0u;
    float jitterX=0.f,jitterY=0.f;
    const bool haveJitterX=p->Get(NVSDK_NGX_Parameter_Jitter_Offset_X,&jitterX)==NVSDK_NGX_Result_Success;
    const bool haveJitterY=p->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y,&jitterY)==NVSDK_NGX_Result_Success;
    nrgame033::ngxJitterKnown.store(haveJitterX&&haveJitterY&&std::isfinite(jitterX)&&std::isfinite(jitterY));
    nrgame033::ngxMvJittered.store(flagsKnown&&(flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)!=0);
    nrgame033::ngxExposure.store(have_exp);
    nrgame033::ngxPreExposure.store(have_pre&&std::isfinite(pre_exp)&&pre_exp>0.f);
    // Feature 18 has no verified jitter setter in our runtime contract. Keep
    // metadata distinct from inputs actually consumed; do not invent a key or
    // apply exposure twice to the already resolved output.
    p->Get(NVSDK_NGX_Parameter_Reset, &rs);
    // ★游戏的 Reset 默认【不】透传给 feature 18★ 见 carrier.h 的 injreset 注释。
    //   那是游戏【超分】的时域标志, 不是我们这个 feature 的。燕云实测 30% 的帧
    //   带 reset=1, 透传过去等于每三帧清一次 NR 的时域累积 —— 那就是闪烁。
    const unsigned rs_game = rs;
    if (carrier::cfg.injreset == 0) rs = 0;
    (void)rs_game;

    // ★把游戏这块【活的】参数块交给转发器当能力块用★
    //   必须在 Stage 之前交 —— feature 就是在 Stage 里建的, 建的时候才读它。
    //   游戏刚用这块建完自己的 DLSS, 所以它一定是构造完整的; 而核心的能力块
    //   在我们没 Init 过核心的进程里是个空壳, 一喂就崩(详见 nrfwd.h 那段注释)。
    if (carrier::cfg.injcaps != 0 && external == nullptr) nrfwd::use_external_caps(p);

    // ---- 设备从命令列表上拿, 别自己造 ----
    ID3D12Device *dev = nullptr;
    if (FAILED(cmd->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&dev))) || dev == nullptr)
    { Why("从命令列表上拿不到 D3D12 设备"); return 0; }

    // Stage 返 0 有两种: 还在建(正常, 建完就好) / 真出问题(hostnr 自己会记日志)
    // ★画面尺寸必须取 Output 自己的★
    //   rw/rh 是 DLSS_Render_Subrect_Dimensions —— 那是【引导图/输入】的尺寸,
    //   不是 Output 的。拿它当画面尺寸 = 只处理左上角那块
    //   (实测: 古墓丽影只有左上角一个长方形被渲染)。
    ID3D12Resource *outRes = reinterpret_cast<ID3D12Resource *>(out);
    const D3D12_RESOURCE_DESC od = outRes->GetDesc();
    nrcontract::Rect output;
    p->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, &output.x);
    p->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, &output.y);
    output.width = info.outputW; output.height = info.outputH;
    if (!output.width) p->Get(NVSDK_NGX_Parameter_OutWidth, &output.width);
    if (!output.height) p->Get(NVSDK_NGX_Parameter_OutHeight, &output.height);
    if (!output.width && !output.x) output.width = static_cast<unsigned>(od.Width);
    if (!output.height && !output.y) output.height = od.Height;
    const unsigned cw = output.width, ch = output.height;
    auto fits = [](ID3D12Resource *res, const nrcontract::Rect &rect) {
        if (res == nullptr) return false;
        const auto d = res->GetDesc();
        return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.DepthOrArraySize == 1 &&
            d.SampleDesc.Count == 1 && rect.fits(d.Width, d.Height);
    };
    if (!fits(outRes, output)) { Why("NGX 输出区域缺失或越界"); dev->Release(); return 0; }
    nrcontract::Guides guides;
    guides.depth = {0, 0, rw, rh};
    const bool lowMV = (flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    guides.motion = {0, 0, lowMV ? rw : cw, lowMV ? rh : ch};
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &guides.depth.x);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &guides.depth.y);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &guides.motion.x);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &guides.motion.y);
    if ((dep && !fits(reinterpret_cast<ID3D12Resource *>(dep), guides.depth)) ||
        (mvc && !fits(reinterpret_cast<ID3D12Resource *>(mvc), guides.motion)))
    { Why("NGX 深度或运动矢量区域越界"); dev->Release(); return 0; }
    static unsigned observations = 0;
    if (observations == 0) {
        HMODULE module = nullptr; char path[MAX_PATH] = {};
        const auto caller = reinterpret_cast<LPCSTR>(nrdispatch::context.caller);
        if (caller && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, caller, &module))
            GetModuleFileNameA(module, path, MAX_PATH);
        const char *name = std::strrchr(path, '\\');
        Log("[inject-source] NGX caller=%s address=%p handle=%p; module provenance does not verify MV contents",
            path[0] ? (name ? name + 1 : path) : "unknown", caller, handle);
    }
    if (++observations <= 2 || observations % 1800 == 0)
        Log("[inject-contract] source=NGX (native/content not verified) feature=%d flags=%u known=%d "
            "output=(%u,%u %ux%u) depth=%p (%u,%u %ux%u) mv=%p (%u,%u %ux%u) scale=%.4f/%.4f "
            "reset=%u(游戏给的=%u, injreset=%d) jitter_known=%d jitter=%.4f/%.4f mv_jittered=%d exposure_texture=%d pre_exposure=%d",
            info.id, flags, flagsKnown, output.x, output.y, cw, ch,
            dep, guides.depth.x, guides.depth.y, guides.depth.width, guides.depth.height,
            mvc, guides.motion.x, guides.motion.y, guides.motion.width, guides.motion.height, sx, sy,
            rs, rs_game, carrier::cfg.injreset,nrgame033::ngxJitterKnown.load()?1:0,jitterX,jitterY,
            nrgame033::ngxMvJittered.load()?1:0,have_exp?1:0,nrgame033::ngxPreExposure.load()?1:0);
    hostnr::set_guide_rects(guides);
    hostnr::set_output_rect(output);
    hostnr::set_stream_key(reinterpret_cast<uintptr_t>(handle));
    if (nrdispatch::consume_gap()) hostnr::invalidate_history();
    // 只借到 Stage 返回：不 AddRef、不缓存游戏资源。缺帧由 exposure::Meter 的
    // 自有 held_e 兜住，绝不保留上一帧游戏纹理的悬空指针。
    exposure::set_frame(have_exp ? reinterpret_cast<ID3D12Resource *>(exp) : nullptr,
                        pre_exp, have_pre, exp_scale, have_scale);
#ifdef K033_BETA2_RESHADE_HOST
    if(!hostnr::InputCurrent()){exposure::clear_frame();hostnr::clear_guide_rects();hostnr::set_stream_key(0);dev->Release();return 0;}
#endif
    const int r = hostnr::Stage(cmd, dev, outRes,
                                reinterpret_cast<ID3D12Resource *>(dep),
                                reinterpret_cast<ID3D12Resource *>(mvc),
                                cw, ch, sx, sy,
                                static_cast<int>(di), static_cast<int>(rs));
    exposure::clear_frame();
    hostnr::clear_guide_rects();
    hostnr::set_stream_key(0);
    if (r == 0) hostnr::invalidate_history();
    dev->Release();

    if (r == 2) attempt.completed=true; // valid recorded input; semantic output still pending
    if (r == 1)
    {
#ifdef K033_BETA2_RESHADE_HOST
        // This callback is invoked from the successful Evaluate's original
        // parameters, output and command list. The receipt is CPU recording.
        nrbeta2::Recorded(nrbeta2::Source::NgxDeclared,true);
#endif
        attempt.completed = true;
        if (++s_frames == 1)
        {
            // 把真分辨率写进状态里 —— 这是「省了多少像素」最直观的证据
            static char nb[96];
            std::snprintf(nb, sizeof(nb), "游戏的 DLSS 里 · 画面 %ux%u (引导 %ux%u)", cw, ch, rw, rh);
            s_note = nb;
            carrier::g_inject_live = true;   // 交换链那条路从此让位
            Log("[inject] ★就地插入成功★ 在 NGX DLSS 求值之后做神经渲染（来源未证实） "
                "(画面 %ux%u, 引导图 %ux%u, MV 缩放 %.3f/%.3f, 深度反转=%u)", cw, ch, rw, rh, sx, sy, di);
        }
        else if ((s_frames % 1800) == 0)
        {
            Log("[inject] 帧 %lld (画面 %ux%u, 引导 %ux%u)", s_frames, cw, ch, rw, rh);
        }
    }
    return r;
}

static int __cdecl AfterNativeDlss(ID3D12GraphicsCommandList* cmd,const NVSDK_NGX_Parameter* params,const NVSDK_NGX_Handle* handle)
{ return AfterGameDlss(cmd,params,handle); }

// 打开这条路: 装 NGX 钩子 + 注册回调
static bool enable()
{
    if (s_on) return true;

    // Streamline 游戏(燕云/剑星那类: sl.interposer + sl.dlss + 常带 DLSS-D/G)
    //   ★2026-09-03 更正★ 一度以为这类游戏碰不得 —— 建 feature 18 会
    //   seh=0xC0000005、游戏随后就死。后来查清真凶是「feature 18 中途换手」
    //   (交换链先建再交出 = 设备被移除), 跟 Streamline 没关系。
    //   换手改掉之后燕云实测: ★就地插入成功★, ReShade 错误 0, 全程无异常。
    //   所以默认放行; 万一某个 Streamline 游戏还是不行, hostnr 建失败会置
    //   g_inject_dead, 交换链自动接管, 不会两边都不做。
    //   要临时关掉: cfg 里写 injsl=0。
    static const bool streamline = (GetModuleHandleW(L"sl.interposer.dll") != nullptr) ||
                                   (GetModuleHandleW(L"sl.dlss.dll") != nullptr);
    if (streamline && carrier::cfg.injsl == 0)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            s_note = "这游戏走 Streamline, 交给交换链做";
            Log("[inject] 检测到 Streamline, 且 cfg 里 injsl=0 → 不做就地插入, 交换链满分辨率做。");
            carrier::g_inject_dead = true;   // 让交换链别再等着让位
        }
        return false;
    }

    if (!nrscale::install_hooks_for_inject())
    {
        // NGX 核心可能比我们晚加载, 每帧重试 —— 但别每帧都写一行日志
        s_note = nrscale::status_text();
        static std::string last;
        if (last != s_note) { last = s_note; Log("[inject] 暂时装不上: %s (会接着试)", s_note.c_str()); }
        return false;
    }
    nrscale::g_after_eval = &AfterNativeDlss;
    hostnr::s_defer_build = true;   // 就地插入路: feature 必须在 NGX 钩子外面建
    // ★立刻通知交换链让位★ feature 18 只能有一个, 它占着我们就建不出来
    carrier::g_inject_hooked = true;
    s_on   = true;
    s_note = "等游戏调 DLSS…";
    Log("[inject] 已挂到游戏的 DLSS 上, 等它开工");
    return true;
}

} // namespace inject
