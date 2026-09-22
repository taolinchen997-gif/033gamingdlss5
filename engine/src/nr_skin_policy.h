#pragma once
#include "nr_feature_policy.h"
// 皮肤保护强度：只由「叠了几层」决定，面板上不加任何控件。
//
// ★为什么是按层数，而不是一个滑块★
//   玩家报的原话是「多次堆叠之后人物跟非洲人一样」「法令纹很重」——
//   抱怨的对象是【叠层】，不是单层。单层那套画面业主是验收过的，
//   一个像素都不该动；叠得越多，同一份结构/颜色编辑就被施加越多次，
//   皮肤这种大面积中间调最先崩，所以保护跟着层数走正好。
//
// ★这个保护做什么★（着色器侧 scale.h 里本来就写好了，一直传 0 没启用）
//   skinGuard = protection * SkinWeight(颜色)  —— SkinWeight 是逐像素的
//   归一化色度门，不是人脸检测，没有 CPU、没有回读、没有延迟。它压三件事：
//     · 锐化       shp        *= 1 - 0.75*skinGuard   → 法令纹
//     · 模型的编辑 edit        在皮肤上退成【只取明暗、不取颜色】→ 发黑/变色
//     · 最终混合   colourEff  同上
//   语义上的皮肤细节仍然归模型自己的 auto mask 管，这里只管别过头。
namespace nrskin {
// 业主 2026-09-13：「单层也给」。原来单层留 0 是为了一个像素都不动已验收的画面，
// 业主看过之后要求单层也保护 —— 那就 0.5 起步，叠层再往上加。
inline float Protection(int layers){
    const int n=nrfeatures::ClampPasses(layers);
    return n>=3?1.0f:(n==2?0.75f:0.5f);
}
}
