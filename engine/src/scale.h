// =====================================================================
//  033 · D3D12 计算着色器缩放 (面积缩小 / Lanczos3 残差放大)
//
//  为什么需要它: ReShade 的 D3D12 后端 copy_texture_region 不会缩放
//  (device_caps::blit=false, d3d12_impl_command_list.cpp 断言 box 同尺寸)。
//  Feeder 的 D3D11 路径用 resample 像素着色器做这件事; 我们在 D3D12 上
//  用一个 8x8 线程组的计算着色器: 采样 SRV(线性钳位) → 写 UAV。
//
//  编译走 d3dcompiler_47.dll 的 D3DCompile(运行时 GetProcAddress, 不加链接依赖)。
//  只在我们自己的命令列表上跑, 资源都是我们自己的, 所以裸用 ResourceBarrier。
// =====================================================================
#pragma once
#include <cmath>
#include <algorithm>
#include <cstring>
#include <type_traits>
#include "pregrade.h"

namespace scale
{
static const char kHlsl[] =
#include "nr_color_hlsl.inl"
#include "pregrade_hlsl.inl"
R"(
Texture2D<float4>   src : register(t0);
Texture2D<float>    live_white : register(t1); // 自有 1x1: 当帧游戏曝光算出的白点
RWTexture2D<float4> dst : register(u0);
SamplerState        lin : register(s0);
cbuffer C : register(b0) { uint2 dst_size; float2 inv_dst; float white; uint encode; uint encmode; float diffuse_white; uint pg_enabled; float pg_exposure; float pg_contrast; float pg_saturation; float pg_warmth; float pg_tint; float pg_highlights; float pg_padding; uint pg_style; float pg_style_strength; };

static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);

float3 LinearToSrgb(float3 v)
{
    v = saturate(v);
    float3 lo = v * 12.92f;
    float3 hi = 1.055f * pow(max(v, 1e-8f), 1.0f / 2.4f) - 0.055f;
    return float3(v.r <= 0.0031308f ? lo.r : hi.r,
                  v.g <= 0.0031308f ? lo.g : hi.g,
                  v.b <= 0.0031308f ? lo.b : hi.b);
}

// 高光滚降, 不硬切。0.75 以上柔和压下去 —— 模型永远不会看到一片死白,
// 而死白的像素在帧与帧之间会翻来翻去: 输入不稳 = 输出不稳 = 屏闪。
float3 SoftKnee(float3 d)
{
    float l = dot(d, kLuma);
    if (l > 0.75f)
    {
        float rolled = 0.75f + 0.25f * (1.0f - exp(-(l - 0.75f) / 0.25f));
        d *= rolled / max(l, 1e-6f);
    }
    // ★逐通道留头, 但不许动色相★
    //   上面那道滞降是按【亮度】做的, 而亮度里蓝色只占 7%。
    //   一个饱和的蓝可以 B=2 而亮度才 0.14, 躲过滞降, 然后被 sRGB
    //   编码里的 saturate【逐通道】裁掉 —— 裁一个通道就是色相旋转,
    //   蓝色于是变青。(上游实测: GTA V 里天空/牛仔布/小地图上那层绿。)
    //   一个标量作用于整个三元组不可能移动色相, 所以把峰值通道
    //   按整体压到 1。只有本来就要被裁的像素会被碰到。
    float peak = max(d.r, max(d.g, d.b));
    if (peak > 1.0f) d /= peak;
    return d;
}

// ══════════════════════════════════════════════════════════════════════════
//  可逆色彩桥 (Neutwo / Hybrid) —— 移植自 RenoDX 的 DLSS5 插件 (clshortfuse),
//  实现照抄 OptiScaler_DLSSNR 的开源复刻(它在 Licenses/RenoDX_ATTRIBUTION.txt
//  里逐条署名了来源)。
//
//  为什么要它: 我们原来的 SoftKnee 在接近白的地方把一切压进一条极窄的带子里,
//  「场景亮度 2」和「场景亮度 4」编码出来只差 0.001 —— 模型根本分辨不出来。
//  Neutwo 把 [0, 无穷) 映到 [0, 1), 没有裁切点, 同样那两个值差 0.076。
//  而且它【可以精确还原】—— 这正是「直接替换」那条路(renodx 4.7 的做法)成立的
//  前提: 编码进去、模型算完、精确解码回来, 中间不需要任何比值。
//
//  一个标量作用在峰值通道上, 所以色相不会被弯 —— 跟 SoftKnee 那段同一个道理。
// ══════════════════════════════════════════════════════════════════════════
float Neutwo(float x) { return x * rsqrt(x * x + 1.0f); }   // [0,inf) -> [0,1), 无裁切点

float3 NeutwoEncode(float3 v)
{
    v = max(v, 0.0f);
    float m = max(v.r, max(v.g, v.b));
    if (m <= 1e-6f) return v;
    return v * (Neutwo(m) / m);
}

// NeutwoEncode 的精确逆: 峰值通道上 y/sqrt(1-y^2)。逆在 1 处发散, 所以峰值夹在
// 1 以下一点点 —— 顶到天花板的高光会解出一个很大但有限的值。
float3 NeutwoDecode(float3 y)
{
    y = max(y, 0.0f);
    float m = max(y.r, max(y.g, y.b));
    m = min(m, 0.999999f);
    if (m <= 1e-6f) return y;
    float x = m * rsqrt(max(1.0f - m * m, 1e-8f));
    return y * (x / m);
}

// 混合曲线: 拐点以下【完全等同】(中间调跟不编码一样好), 拐点以上才用 Neutwo 滚降。
// 参考实现的评价原话: "the best of both, and the one to use" —— 因为纯 Neutwo 虽然
// 救回了高光, 却把中间调也压了; 混合曲线在中间调零损失, 高光该有的层次照样有。
float HybridCurve(float m)
{
    const float k = 0.75f;
    if (m <= k) return m;
    const float e = (m - k) / (1.0f - k);
    return k + (1.0f - k) * (e * rsqrt(e * e + 1.0f));
}

float3 HybridEncode(float3 v)
{
    v = max(v, 0.0f);
    float m = max(v.r, max(v.g, v.b));
    if (m <= 1e-6f) return v;
    return v * (HybridCurve(m) / m);
}

float HybridCurveInv(float y)
{
    const float k = 0.75f;
    if (y <= k) return y;
    float u = (y - k) / (1.0f - k);
    u = min(u, 0.999999f);
    const float e = u * rsqrt(max(1.0f - u * u, 1e-8f));
    return k + (1.0f - k) * e;
}

float3 HybridDecode(float3 y)
{
    y = max(y, 0.0f);
    float m = max(y.r, max(y.g, y.b));
    if (m <= 1e-6f) return y;
    return y * (HybridCurveInv(m) / m);
}

// 按开关选曲线: 0 = 柔和滚降(我们原来的 SoftKnee) / 1 = 可逆桥 Neutwo / 2 = 混合
float3 EncodeCurve(float3 v, uint mode)
{
    if (mode == 1u) return NeutwoEncode(v);
    if (mode == 2u) return HybridEncode(v);
    return SoftKnee(v);
}
float3 DecodeCurve(float3 y, uint mode)
{
    if (mode == 1u) return NeutwoDecode(y);
    if (mode == 2u) return HybridDecode(y);
    return y;      // SoftKnee 没有精确逆 —— 直接替换那条路请用 1 或 2
}

float4 ReadGraded(int2 at) {
    uint sw,sh;src.GetDimensions(sw,sh);at=clamp(at,int2(0,0),int2(sw-1,sh-1));
    float4 v=src.Load(int3(at,0));
    v.rgb=GradeSource(v.rgb,encode==0?1:encode==1?0:encode,diffuse_white,pg_enabled,pg_exposure,pg_contrast,pg_saturation,pg_warmth,pg_tint,pg_highlights,pg_padding,pg_style,pg_style_strength);
    return v;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= dst_size.x || id.y >= dst_size.y) return;

    uint sw, sh;
    src.GetDimensions(sw, sh);

    float4 acc;
    if (sw == dst_size.x && sh == dst_size.y)
    {
        acc = ReadGraded(id.xy);
    }
    else if (sw < dst_size.x)
    {
        float2 uv = (float2(id.xy) + 0.5f) * inv_dst;
        if(pg_enabled!=0) {
            float2 at=uv*float2(sw,sh)-0.5f;int2 lo=(int2)floor(at);float2 a=frac(at);
            acc=lerp(lerp(ReadGraded(lo),ReadGraded(lo+int2(1,0)),a.x),
                     lerp(ReadGraded(lo+int2(0,1)),ReadGraded(lo+int2(1,1)),a.x),a.y);
        } else acc = src.SampleLevel(lin, uv, 0);          // 放大照旧走双线性
    }
    else
    {
        // 精确面积平均, 不是双线性。
        // 双线性缩小只读 4 个纹素、其余全丢, 而且权重取决于采样点落在哪 ——
        // 那是【输入端的锯齿】: 模型看到本来没有的细节、漏掉本来有的,
        // 于是它的答案会随亚像素移动无故改变(画面里什么都没动也在抖)。
        float x0 = (float)id.x       * (float)sw / (float)dst_size.x;
        float x1 = (float)(id.x + 1) * (float)sw / (float)dst_size.x;
        float y0 = (float)id.y       * (float)sh / (float)dst_size.y;
        float y1 = (float)(id.y + 1) * (float)sh / (float)dst_size.y;
        float area = max((x1 - x0) * (y1 - y0), 1e-6f);

        int i0 = (int)floor(x0), i1 = (int)ceil(x1) - 1;
        int j0 = (int)floor(y0), j1 = (int)ceil(y1) - 1;

        float4 sum = float4(0, 0, 0, 0);
        for (int j = j0; j <= j1; ++j)
        {
            int   jj = clamp(j, 0, (int)sh - 1);
            float wy = max(min(y1, (float)j + 1.0f) - max(y0, (float)j), 0.0f);
            for (int i = i0; i <= i1; ++i)
            {
                int   ii = clamp(i, 0, (int)sw - 1);
                float wx = max(min(x1, (float)i + 1.0f) - max(x0, (float)i), 0.0f);
                sum += ReadGraded(int2(ii,jj)) * (wx * wy);
            }
        }
        acc = sum / area;
    }

    if (encode == 0) { dst[id.xy] = acc; return; }

    // 模型是在【显示参考】的图上训练的, 所以喂它之前要按白点归一 + 滚降 + sRGB 编码。
    // 以前把原始线性帧直接喂进去, 亮度一变模型输出就跟着跳。
    // white<0 是内部标记: 白点不在 CPU 常量里，而在自有的 1x1 GPU 纹理里。
    // Encode/Resolve 都读同一张纹理，绝不会隔三四帧各算各的。
    float white_now = (white < 0.0f) ? live_white.Load(int3(0, 0, 0)).r : white;
    float3 c = max(encode == 2 ? NrPqToWorking(acc.rgb, diffuse_white) : acc.rgb, 0.0f);
    if (encode == 3) c *= 80.0f / max(diffuse_white, 1.0f);
    dst[id.xy] = float4(LinearToSrgb(EncodeCurve(c / max(white_now, 1e-4f), encmode)), acc.a);
}
)";

// ---------------------------------------------------------------- 差值合成
//  ★这一趟是画质的关键★
//  错误做法(我们第一版): 把模型的小图直接放大盖回画面 —— 整张图都过了一次
//  75% 往返, 画面必然发软。
//  正确做法(照 OptiScaler DlssNr_Dx12.cpp:1628 的注释): 满分辨率原画一动不动,
//  只取「模型输出 − 模型看到的输入」这个【差值】, 放大后叠上去。
//  放大的是那点修正, 不是图像本身。
//  100% 档时 模型输入 == 原画, 差值 = 输出 − 原画, 结果恰好 == 模型输出,
//  跟直通完全等价 —— 所以这条路在任何档位都是对的。
// ───────── 暗部护栏 / 暗部保色 (写在这里是因为 MSVC 单个字面量上限 16380 字节) ─────────
//// ★越暗, 护栏越紧★
////   护栏本来是个常数(默认 3.0), 亮处暗处一视同仁。问题是: 亮处根本摆不了
////   3 倍(比值条件数好), 而暗处正好相反 —— 路径追踪/夜景里模型每帧的输出
////   本来就在大幅跳动, 3 倍的活动余量全被它用满, 于是静止画面上一片一片
////   地忽明忽暗。观众自己摸出的偏方是「把白点拉到最低」, 那等于把暗部整体
////   抬出噪声区 —— 闪是不闪了, 人脸也一起被冲淡。
////   正确的做法是让护栏跟着亮度走: 亮度趋零时护栏趋于 1(= 亮度一位都不许改),
////   到 kDarkKnee 以上才放开到用户设的值。这跟上面「没有光的像素, 不改才是
////   对的」是同一条原则, 之前只落到了地板上, 没落到护栏上。
////   ★膝点必须在【线性】空间比★: 直通分支(UNORM/SDR 后缓冲, 老游戏那条路的
////   常态)里 original 是 sRGB 编码值, 0.05 编码 ≈ 线性 0.004, 只护到 13/255
////   以下的近黑, SDR 夜景(编码 0.1-0.3)一点没护到 —— 审查抓出来的。所以直通时
////   先按 2.2 幂近似线性化再比; 浮点缓冲本来就是线性(已除白点), 直接用。
////   ★写成着色器内部常量, 不进常量缓冲★ —— resolve 的根常量正好 10 个,
////   动它的数量是有前科的(10->12 当场掉设备)。
////   (数值上: 膝点以上 dk 精确为 1, g 精确等于用户值, 逐位不变 —— 审查逐个
////   float 验过; 强度 0 时无论 g 是多少结果都逐位等于原画。)
//// ★暗部连颜色也别跟模型走★ 上面只钉住了亮度; 颜色方向仍来自模型, 暗处它每帧
////   重新决定 —— 亮度不闪了, 色相还在闪。同一条原则: 越暗越保留游戏自己的颜色。
static const char kHlslResolve[] =
#include "nr_color_hlsl.inl"
#include "pregrade_hlsl.inl"
R"(
Texture2D<float4>   full    : register(t0);   // 满分辨率原画(没被碰过)
Texture2D<float4>   mdl_in  : register(t1);   // 模型看到的代理图(已编码, 可能更小)
Texture2D<float4>   mdl_out : register(t2);   // 模型给出的(已编码, 跟代理图同尺寸)
Texture2D<float>    live_white : register(t3);// 自有 1x1: 与 Encode 同帧、同一个白点
Texture2D<float2> motion_vectors : register(t4);
RWTexture2D<float4> dst     : register(u0);
SamplerState        lin     : register(s0);
cbuffer C : register(b0)
{
    uint2 dst_size; float2 inv_dst;
    float strength;      // 细节强度: 0..2 —— 模型那张图被够到多少
    float white;         // 白点
    float guard;         // 高光护栏: 最多放大/缩小多少倍
    uint  passthrough;   // 1 = 画面已经是显示参考的(SDR), 不编码也不解码
    float colour;        // 颜色强度: 0..1 —— 模型的颜色跟不跟过来
    float sharpen;       // 锐化: 0=关, 0..1
    uint  replace;       // 1 = 直接替换(RenoDX 那条路) / 2 = 差异视图
    uint  resample;      // 模型比画面小时怎么放大: 0 = 双线性(出厂) / 1 = Lanczos3
    uint  encmode;       // 编码曲线: 0 柔和滚降 / 1 可逆桥 Neutwo / 2 混合
    uint  applymodel;    // 0 = 一切照跑但跳过模型(同帧对照基准)
    float diffuse_white;
    float split;         // 分屏对比: 0=关, 0..1=分界线位置(左原画/右处理后)
    uint mas_enabled; float mas_still; float mas_moving; float mas_threshold;
    float2 mas_scale; uint2 mas_origin;
    uint2 mas_extent; float skin_protect; float skin_lift;
    uint pg_enabled; float pg_exposure; float pg_contrast; float pg_saturation; float pg_warmth; float pg_tint; float pg_highlights; float pg_padding; uint pg_style; float pg_style_strength;
};

static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);

float3 SrgbToLinear(float3 v)
{
    v = saturate(v);
    float3 lo = v / 12.92f;
    float3 hi = pow(max((v + 0.055f) / 1.055f, 1e-8f), 2.4f);
    return float3(v.r <= 0.04045f ? lo.r : hi.r,
                  v.g <= 0.04045f ? lo.g : hi.g,
                  v.b <= 0.04045f ? lo.b : hi.b);
}

float3 SoftKnee(float3 d)
{
    float l = dot(d, kLuma);
    if (l > 0.75f)
    {
        float rolled = 0.75f + 0.25f * (1.0f - exp(-(l - 0.75f) / 0.25f));
        d *= rolled / max(l, 1e-6f);
    }
    // ★逐通道留头, 但不许动色相★
    //   上面那道滞降是按【亮度】做的, 而亮度里蓝色只占 7%。
    //   一个饱和的蓝可以 B=2 而亮度才 0.14, 躲过滞降, 然后被 sRGB
    //   编码里的 saturate【逐通道】裁掉 —— 裁一个通道就是色相旋转,
    //   蓝色于是变青。(上游实测: GTA V 里天空/牛仔布/小地图上那层绿。)
    //   一个标量作用于整个三元组不可能移动色相, 所以把峰值通道
    //   按整体压到 1。只有本来就要被裁的像素会被碰到。
    float peak = max(d.r, max(d.g, d.b));
    if (peak > 1.0f) d /= peak;
    return d;
}

// ══════════════════════════════════════════════════════════════════════════
//  可逆色彩桥 (Neutwo / Hybrid) —— 移植自 RenoDX 的 DLSS5 插件 (clshortfuse),
//  实现照抄 OptiScaler_DLSSNR 的开源复刻(它在 Licenses/RenoDX_ATTRIBUTION.txt
//  里逐条署名了来源)。
//
//  为什么要它: 我们原来的 SoftKnee 在接近白的地方把一切压进一条极窄的带子里,
//  「场景亮度 2」和「场景亮度 4」编码出来只差 0.001 —— 模型根本分辨不出来。
//  Neutwo 把 [0, 无穷) 映到 [0, 1), 没有裁切点, 同样那两个值差 0.076。
//  而且它【可以精确还原】—— 这正是「直接替换」那条路(renodx 4.7 的做法)成立的
//  前提: 编码进去、模型算完、精确解码回来, 中间不需要任何比值。
//
//  一个标量作用在峰值通道上, 所以色相不会被弯 —— 跟 SoftKnee 那段同一个道理。
// ══════════════════════════════════════════════════════════════════════════
float Neutwo(float x) { return x * rsqrt(x * x + 1.0f); }   // [0,inf) -> [0,1), 无裁切点

float3 NeutwoEncode(float3 v)
{
    v = max(v, 0.0f);
    float m = max(v.r, max(v.g, v.b));
    if (m <= 1e-6f) return v;
    return v * (Neutwo(m) / m);
}

// NeutwoEncode 的精确逆: 峰值通道上 y/sqrt(1-y^2)。逆在 1 处发散, 所以峰值夹在
// 1 以下一点点 —— 顶到天花板的高光会解出一个很大但有限的值。
float3 NeutwoDecode(float3 y)
{
    y = max(y, 0.0f);
    float m = max(y.r, max(y.g, y.b));
    m = min(m, 0.999999f);
    if (m <= 1e-6f) return y;
    float x = m * rsqrt(max(1.0f - m * m, 1e-8f));
    return y * (x / m);
}

// 混合曲线: 拐点以下【完全等同】(中间调跟不编码一样好), 拐点以上才用 Neutwo 滚降。
// 参考实现的评价原话: "the best of both, and the one to use" —— 因为纯 Neutwo 虽然
// 救回了高光, 却把中间调也压了; 混合曲线在中间调零损失, 高光该有的层次照样有。
float HybridCurve(float m)
{
    const float k = 0.75f;
    if (m <= k) return m;
    const float e = (m - k) / (1.0f - k);
    return k + (1.0f - k) * (e * rsqrt(e * e + 1.0f));
}

float3 HybridEncode(float3 v)
{
    v = max(v, 0.0f);
    float m = max(v.r, max(v.g, v.b));
    if (m <= 1e-6f) return v;
    return v * (HybridCurve(m) / m);
}

float HybridCurveInv(float y)
{
    const float k = 0.75f;
    if (y <= k) return y;
    float u = (y - k) / (1.0f - k);
    u = min(u, 0.999999f);
    const float e = u * rsqrt(max(1.0f - u * u, 1e-8f));
    return k + (1.0f - k) * e;
}

float3 HybridDecode(float3 y)
{
    y = max(y, 0.0f);
    float m = max(y.r, max(y.g, y.b));
    if (m <= 1e-6f) return y;
    return y * (HybridCurveInv(m) / m);
}

// 按开关选曲线: 0 = 柔和滚降(我们原来的 SoftKnee) / 1 = 可逆桥 Neutwo / 2 = 混合
float3 EncodeCurve(float3 v, uint mode)
{
    if (mode == 1u) return NeutwoEncode(v);
    if (mode == 2u) return HybridEncode(v);
    return SoftKnee(v);
}
float3 DecodeCurve(float3 y, uint mode)
{
    if (mode == 1u) return NeutwoDecode(y);
    if (mode == 2u) return HybridDecode(y);
    return y;      // SoftKnee 没有精确逆 —— 直接替换那条路请用 1 或 2
}

// ---- OkLab: 色相校正用 ----
float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0f / 3.0f); }

float3 ToOkLab(float3 c)
{
    const float3x3 rgb2lms = { 0.4122214708f, 0.5363325363f, 0.0514459929f,
                               0.2119034982f, 0.6806995451f, 0.1073969566f,
                               0.0883024619f, 0.2817188376f, 0.6299787005f };
    const float3x3 lms2lab = { 0.2104542553f,  0.7936177850f, -0.0040720468f,
                               1.9779984951f, -2.4285922050f,  0.4505937099f,
                               0.0259040371f,  0.7827717662f, -0.8086757660f };
    return mul(lms2lab, CbrtSigned(mul(rgb2lms, c)));
}

float3 FromOkLab(float3 lab)
{
    const float3x3 lab2lms = { 1.0f,  0.3963377774f,  0.2158037573f,
                               1.0f, -0.1055613458f, -0.0638541728f,
                               1.0f, -0.0894841775f, -1.2914855480f };
    const float3x3 lms2rgb = {  4.0767416621f, -3.3077115913f,  0.2309699292f,
                               -1.2684380046f,  2.6097574011f, -0.3413193965f,
                               -0.0041960863f, -0.7034186147f,  1.7076147010f };
    float3 lms = mul(lab2lms, lab);
    return mul(lms2rgb, lms * lms * lms);
}

// ---- 中性轴色域压缩: 越界了整体往同亮度的中性色拉, 只让饱和度让步, 不动色相 ----
float3 LMSToBT709(float3 c)
{
    const float3x3 m = {  5.62059812f, -4.57145756f,  0.15577924f,
                         -1.15555585f,  2.25800438f, -0.15415806f,
                          0.03059913f, -0.19018011f,  1.06820532f };
    return mul(m, c);
}
float3 BT709ToLMS(float3 c)
{
    const float3x3 m = { 0.30569589f, 0.62271286f, 0.04528636f,
                         0.15776262f, 0.76968599f, 0.08807030f,
                         0.01933082f, 0.11919478f, 0.95053215f };
    return mul(m, c);
}
float3 D65Neutral(float3 lms, float y)
{
    float3 d65 = LMSToBT709(max(lms, 1e-8f));
    float  d65y = max(dot(d65, kLuma), 1e-8f);
    return d65 * (y / d65y);
}
float3 ClampGamut(float3 c)
{
    const float3 lms = BT709ToLMS(float3(0.18f, 0.18f, 0.18f));
    const float  y   = dot(c, kLuma);
    if (!(y > 1e-8f)) return c;
    const float3 nt = D65Neutral(lms, y);
    float k = 1.0f;
    // ★整体一个标量★ 逐通道钳位是色相破坏器: 饱和像素上最小的通道先撞墙,
    //   于是一个不带颜色的修正会落成偏色。
    if (c.r < 0.0f && nt.r > c.r) k = min(k, nt.r / max(nt.r - c.r, 1e-8f));
    if (c.g < 0.0f && nt.g > c.g) k = min(k, nt.g / max(nt.g - c.g, 1e-8f));
    if (c.b < 0.0f && nt.b > c.b) k = min(k, nt.b / max(nt.b - c.b, 1e-8f));
    k = saturate(k);
    if (k >= 1.0f) return c;                 // 本来就在色域里, 一点不动
    return nt + (c - nt) * k;
}

// 色相取模型的, 饱和度大小取被缩放那个的
float3 HueOkLab(float3 incorrect, float3 correct)
{
    float3 a = ToOkLab(incorrect);
    float3 b = ToOkLab(correct);
    float  ca = length(a.yz);
    float  cb = length(b.yz);
    // ★先归一化方向再乘幅度★ 直接用幅度比会炸: 近乎灰的像素色度约 1e-7,
    //   "== 0" 那种精确比较拦不住它, 一除就变成上万倍 —— 夜景里冒绿点就是这么来的。
    float2 dir = cb > 1e-5f ? b.yz / cb : float2(0.0f, 0.0f);
    a.yz = dir * ca;
    return ClampGamut(FromOkLab(a));
}

// ── 对比度自适应锐化 (RCAS) ───────────────────────────────────────────
//  出处: AMD FidelityFX 的 RCAS, 算法是公开的、MIT 许可。这里是照公开描述
//  自己写的实现 —— ★没有抄 OptiScaler 那份 da_das_sharpen.hlsl★, 那个仓库
//  是 GPL, 抄进来会把整个包变成 GPL, 而我们这个包是刻意不带 GPL 组件的。
//
//  为什么锐化【模型输出】而不是最终画面:
//    细节本来就是模型加上去的, 它那张图分辨率还更小(默认 75%), 多采 4 个点
//    很便宜; 而且不用另开一趟全屏 pass。锐化完再走合成, 细节自然带过去。
//
//  "对比度自适应"的意思: 锐化力度按局部对比度自动收敛 —— 平坦区域(天空、
//  墙面)几乎不动, 免得把噪点放大; 边缘附近才真出力。而且有上下限夹着,
//  不会像 UnsharpMask 那样在高对比边上冲出白边(光晕)。
)"
R"(
float3 RcasSharpen(Texture2D<float4> tex, SamplerState smp, float2 uv, float2 texel,
                   float3 e, float amount)
{
    if (amount <= 0.001f) return e;

    // 十字五点
    float3 b = tex.SampleLevel(smp, uv + float2(0.0f, -texel.y), 0).rgb;
    float3 d = tex.SampleLevel(smp, uv + float2(-texel.x, 0.0f), 0).rgb;
    float3 f = tex.SampleLevel(smp, uv + float2( texel.x, 0.0f), 0).rgb;
    float3 h = tex.SampleLevel(smp, uv + float2(0.0f,  texel.y), 0).rgb;

    // ★带钳位的反锐化掩模★
    //   第一版我照记忆写 AMD 的 RCAS 瓣长公式, 推错了 —— hitMax 化简下来是
    //   常数 -0.25, 等于整条自适应逻辑没起作用, 拉滑杆看不出任何变化(业主实测)。
    //   与其写一个自己推不明白的公式, 不如用这个: 数学上完全透明, 而且
    //   防光晕的性质是【钳位】给的, 不是靠瓣长公式凑出来的。
    //
    //   1) 减掉邻域均值 = 只留下局部高频(细节), 平坦区域这一项天然是 0,
    //      所以天空、墙面几乎不动 —— 这就是"对比度自适应"想要的效果。
    //   2) 结果死死夹在四邻的 min/max 之间: 锐化永远不会让一个像素比它周围
    //      最亮的还亮、比最暗的还暗 —— 高对比边上冲不出白边。
    float3 mn = min(min(b, d), min(f, h));
    float3 mx = max(max(b, d), max(f, h));
    float3 blur = (b + d + f + h) * 0.25f;

    // amount 1.0 → 系数 1.5, 是个够看得见但不过火的上限
    float3 sharp = e + (e - blur) * (amount * 1.5f);
    return clamp(sharp, min(mn, e), max(mx, e));
}

)"
R"(

// ── Lanczos3: 只用来放大模型残差的两张匹配小图 ──────────────────────────
//  原画从不经过这个滤镜；mdl_out 与 mdl_in 用完全相同的核分别放大，随后才相减，
//  因而缩放本身带来的模糊会互相抵消，不会被误当成模型编辑铺回满分辨率。
float Sinc(float x)
{
    x = abs(x);
    if (x < 1e-5f) return 1.0f;
    const float pix = 3.14159265358979323846f * x;
    return sin(pix) / pix;
}
float Lanczos3Weight(float x)
{
    x = abs(x);
    return x < 3.0f ? Sinc(x) * Sinc(x / 3.0f) : 0.0f;
}
float3 SampleLanczos3(Texture2D<float4> tex, float2 uv)
{
    uint w, h; tex.GetDimensions(w, h);
    float2 p = uv * float2(w, h) - 0.5f;
    int2 base = int2(floor(p));
    float3 sum = 0.0f;
    float wsum = 0.0f;
    [unroll] for (int j = -2; j <= 3; ++j)
    {
        const float wy = Lanczos3Weight(p.y - (float)(base.y + j));
        const int yy = clamp(base.y + j, 0, (int)h - 1);
        [unroll] for (int i = -2; i <= 3; ++i)
        {
            const float ww = Lanczos3Weight(p.x - (float)(base.x + i)) * wy;
            const int xx = clamp(base.x + i, 0, (int)w - 1);
            sum += tex.Load(int3(xx, yy, 0)).rgb * ww;
            wsum += ww;
        }
    }
    return abs(wsum) > 1e-6f ? sum / wsum : 0.0f;
}

)"
#include "nr_effect_hlsl.inl"
#include "nr_skin_lift_hlsl.inl"
#include "nr_clarity_hlsl.inl"
#include "nr_natural_hlsl.inl"
R"(

// 把残差整体缩放到不出单位立方体, 方向不变(逐通道钳会弯色相)
float3 CubeScaleResidual(float3 P, float3 T)
{
    float3 d = T - P;
    float a = 1.0f;
    [unroll] for (int c = 0; c < 3; ++c)
    {
        if      (d[c] >  1e-6f) a = min(a, (1.0f - P[c]) / d[c]);
        else if (d[c] < -1e-6f) a = min(a, (0.0f - P[c]) / d[c]);
    }
    return P + saturate(a) * d;
}
)"
#include "nr_stack_input_hlsl.inl"
R"(
[numthreads(8, 8, 1)]
void stack_main(uint3 id : SV_DispatchThreadID,uint3 threadId : SV_GroupThreadID,uint3 groupId : SV_GroupID)
{
    StackDispatch(id.xy,threadId.xy,groupId.xy);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID,uint3 threadId : SV_GroupThreadID,uint3 groupId : SV_GroupID)
{
    if (id.x >= dst_size.x || id.y >= dst_size.y) return;
    float2 uv = (float2(id.xy) + 0.5f) * inv_dst;

    float4 raw4 = full[id.xy];
    if(replace==5u){dst[id.xy]=FinalPicturePixel(id.xy,raw4);return;}
    if(replace==3){
        // Lift the COMPLETE RGB model edit onto the full-resolution prior.
        // At equal resolution/strength=1 this reproduces the model answer;
        // at lower resolution it preserves prior detail plus the model edit.
        // Do not high-pass away facial shading or use motion to erase NR.
        float3 prior=raw4.rgb;
        float3 input=mdl_in.SampleLevel(lin,uv,0).rgb;
        float3 answer=mdl_out.SampleLevel(lin,uv,0).rgb;
        if(applymodel==0 || strength<=0 || any(!isfinite(answer)) || any(!isfinite(input))){dst[id.xy]=raw4;return;}
        float3 edit=(answer-input)*clamp(strength,0,2);
        if(all(edit==0)){dst[id.xy]=raw4;return;}
        float3 result=CubeScaleResidual(saturate(prior),saturate(prior)+edit);
        dst[id.xy]=float4(result,raw4.a);return;
    }
    float4 orig4=raw4;
    orig4.rgb=GradeSource(raw4.rgb,passthrough,diffuse_white,pg_enabled,pg_exposure,pg_contrast,pg_saturation,pg_warmth,pg_tint,pg_highlights,pg_padding,pg_style,pg_style_strength);

    // ★不是加法差值★
    //   以前: dst = 原画 + (模型输出 − 模型输入) × 强度。
    //   那样会「丢掉模型在高光里的行为, 而且让所有档位看起来都一个样」——
    //   预设 1/2/3 没区别就是这么来的。
    //   正确做法: 模型给的是一张【完整的图】, 按原画亮度该在的位置把它缩放回来。
    // ★测量工具★ 抄自 OptiScaler(DlssNrApplyModel / DlssNrCompare)。
    //   它的设计文档原话: 「两样同时在变的时候, 你分不清是设置的效果还是场景的效果」。
    //   ① 跳过模型: 一切照跑, 只是不把模型的结果放回去 —— 真正的对照基准
    if (applymodel == 0) { dst[id.xy] = orig4; return; }
    //   ② 分屏对比: 左半原画 / 右半处理后, 中间一条细白线
    if (split > 0.001f)
    {
        const float bx = split * (float)dst_size.x;
        if (abs((float)id.x - bx) < 1.0f)      // 分界线
        {
            dst[id.xy] = float4(NrStore(float3(1,1,1), passthrough, diffuse_white), orig4.a);
            return;
        }
        if ((float)id.x < bx) { dst[id.xy] = raw4; return; }
    }

    const float3 sourceRgb = orig4.rgb;
    if (passthrough == 2) orig4.rgb = NrPqToWorking(orig4.rgb, diffuse_white);
    if (passthrough == 3) orig4.rgb *= 80.0f / max(diffuse_white, 1.0f);
    uint pw, ph; mdl_in.GetDimensions(pw, ph);
    uint mw, mh; mdl_out.GetDimensions(mw, mh);
    const bool scaled = (pw != dst_size.x || ph != dst_size.y);
)"
R"(
    // ★模型比画面小时, 用什么把它抬回满分辨率★
    //   出厂 = 硬件双线性。这不是"图省事", 是全生态的一致做法:
    //     OptiScaler  —— D3D12_FILTER_MIN_MAG_MIP_LINEAR, 一次 SampleLevel
    //     DLSS5-Feeder —— work_upscale 默认 0 = 双线性
    //     hhkbble 的设计文档 Non-goals 直接写死: "Do not EASU / Lanczos the model RGB"
    //   我们以前用 Lanczos3, 是全生态唯一一个。它确实更锐, 代价是负瓣过冲 ——
    //   参考实现对这件事的原话: 锐利滤波器会「把模型的细节混叠成噪点」
    //   (aliases the model's detail into noise)。业主实测反馈就是"有噪点"。
    //   要回去比一比: cfg 里加一行 resample=1。
    float3 proxy, model;
    if (!scaled && mw == pw && mh == ph)
    {
        proxy = mdl_in.Load(int3(id.xy, 0)).rgb;
        model = mdl_out.Load(int3(id.xy, 0)).rgb;
    }
    else if (resample != 0 || pw > dst_size.x)
    {
        // ★模型【比画面大】时(超采样档)一律走 Lanczos3, 不管开关怎么设★
        //   参考实现实测过: 把 Nx 的模型结果用一次双线性点采缩回显示尺寸,
        //   会「把模型的细节混叠成噪点」—— 那正是他们量到的"超过 100% 反而更噪"。
        //   缩小这条腿需要的是真正的重采样, 不是一次点采。
        proxy = SampleLanczos3(mdl_in,  uv);
        model = SampleLanczos3(mdl_out, uv);
    }
    else
    {
        proxy = mdl_in.SampleLevel(lin,  uv, 0).rgb;
        model = mdl_out.SampleLevel(lin, uv, 0).rgb;
    }
    // 必须在残差重建之前记住模型有没有真的给答案。否则空输出减代理图后留下的
    // 高频边缘会被当成“模型编辑”，反而让空帧变成一张怪图。
    const float modelSignal = dot(saturate(model), kLuma);

    const float skinGuard = saturate(skin_protect) * SkinWeightLinear(EffectTo709(sourceRgb,passthrough,diffuse_white));
    // 锐化(在解码之前做 —— 此刻是显示参考的, 正是锐化假设的空间)
    // 锐化只是低工作分辨率的伴生补偿。100% 时模型没有被放大，不默认再锐一遍。
    float shp = scaled ? sharpen : 0.0f;
    // Independent 033 implementation of motion-adaptive strength. Motion is
    // converted to output pixels per real frame; this adds no temporal history.
    if (mas_enabled != 0)
    {
        uint2 at = mas_origin + min((uint2)(uv * mas_extent), mas_extent - 1);
        float2 velocity = motion_vectors.Load(int3(at, 0)) * mas_scale;
        float speed = length(velocity);
        shp = all(isfinite(velocity)) && isfinite(speed)
            ? lerp(mas_still, mas_moving, smoothstep(0.0f, mas_threshold, speed)) : 0.0f;
    }
    shp *= 1.0f - 0.75f * skinGuard;
    if (shp > 0.001f)
    {
        if (pw > 0 && ph > 0)
            model = RcasSharpen(mdl_out, lin, uv, float2(1.0f / mw, 1.0f / mh), model, shp);
    }
    // Preserve identity before nonlinear colour math. NVIDIA's native shader
    // optimizer can otherwise evaluate matching PQ conversions with a few ULPs
    // of difference, although WARP happens to cancel them exactly.
    if (replace == 1 && all(model == proxy)) { dst[id.xy] = float4(sourceRgb, orig4.a); return; }
    if (passthrough != 1) { proxy = SrgbToLinear(proxy); model = SrgbToLinear(model); }

    // 三张图必须在同一个尺度上才能比亮度。代理图和模型输出解码回来是 0..1(1=白点),
    // 而原画是原始线性、可以远超 1。不归一化就是真 bug, 表现【正好像模型不加细节了】。
    float white_now = (white < 0.0f) ? live_white.Load(int3(0, 0, 0)).r : white;
    const float normScale = (passthrough == 1) ? 1.0f : max(white_now, 1e-4f);
    float3 original = orig4.rgb / normScale;

    float origLuma  = dot(original, kLuma);
    float proxyLuma = dot(proxy,    kLuma);

    // 模型跑在小尺寸时: 原画自己的代理图在满分辨率重建一遍(编码是纯函数, 可复现),
    // 只把模型的【差值】从小图抬上来 —— 交给合成的两张都是满分辨率的。
    if (scaled || replace == 1)
    {
        // SDR/UNORM 直通帧已经是显示参考，不能再套一遍高光编码；浮点 HDR 才复现
        // 编码 pass 的 SoftKnee。两路都只构造匹配代理，不改满分辨率原画。
        // ★重建代理图必须用【同一条曲线】★ 编码 pass 用哪条, 这里就得用哪条,
        //   否则"模型的改动"里会混进两条曲线的差, 那不是模型干的。
        float3 fullProxy = (passthrough == 1) ? saturate(original) : saturate(EncodeCurve(original, encmode));
        float3 edit = model - proxy;
        proxy     = fullProxy;
        proxyLuma = dot(proxy, kLuma);
        model     = CubeScaleResidual(fullProxy, fullProxy + edit);
    }

    // Decode the model edit with bounded response. Raw inverse replacement was
    // unstable at the highlight ceiling (confirmed by the sensitivity test).
    // Identity compensation alone does not bound nonzero model edits.
    if (replace == 1)
    {
        // Black is a valid per-pixel model answer. A near-black fallback causes
        // a discontinuity; a missing whole frame needs separate validation.
        if (strength <= 0.0f || any(!isfinite(model)))
        { dst[id.xy] = float4(sourceRgb, orig4.a); return; }
        float3 pure = (passthrough == 1) ? model : DecodeCurve(model, encmode);
        float3 baseline = (passthrough == 1) ? proxy : DecodeCurve(proxy, encmode);
        float3 edit = (pure - baseline) * normScale;
        float luma = dot(max(orig4.rgb, 0.0f), kLuma);
        float3 lumaEdit = luma > 1e-6f ? max(orig4.rgb, 0.0f) * (dot(edit, kLuma) / luma) : 0.0f;
        float amount = clamp(strength, 0.0f, 2.0f);
        edit = lerp(lumaEdit, edit, saturate(colour) * (1.0f - 0.75f * skinGuard));
        if (any(!isfinite(edit))) { dst[id.xy] = float4(sourceRgb, orig4.a); return; }
        edit = NrBoundEdit(orig4.rgb, edit, model - proxy, proxy, guard);
        // Blend the already-bounded edit so intermediate slider positions do
        // not all hit the same ceiling. Recheck bounds when extrapolating.
        edit *= amount;
        if (amount > 1.0f)
            edit = NrBoundEdit(orig4.rgb, edit, (model - proxy) * amount, proxy, guard);
        if (all(edit == 0.0f) && skin_lift <= 0.0f) { dst[id.xy] = float4(sourceRgb, orig4.a); return; }
        // Subtract the same colour-space conversion to retain exact source
        // identity, including original negative gamut channels and FP16 rounding.
        float3 result;
        if (passthrough == 2)
        {
            // Inverse gamut compression can amplify coloured HDR edits again.
            // Enforce the same budget in destination BT.2020 linear nits, not
            // merely in the intermediate compressed BT.709 working space.
            float3 originalNits = NrPqToNits(sourceRgb);
            float3 changedNits = NrPqToNits(NrStore(orig4.rgb + edit, 2, diffuse_white));
            float3 baselineNits = NrPqToNits(NrStore(orig4.rgb, 2, diffuse_white));
            float3 nitsEdit = NrBoundEdit(originalNits, changedNits - baselineNits,
                                         (model - proxy) * amount, proxy, guard);
            result = sourceRgb + (NrNitsToPq(originalNits + nitsEdit) - NrNitsToPq(originalNits));
        }
        else
        {
            float3 delta = NrStore(orig4.rgb + edit, passthrough, diffuse_white)
                         - NrStore(orig4.rgb, passthrough, diffuse_white);
            result = sourceRgb + delta;
        }
        // Both final compose modes honour the same existing panel control.
        // Layer-internal calls pass zero, so this executes only at final output.
        if (all(isfinite(result))) result = SkinLiftOutput(result,passthrough,diffuse_white,skin_lift);
        // The legacy dark-knee branch is bypassed by replace=1. Protect the
        // final physical-light edit here, including curve 3 and skin lift.
        if (all(isfinite(result))) result = EffectShadowOutput(sourceRgb,result,passthrough,diffuse_white,white_now,guard);
        if (any(!isfinite(result))) result = sourceRgb;
        dst[id.xy] = float4(passthrough == 2 ? saturate(result) : result, orig4.a);
        return;
    }
    // ★合成方式三: 差异视图(调试用)★
    //   把「模型改了多少」直接画出来 —— |模型 - 代理图| 放大 10 倍。
    //   全黑 = 模型什么都没做(那画质问题就跟合成无关, 得往模型那边查);
    //   看得见轮廓/皮肤纹理 = 模型在干活, 那问题就出在我们怎么把它贴回去。
    //   这是唯一不靠眼睛猜的判据。
    if (replace == 2)
    {
        float3 d = abs(model - proxy) * 10.0f;
        dst[id.xy] = float4(NrStore(saturate(d) * normScale, passthrough, diffuse_white), orig4.a);
        return;
    }

    float modelLuma = dot(model, kLuma);
    float3 upgraded;

    if (modelSignal <= 1e-5f || modelLuma <= 1e-5f)
    {
        upgraded = original;                 // 模型返回空图时原样交还, 别塌成黑
    }
    else
    {
        float ratio;
        if (origLuma < proxyLuma)
            ratio = origLuma / max(proxyLuma, 1e-6f);          // 代理图之下: 以原画亮度为目标
        else
            ratio = (modelLuma + max(0.0f, origLuma - proxyLuma)) / modelLuma;  // 之上: 那是模型没见过的余量, 加回去
        upgraded = lerp(original, HueOkLab(model * ratio, model), saturate(strength));
    }

    float upLuma = dot(upgraded, kLuma);

    // ★暗部地板★ 暗像素的比值是无界的: 一点点绝对修正就变成巨大比值, 撞上护栏把
    //   那个像素亮度翻倍, 下一帧又落回去 —— 那就是「沸腾」: 静止的几何上爬着一片
    //   一片发亮的斑, 画面越暗越明显。分子分母同加一个地板, 亮处几乎不受影响,
    //   而亮度趋零时比值平滑地趋于 1(没有光的像素, 不改才是对的)。
    const float kRatioFloor = 1.0f / 512.0f;
    float lumaRatio = (upLuma + kRatioFloor) / (origLuma + kRatioFloor);

    // 强度超过 1 走指数, 而不是把混合外推 —— 结果仍然是个比值, 才能被下面的护栏管住
    float amplified = pow(max(lumaRatio, 1e-6f), 1.0f + max(strength - 1.0f, 0.0f));

    // ★高光护栏★ 一个细节增强没有理由去重塑一个光源, 不管模型返回什么。
    //   双边的: 暗场景里 knee 不触发、比值退化成 1, 合成就等于模型自己那张图,
    //   于是【模型每帧重新决定的结果整张到达】—— 那是另一种屏闪。
    //   一个标量作用于整个三元组, 绝不逐通道。
    // 越暗护栏越紧 —— 原理见字面量上方的 C++ 注释「暗部护栏」
    const float kDarkKnee = 0.05f;
    float lumaForKnee = (passthrough == 1) ? pow(max(origLuma, 0.0f), 2.2f) : origLuma;
    float dk = saturate(lumaForKnee / kDarkKnee);
    dk = dk * dk * (3.0f - 2.0f * dk);              // smoothstep, 别在边界上出硬边
    float g = 1.0f + (max(guard, 1.0f) - 1.0f) * dk;
    float bounded = clamp(amplified, 1.0f / g, g);
    // 比值本来就在护栏内时正好乘 1, 所以「本不需要夹」的帧一位都不动
    upgraded *= bounded / max(lumaRatio, 1e-6f);

    // ★两个强度, 各管一件事★
    //   细节强度(上面的 strength): 模型那张图被够到多少
    //   颜色强度(这里的 colour):   模型的颜色跟不跟过来
    //     0 = 画面保持游戏自己的色相, 只让光影带上模型的判断
    //     1 = 模型的颜色也一起到达(默认)
    //   两端现在都在同一个护栏里, 所以中间任何一点也都在, 不用再夹第二次。
    // 暗部连颜色也不跟模型走(同一原则), 见上方 C++ 注释
    float colourEff = saturate(colour) * dk * (1.0f - 0.75f * skinGuard);
    float3 result = lerp(original * bounded, upgraded, colourEff) * normScale;
    float3 stored = NrStore(max(result, 0.0f), passthrough, diffuse_white);
    stored = SkinLiftOutput(stored,passthrough,diffuse_white,skin_lift);
    stored = EffectShadowOutput(sourceRgb,stored,passthrough,diffuse_white,white_now,guard);
    dst[id.xy] = float4(stored, orig4.a);
}
)";

// ---------------------------------------------------------------- 帧插值
//  在 prev 和 cur 中间造一张。运动矢量的约定跟 DLSS5_Feed 一致:
//  mv 是【像素】单位, 指向"这个像素在上一帧的位置"(prev_uv = uv + mv)。
//  所以中间帧上某点 q: 它在 prev 里在 q + 0.5*mv, 在 cur 里在 q - 0.5*mv。
//  两边各采一次再混合。
//  遮挡处理: 两边采到的颜色差太大说明这块被遮住/露出来了, 这时不混合,
//  直接偏向 cur —— 宁可少一点插值, 也不要糊成一团。
static const char kHlslInterp[] = R"(
Texture2D<float4>   prv : register(t0);
Texture2D<float4>   cur : register(t1);
Texture2D<float2>   mvt : register(t2);
RWTexture2D<float4> dst : register(u0);
SamplerState        lin : register(s0);
cbuffer C : register(b0) { uint2 size; float2 inv; float phase; float reject; float2 pad; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    float2 uv = (float2(id.xy) + 0.5f) * inv;
    float2 mv = mvt.SampleLevel(lin, uv, 0) * inv;      // 像素 -> UV

    float4 a = prv.SampleLevel(lin, uv + mv * phase, 0);
    float4 b = cur.SampleLevel(lin, uv - mv * (1.0f - phase), 0);

    float d = abs(a.r - b.r) + abs(a.g - b.g) + abs(a.b - b.b);
    float w = saturate(1.0f - d / max(reject, 1e-4f));  // 差得越大越不混
    dst[id.xy] = lerp(b, lerp(a, b, phase), w);
}
)";

typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

// ---------------------------------------------------------------- 模型输入的时域稳定
//  ★为什么是【输入】不是【输出】★
//   有人用照相模式把场景完全冻住, 连续 8 帧逐像素量亮度标准差, 结论是:
//   闪不是模型的重投影出问题, 是「它对一张在脚下微微移动的输入图, 每帧重新做
//   一次空间判断」。所以对模型的【答案】做时域滤波是死路 —— 上游测过两次
//   (含一个训练好的 DLAA pass), 原话是「模型随取景重新决定细节, 旧答案不属于新帧」。
//   要治就得治因: 把喂进去的那张图先稳住, 它的答案自然就稳了。
//
//  ★为什么这条路在我们这儿可能成、在他们那儿不成★
//   他们把模型的答案【替换】上屏, 输入糊 → 画面糊, 实测「抖动减半但眼睛更喜欢原始渲染」。
//   我们是【比值合成】: 模型只产出「这个像素该亮多少倍」, 乘回一动没动过的原画。
//   输入糊掉, 糊的是那个倍率(增强层), 不是画面本身 —— 原图锐度一根头发都不少。
//
//  做法就是一个最朴素的 TAA, 只不过跑在模型输入上:
//   prev_uv = uv + mv(约定跟帧插值那趟一致: mv 指向"这个像素在上一帧的位置")
//   历史先按当前帧 3x3 邻域的最小/最大值夹一遍 —— 这一步是防拖影的命根子,
//   顺带让"万一 mv 符号反了"退化成"几乎等于当前帧"而不是糊成一片。
static const char kHlslStab[] = R"(
Texture2D<float4>   hist : register(t0);
Texture2D<float4>   cur  : register(t1);
Texture2D<float4>   mvt  : register(t2);
RWTexture2D<float4> dst  : register(u0);
SamplerState        lin  : register(s0);
cbuffer C : register(b0)
{
    uint  w, h;
    float inv_w, inv_h;
    float alpha;      // 当前帧权重: 1 = 完全不稳定化
    float kx, ky;     // mv 纹素值 -> UV 的换算 (mvScale / 引导图尺寸)
    float pad0, pad1;
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= w || id.y >= h) return;
    float4 c = cur[id.xy];
    if (alpha >= 0.999f) { dst[id.xy] = c; return; }

    float2 uv = (float2(id.xy) + 0.5f) * float2(inv_w, inv_h);
    float2 mv = mvt.SampleLevel(lin, uv, 0).xy;
    float2 prev_uv = uv + float2(mv.x * kx, mv.y * ky);
    if (prev_uv.x < 0.0f || prev_uv.x > 1.0f || prev_uv.y < 0.0f || prev_uv.y > 1.0f)
    { dst[id.xy] = c; return; }

    float3 lo = c.rgb, hi = c.rgb;
    [unroll] for (int j = -1; j <= 1; ++j)
    {
        [unroll] for (int i = -1; i <= 1; ++i)
        {
            int2 p = clamp(int2(id.xy) + int2(i, j), int2(0, 0), int2(int(w) - 1, int(h) - 1));
            float3 sm = cur[p].rgb;
            lo = min(lo, sm); hi = max(hi, sm);
        }
    }
    float3 hc = hist.SampleLevel(lin, prev_uv, 0).rgb;
    hc = clamp(hc, lo, hi);
    dst[id.xy] = float4(lerp(hc, c.rgb, alpha), c.a);
}
)";

struct Blitter
{
    ID3D12RootSignature *rs   = nullptr;
    ID3D12PipelineState *pso  = nullptr;
    ID3D12RootSignature *rs_rv  = nullptr;  // 差值合成用(3 个图像 SRV + 可选白点 SRV + 1 个 UAV)
    ID3D12PipelineState *pso_rv = nullptr;
    ID3D12PipelineState *pso_stack = nullptr;
    ID3D12PipelineState *pso_ip = nullptr;  // 帧插值(跟差值合成共用根签名: 3 SRV + 1 UAV + 8 常量)
    ID3D12PipelineState *pso_st = nullptr;  // 模型输入的时域稳定(同一根签名: 3 SRV + 1 UAV)
    // shader-visible 堆: [0]=SRV A [1]=UAV A [2]=SRV B [3]=UAV B
    //                    [4..6]=resolve 的三个 SRV  [7]=resolve 的 UAV
    //                    [12]=自有 1x1 动态白点 SRV(创建一次后不在飞行中改描述符)
    ID3D12DescriptorHeap *heap = nullptr;
    ID3D12Resource *white_bound = nullptr; // 裸指针只用于判定 slot 12 是否已建，不持有资源
    UINT inc = 0;
    bool ready = false;
    std::string error;
};

template <typename T> static void Rel(T *&p) { if (p) { p->Release(); p = nullptr; } }

static void Destroy(Blitter &b)
{
    Rel(b.pso); Rel(b.rs); Rel(b.pso_rv); Rel(b.pso_stack); Rel(b.pso_ip); Rel(b.pso_st); Rel(b.rs_rv); Rel(b.heap);
    b.white_bound = nullptr;
    b.ready = false;
}

static bool Create(Blitter &b, ID3D12Device *dev)
{
    Destroy(b);
    HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll");
    if (dc == nullptr) dc = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dc ? reinterpret_cast<PFN_D3DCompile>(GetProcAddress(dc, "D3DCompile")) : nullptr;
    if (compile == nullptr) { b.error = "找不到 d3dcompiler_47.dll / D3DCompile"; return false; }

    ID3DBlob *cs = nullptr, *err = nullptr;
    HRESULT hr = compile(kHlsl, sizeof(kHlsl) - 1, "033_scale", nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, &err);
    if (FAILED(hr))
    {
        b.error = err ? std::string(static_cast<const char *>(err->GetBufferPointer()), err->GetBufferSize()) : "着色器编译失败";
        Rel(err);
        return false;
    }
    Rel(err);

    // 根签名: [0] 常量 6 DWORD, [1] SRV t0, [2] UAV u0, [3] 可选白点 SRV t1。
    // 白点单独一张表，避免打乱沿用多年的 src/uav 描述符布局。
    D3D12_DESCRIPTOR_RANGE r_srv = {}; r_srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r_srv.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE r_uav = {}; r_uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; r_uav.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE r_white = {}; r_white.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r_white.NumDescriptors = 1; r_white.BaseShaderRegister = 1;
    D3D12_ROOT_PARAMETER p[4] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.Num32BitValues = 18; p[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[1].DescriptorTable.NumDescriptorRanges = 1; p[1].DescriptorTable.pDescriptorRanges = &r_srv; p[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable.NumDescriptorRanges = 1; p[2].DescriptorTable.pDescriptorRanges = &r_uav; p[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[3].DescriptorTable.NumDescriptorRanges = 1; p[3].DescriptorTable.pDescriptorRanges = &r_white; p[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC smp = {};
    smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.ShaderRegister = 0; smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 4; rsd.pParameters = p; rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &smp;

    // 不静态链 d3d12.lib: addon 是被 ReShade 的 dxgi.dll 代理在进程极早期加载的, 多一个静态导入
    // 就多一个加载顺序变数。游戏此时早已加载 d3d12.dll, 直接从它身上取函数。
    typedef HRESULT (WINAPI *PFN_Serialize)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    auto serialize = d12 ? reinterpret_cast<PFN_Serialize>(GetProcAddress(d12, "D3D12SerializeRootSignature")) : nullptr;
    if (serialize == nullptr) { b.error = "进程里没有 d3d12.dll / D3D12SerializeRootSignature"; Rel(cs); return false; }
    ID3DBlob *sig = nullptr;
    hr = serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) { b.error = "根签名序列化失败"; Rel(err); Rel(cs); return false; }
    Rel(err);
    hr = dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&b.rs));
    Rel(sig);
    if (FAILED(hr)) { b.error = "根签名创建失败"; Rel(cs); return false; }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = b.rs; pd.CS.pShaderBytecode = cs->GetBufferPointer(); pd.CS.BytecodeLength = cs->GetBufferSize();
    hr = dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&b.pso));
    Rel(cs);
    if (FAILED(hr)) { b.error = "PSO 创建失败"; return false; }

    // ---- 差值合成的根签名 + PSO ----
    // 表 0: t0..t2 三个图像 SRV, 表 1: u0, 表 2: 可选白点 t3。常量仍是 10 DWORD。
    {
        ID3DBlob *cs2 = nullptr, *e2 = nullptr;
        hr = compile(kHlslResolve, sizeof(kHlslResolve) - 1, "033_resolve", nullptr, nullptr,
                     "main", "cs_5_0", 0, 0, &cs2, &e2);
        if (FAILED(hr))
        {
            b.error = e2 ? std::string(static_cast<const char *>(e2->GetBufferPointer()), e2->GetBufferSize())
                         : "差值着色器编译失败";
            Rel(e2); return false;
        }
        Rel(e2);

        D3D12_DESCRIPTOR_RANGE r3 = {}; r3.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r3.NumDescriptors = 3;
        D3D12_DESCRIPTOR_RANGE r1 = {}; r1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; r1.NumDescriptors = 1;
        D3D12_DESCRIPTOR_RANGE rw = {}; rw.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rw.NumDescriptors = 1; rw.BaseShaderRegister = 3;
        D3D12_DESCRIPTOR_RANGE rm = rw; rm.BaseShaderRegister = 4;
        D3D12_ROOT_PARAMETER q[5] = {};
        q[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; q[0].Constants.Num32BitValues = 38; q[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        q[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[1].DescriptorTable.NumDescriptorRanges = 1; q[1].DescriptorTable.pDescriptorRanges = &r3; q[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        q[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[2].DescriptorTable.NumDescriptorRanges = 1; q[2].DescriptorTable.pDescriptorRanges = &r1; q[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        q[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; q[3].DescriptorTable.NumDescriptorRanges = 1; q[3].DescriptorTable.pDescriptorRanges = &rw; q[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC qd = {};
        q[4] = q[3]; q[4].DescriptorTable.pDescriptorRanges = &rm;
        qd.NumParameters = 5; qd.pParameters = q; qd.NumStaticSamplers = 1; qd.pStaticSamplers = &smp;

        ID3DBlob *sig2 = nullptr, *se2 = nullptr;
        hr = serialize(&qd, D3D_ROOT_SIGNATURE_VERSION_1, &sig2, &se2);
        Rel(se2);
        if (FAILED(hr)) { b.error = "差值根签名序列化失败"; Rel(cs2); return false; }
        hr = dev->CreateRootSignature(0, sig2->GetBufferPointer(), sig2->GetBufferSize(),
                                      __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&b.rs_rv));
        Rel(sig2);
        if (FAILED(hr)) { b.error = "差值根签名创建失败"; Rel(cs2); return false; }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pd2 = {};
        pd2.pRootSignature = b.rs_rv; pd2.CS.pShaderBytecode = cs2->GetBufferPointer(); pd2.CS.BytecodeLength = cs2->GetBufferSize();
        hr = dev->CreateComputePipelineState(&pd2, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&b.pso_rv));
        Rel(cs2);
        if (FAILED(hr)) { b.error = "差值 PSO 创建失败"; return false; }

        // Group synchronization belongs only to layer conditioning. Keeping it
        // in a runtime branch of the ordinary resolve shader crashes WARP's
        // execution path even for identity composition; separate entry points
        // preserve the math while giving each dispatch an unambiguous contract.
        ID3DBlob *stackCode=nullptr,*stackError=nullptr;
        hr=compile(kHlslResolve,sizeof(kHlslResolve)-1,"033_stack",nullptr,nullptr,"stack_main","cs_5_0",0,0,&stackCode,&stackError);
        if(FAILED(hr)){b.error=stackError?std::string(static_cast<const char*>(stackError->GetBufferPointer()),stackError->GetBufferSize()):"层间着色器编译失败";Rel(stackError);Rel(stackCode);return false;}
        Rel(stackError);pd2.CS={stackCode->GetBufferPointer(),stackCode->GetBufferSize()};
        hr=dev->CreateComputePipelineState(&pd2,__uuidof(ID3D12PipelineState),reinterpret_cast<void**>(&b.pso_stack));Rel(stackCode);
        if(FAILED(hr)){b.error="层间 PSO 创建失败";return false;}

        // 帧插值: 输入布局跟差值合成一样(3 SRV + 1 UAV + 8 个常量), 直接复用根签名
        ID3DBlob *cs3 = nullptr, *e3 = nullptr;
        hr = compile(kHlslInterp, sizeof(kHlslInterp) - 1, "033_interp", nullptr, nullptr,
                     "main", "cs_5_0", 0, 0, &cs3, &e3);
        if (FAILED(hr))
        {
            b.error = e3 ? std::string(static_cast<const char *>(e3->GetBufferPointer()), e3->GetBufferSize())
                         : "插值着色器编译失败";
            Rel(e3); return false;
        }
        Rel(e3);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd3 = {};
        pd3.pRootSignature = b.rs_rv; pd3.CS.pShaderBytecode = cs3->GetBufferPointer(); pd3.CS.BytecodeLength = cs3->GetBufferSize();
        hr = dev->CreateComputePipelineState(&pd3, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&b.pso_ip));
        Rel(cs3);
        if (FAILED(hr)) { b.error = "插值 PSO 创建失败"; return false; }

        // 时域稳定: 输入布局与差值合成一致(3 SRV + 1 UAV + 10 常量), 复用同一根签名
        ID3DBlob *cs4 = nullptr, *e4 = nullptr;
        hr = compile(kHlslStab, sizeof(kHlslStab) - 1, "033_stab", nullptr, nullptr,
                     "main", "cs_5_0", 0, 0, &cs4, &e4);
        if (FAILED(hr))
        {
            b.error = e4 ? std::string(static_cast<const char *>(e4->GetBufferPointer()), e4->GetBufferSize())
                         : "时域稳定着色器编译失败";
            Rel(e4); return false;
        }
        Rel(e4);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd4 = {};
        pd4.pRootSignature = b.rs_rv; pd4.CS.pShaderBytecode = cs4->GetBufferPointer(); pd4.CS.BytecodeLength = cs4->GetBufferSize();
        hr = dev->CreateComputePipelineState(&pd4, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&b.pso_st));
        Rel(cs4);
        if (FAILED(hr)) { b.error = "时域稳定 PSO 创建失败"; return false; }
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 48; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&b.heap));
    if (FAILED(hr)) { b.error = "描述符堆创建失败"; return false; }
    b.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    b.ready = true;
    return true;
}

// 把 src(任意尺寸, 需处于 SR 态) 双线性缩放写进 dst(需处于 UAV 态)。slot 0/1 选用堆里哪一对描述符。
static void Dispatch(Blitter &b, ID3D12Device *dev, ID3D12GraphicsCommandList *cl,
                     ID3D12Resource *src, DXGI_FORMAT src_fmt,
                     ID3D12Resource *dst, DXGI_FORMAT dst_fmt, UINT dst_w, UINT dst_h, int slot,
                     float white = 1.0f, int encode = 0, ID3D12Resource *white_tex = nullptr,
                     int encmode = 0, float diffuse_white = 203.0f, const pregrade::Settings* grade = nullptr)
{
    const UINT base = static_cast<UINT>(slot) * 2;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = b.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = b.heap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = src_fmt; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE h_srv = cpu; h_srv.ptr += static_cast<SIZE_T>(base) * b.inc;
    dev->CreateShaderResourceView(src, &sv, h_srv);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = dst_fmt; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE h_uav = cpu; h_uav.ptr += static_cast<SIZE_T>(base + 1) * b.inc;
    dev->CreateUnorderedAccessView(dst, nullptr, &uv, h_uav);

    D3D12_GPU_DESCRIPTOR_HANDLE g_srv = gpu; g_srv.ptr += static_cast<UINT64>(base) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_uav = gpu; g_uav.ptr += static_cast<UINT64>(base + 1) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_white = g_srv; // 静态白点分支不会读 t1，仍给根参数一个有效表
    if (white_tex != nullptr)
    {
        if (b.white_bound != white_tex)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC wv = {};
            wv.Format = DXGI_FORMAT_R32_FLOAT; wv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            wv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; wv.Texture2D.MipLevels = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE hw = cpu; hw.ptr += static_cast<SIZE_T>(12) * b.inc;
            dev->CreateShaderResourceView(white_tex, &wv, hw);
            b.white_bound = white_tex;
        }
        g_white = gpu; g_white.ptr += static_cast<UINT64>(12) * b.inc;
        white = -((white > 1e-4f) ? white : 1e-4f); // 负号只作“读 t1”标记；实际值由 GPU 纹理给出
    }

    ID3D12DescriptorHeap *heaps[] = { b.heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(b.rs);
    cl->SetPipelineState(b.pso);
    const float inv[2] = { 1.0f / dst_w, 1.0f / dst_h };
    UINT c[18] = { dst_w, dst_h,
                        *reinterpret_cast<const UINT *>(&inv[0]),
                        *reinterpret_cast<const UINT *>(&inv[1]),
                        *reinterpret_cast<const UINT *>(&white),
                        static_cast<UINT>(encode),
                        static_cast<UINT>(encmode),
                         *reinterpret_cast<const UINT *>(&diffuse_white) };
    const auto pg=pregrade::Checked(grade);std::memcpy(c+8,&pg,sizeof(pg));
    cl->SetComputeRoot32BitConstants(0, 18, c, 0);
    cl->SetComputeRootDescriptorTable(1, g_srv);
    cl->SetComputeRootDescriptorTable(2, g_uav);
    cl->SetComputeRootDescriptorTable(3, g_white);
    cl->Dispatch((dst_w + 7) / 8, (dst_h + 7) / 8, 1);
}

// Caller must keep this descriptor heap and all resources alive until execution
// completes. hostnr uses resolveleases; standalone tests wait for their queue.
struct MotionSharpen {
    ID3D12Resource* texture=nullptr;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    float still=0.20f,moving=0.05f,threshold=8.f;
    float scale[2]={};
    UINT origin[2]={},extent[2]={};
};
static bool ValidMotion(const MotionSharpen* m) {
    if(!m || !m->texture || !m->extent[0] || !m->extent[1])return false;
    if(m->format!=DXGI_FORMAT_R16G16_FLOAT && m->format!=DXGI_FORMAT_R32G32_FLOAT)return false;
    auto d=m->texture->GetDesc();
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.DepthOrArraySize!=1 || d.SampleDesc.Count!=1 ||
       m->origin[0]>d.Width || m->origin[1]>d.Height || m->extent[0]>d.Width-m->origin[0] || m->extent[1]>d.Height-m->origin[1])return false;
    return std::isfinite(m->scale[0]) && std::isfinite(m->scale[1]) && m->scale[0]!=0 && m->scale[1]!=0 &&
        std::isfinite(m->still) && std::isfinite(m->moving) && std::isfinite(m->threshold) &&
        m->still>=0 && m->still<=1 && m->moving>=0 && m->moving<=1 && m->threshold>0;
}

// 差值合成: dst(满, UAV 态) = full(满, SR 态) + (mdl_out - mdl_in)(小, 都在 SR 态) * strength
// ★最后三个参数按【确切类型】收★ (2026-09-13 V6.1 热修)
//   V6.1 在 skin_protect 和 descriptorBase 之间插了 skin_lift。hostnr.h 两处老调用按位置传槽位号
//   (层间抬升 base+6、最终合成 4), 槽位号悄悄变成了提亮强度、槽位退回默认 4 —— 层间抬升跟最终合成
//   写进同一组描述符, 执行时两次派发都读后写的那组, 最终合成读回一张这帧没人写过的中间图:
//   显存是零就整屏黑, 是残留就一屏脏图。只在第 2/3 层工作尺寸跟第 1 层不同时触发(巫师3 100%/85%)。
//   unsigned 转 float 编译器不吭声, 所以这三个参数按实参类型推导, 必须正好是 float/float/UINT:
//   以后再在这里插参数, 错位的调用直接编不过, 不会再悄悄跑错槽。
template<class SkinProtectT = float, class SkinLiftT = float, class DescriptorBaseT = UINT>
static void DispatchResolve(Blitter &b, ID3D12Device *dev, ID3D12GraphicsCommandList *cl,
                            ID3D12Resource *full, ID3D12Resource *mdl_in, ID3D12Resource *mdl_out,
                            DXGI_FORMAT fmt, ID3D12Resource *dst, UINT dst_w, UINT dst_h,
                            float strength, float white = 1.0f, float guard = 2.0f,
                            int passthrough = 1, float colour = 1.0f, float sharpen = 0.0f,
                            ID3D12Resource *white_tex = nullptr, int replace = 0,
                            int resample = 0, int encmode = 0,
                            int applymodel = 1, float split = 0.0f, float diffuse_white = 203.0f,
                            const MotionSharpen* motion = nullptr, const pregrade::Settings* grade = nullptr,
                            SkinProtectT skin_protect = 0.0f, SkinLiftT skin_lift = 0.0f, DescriptorBaseT descriptorBase = 4)
{
    static_assert(std::is_same<SkinProtectT, float>::value && std::is_same<SkinLiftT, float>::value &&
                  std::is_same<DescriptorBaseT, UINT>::value,
                  "DispatchResolve: skin_protect and skin_lift must be float, descriptorBase must be UINT (shifted argument?)");
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = b.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = b.heap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = fmt; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;

    ID3D12Resource *srcs[3] = { full, mdl_in, mdl_out };
    for (UINT i = 0; i < 3; ++i)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu; h.ptr += static_cast<SIZE_T>(descriptorBase + i) * b.inc;
        sv.Format = i == 0 ? fmt : srcs[i]->GetDesc().Format;
        dev->CreateShaderResourceView(srcs[i], &sv, h);
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = fmt; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE h_uav = cpu; h_uav.ptr += static_cast<SIZE_T>(descriptorBase+3) * b.inc;
    dev->CreateUnorderedAccessView(dst, nullptr, &uv, h_uav);

    D3D12_GPU_DESCRIPTOR_HANDLE g_srv = gpu; g_srv.ptr += static_cast<UINT64>(descriptorBase) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_uav = gpu; g_uav.ptr += static_cast<UINT64>(descriptorBase+3) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_white = g_srv;
    if (white_tex != nullptr)
    {
        if (b.white_bound != white_tex)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC wv = {};
            wv.Format = DXGI_FORMAT_R32_FLOAT; wv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            wv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; wv.Texture2D.MipLevels = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE hw = cpu; hw.ptr += static_cast<SIZE_T>(12) * b.inc;
            dev->CreateShaderResourceView(white_tex, &wv, hw);
            b.white_bound = white_tex;
        }
        g_white = gpu; g_white.ptr += static_cast<UINT64>(12) * b.inc;
        white = -((white > 1e-4f) ? white : 1e-4f);
    }

    const bool mas=ValidMotion(motion);
    D3D12_GPU_DESCRIPTOR_HANDLE g_motion=g_srv;
    if(mas) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mv=sv;mv.Format=motion->format;
        auto dest=cpu;dest.ptr+=static_cast<SIZE_T>(descriptorBase==4?13:descriptorBase+4)*b.inc;
        dev->CreateShaderResourceView(motion->texture,&mv,dest);
        g_motion=gpu;g_motion.ptr+=static_cast<UINT64>(descriptorBase==4?13:descriptorBase+4)*b.inc;
    }
    ID3D12DescriptorHeap *heaps[] = { b.heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(b.rs_rv);
    cl->SetPipelineState(replace==4?b.pso_stack:b.pso_rv);
    const float inv[2] = { 1.0f / dst_w, 1.0f / dst_h };
    UINT c[38] = {};
    c[0] = dst_w; c[1] = dst_h;
    c[2] = *reinterpret_cast<const UINT *>(&inv[0]);
    c[3] = *reinterpret_cast<const UINT *>(&inv[1]);
    c[4] = *reinterpret_cast<const UINT *>(&strength);
    c[5] = *reinterpret_cast<const UINT *>(&white);
    c[6] = *reinterpret_cast<const UINT *>(&guard);
    c[7] = static_cast<UINT>(passthrough);
    c[8] = *reinterpret_cast<const UINT *>(&colour);
    c[9] = *reinterpret_cast<const UINT *>(&sharpen);
    c[10] = static_cast<UINT>(replace);
    c[11] = static_cast<UINT>(resample);
    c[12] = static_cast<UINT>(encmode);
    c[13] = static_cast<UINT>(applymodel);
    c[14] = *reinterpret_cast<const UINT *>(&diffuse_white);
    c[15] = *reinterpret_cast<const UINT *>(&split);
    if(mas) {
        c[16]=1;
        std::memcpy(c+17,&motion->still,sizeof(float));
        std::memcpy(c+18,&motion->moving,sizeof(float));
        std::memcpy(c+19,&motion->threshold,sizeof(float));
        std::memcpy(c+20,motion->scale,2*sizeof(float));
        std::memcpy(c+22,motion->origin,2*sizeof(UINT));
        std::memcpy(c+24,motion->extent,2*sizeof(UINT));
    }
    const float skin=std::isfinite(skin_protect)?std::clamp(skin_protect,0.0f,1.0f):0.0f;
    std::memcpy(c+26,&skin,sizeof(skin));
    // 第 27 槽原本是闲置填充(mas_padding), 现在装肤色提亮强度; 根常量仍是 38 个。
    const float lift=std::isfinite(skin_lift)?std::clamp(skin_lift,0.0f,1.0f):0.0f;
    std::memcpy(c+27,&lift,sizeof(lift));
    const auto pg=pregrade::Checked(grade);std::memcpy(c+28,&pg,sizeof(pg));
    cl->SetComputeRoot32BitConstants(0, 38, c, 0);
    cl->SetComputeRootDescriptorTable(1, g_srv);
    cl->SetComputeRootDescriptorTable(2, g_uav);
    cl->SetComputeRootDescriptorTable(3, g_white);
    cl->SetComputeRootDescriptorTable(4, g_motion);
    cl->Dispatch((dst_w + 7) / 8, (dst_h + 7) / 8, 1);
}

// 帧插值: dst(满, UAV) = 在 prv 和 cur 中间的那一帧。mv 是像素单位的运动矢量。
static void DispatchInterp(Blitter &b, ID3D12Device *dev, ID3D12GraphicsCommandList *cl,
                           ID3D12Resource *prv, ID3D12Resource *cur, DXGI_FORMAT fmt,
                           ID3D12Resource *mv, DXGI_FORMAT mv_fmt,
                           ID3D12Resource *dst, UINT w, UINT h, float phase, float reject)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = b.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = b.heap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;

    ID3D12Resource *srcs[3] = { prv, cur, mv };
    DXGI_FORMAT     fms[3]  = { fmt, fmt, mv_fmt };
    for (UINT i = 0; i < 3; ++i)
    {
        sv.Format = fms[i];
        D3D12_CPU_DESCRIPTOR_HANDLE hh = cpu; hh.ptr += static_cast<SIZE_T>(8 + i) * b.inc;
        dev->CreateShaderResourceView(srcs[i], &sv, hh);
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = fmt; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE h_uav = cpu; h_uav.ptr += static_cast<SIZE_T>(11) * b.inc;
    dev->CreateUnorderedAccessView(dst, nullptr, &uv, h_uav);

    D3D12_GPU_DESCRIPTOR_HANDLE g_srv = gpu; g_srv.ptr += static_cast<UINT64>(8) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_uav = gpu; g_uav.ptr += static_cast<UINT64>(11) * b.inc;

    ID3D12DescriptorHeap *heaps[] = { b.heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(b.rs_rv);
    cl->SetPipelineState(b.pso_ip);
    const float inv[2] = { 1.0f / w, 1.0f / h };
    UINT c[8] = {};
    c[0] = w; c[1] = h;
    c[2] = *reinterpret_cast<const UINT *>(&inv[0]);
    c[3] = *reinterpret_cast<const UINT *>(&inv[1]);
    c[4] = *reinterpret_cast<const UINT *>(&phase);
    c[5] = *reinterpret_cast<const UINT *>(&reject);
    cl->SetComputeRoot32BitConstants(0, 8, c, 0);
    cl->SetComputeRootDescriptorTable(1, g_srv);
    cl->SetComputeRootDescriptorTable(2, g_uav);
    // 帧插值着色器不用 t3，但它和 resolve 共用根签名，根参数仍要完整设置。
    cl->SetComputeRootDescriptorTable(3, g_srv);
    cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
}

// 模型输入的时域稳定。hist/cur/mv 都要在 SR 态, dst 在 UAV 态。
//   alpha = 当前帧权重(1.0 等于关掉); kx/ky 把 mv 纹素值换算成 UV 位移。
//   描述符沿用帧插值那三格(8/9/10) + UAV(11) —— 两趟不会在同一时刻在飞。
static void DispatchStabilize(Blitter &b, ID3D12Device *dev, ID3D12GraphicsCommandList *cl,
                              ID3D12Resource *hist, ID3D12Resource *cur, DXGI_FORMAT fmt,
                              ID3D12Resource *mv, DXGI_FORMAT mv_fmt,
                              ID3D12Resource *dst, UINT w, UINT h,
                              float alpha, float kx, float ky)
{
    if (!b.ready || b.pso_st == nullptr || hist == nullptr || cur == nullptr || dst == nullptr) return;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = b.heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = b.heap->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels = 1;
    ID3D12Resource *srcs[3] = { hist, cur, mv };
    DXGI_FORMAT     fms[3]  = { fmt, fmt, mv_fmt };
    for (UINT i = 0; i < 3; ++i)
    {
        if (srcs[i] == nullptr) continue;
        sv.Format = fms[i];
        D3D12_CPU_DESCRIPTOR_HANDLE hh = cpu; hh.ptr += static_cast<SIZE_T>(8 + i) * b.inc;
        dev->CreateShaderResourceView(srcs[i], &sv, hh);
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = fmt; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE h_uav = cpu; h_uav.ptr += static_cast<SIZE_T>(11) * b.inc;
    dev->CreateUnorderedAccessView(dst, nullptr, &uv, h_uav);

    D3D12_GPU_DESCRIPTOR_HANDLE g_srv = gpu; g_srv.ptr += static_cast<UINT64>(8) * b.inc;
    D3D12_GPU_DESCRIPTOR_HANDLE g_uav = gpu; g_uav.ptr += static_cast<UINT64>(11) * b.inc;

    ID3D12DescriptorHeap *heaps[] = { b.heap };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(b.rs_rv);
    cl->SetPipelineState(b.pso_st);
    const float inv[2] = { 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h) };
    UINT c[10] = {};
    c[0] = w; c[1] = h;
    c[2] = *reinterpret_cast<const UINT *>(&inv[0]);
    c[3] = *reinterpret_cast<const UINT *>(&inv[1]);
    c[4] = *reinterpret_cast<const UINT *>(&alpha);
    c[5] = *reinterpret_cast<const UINT *>(&kx);
    c[6] = *reinterpret_cast<const UINT *>(&ky);
    cl->SetComputeRoot32BitConstants(0, 10, c, 0);
    cl->SetComputeRootDescriptorTable(1, g_srv);
    cl->SetComputeRootDescriptorTable(2, g_uav);
    cl->SetComputeRootDescriptorTable(3, g_srv);   // t3 用不到, 但根参数要设满
    cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
}

static void Barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &b);
}
static void UavBarrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = r;
    cl->ResourceBarrier(1, &b);
}
} // namespace scale
