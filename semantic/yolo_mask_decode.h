// Adapted from LCPD15/DXL, commit 8644a875e61a9ecb8379f78ce9fd3ef3bc4853a4.
// Copyright (C) 2026 LCPD15. SPDX-License-Identifier: AGPL-3.0-only.
// 033 changes: CPU-only standalone decoder; person group; bounded buffers;
// finite tensor validation; coherent output only on success. Original license retained.
#pragma once
#include "third_party/DXL/SemanticMask.h"
#include <cstddef>
#include <utility>
namespace yanyunmask {
constexpr uint32_t INPUT_WH=640,PROTO_WH=160;
constexpr float CONF_TH=.35f,IOU_TH=.45f;
constexpr uint8_t kClassGroup[80] = {
	0,                                                    // person
	1, 1, 1, 1,                                           // bicycle car motorcycle airplane
	1, 1, 1, 1,                                           // bus train truck boat
	3, 3, 3, 3, 3,                                        // traffic light fire hydrant stop sign parking meter bench
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,                        // bird cat dog horse sheep cow elephant bear zebra giraffe
	10, 10, 10, 10, 10,                                   // backpack umbrella handbag tie suitcase
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4,                        // frisbee skis snowboard sports ball kite baseball bat glove skateboard surfboard tennis racket
	6, 6, 6, 6, 6, 6,                                     // bottle wine glass cup fork knife spoon
	6,                                                    // bowl
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5,                        // banana apple sandwich orange broccoli carrot hot dog pizza donut cake
	7, 7, 11,                                             // chair couch potted plant
	7,                                                    // bed
	7,                                                    // dining table
	9,                                                    // toilet
	8, 8, 8,                                              // tv laptop mouse
	8,                                                    // remote
	8, 8,                                                 // keyboard cell phone
	8, 8, 8,                                              // microwave oven toaster
	9, 8,                                                 // sink refrigerator（冰箱归电器：厨房家电）
	11, 11, 11,                                           // book clock vase
	11, 11, 11, 11,                                       // scissors teddy bear hair drier toothbrush
};
static_assert(sizeof(kClassGroup) == 80, "80 类全覆盖");
static_assert(kClassGroup[58] == 11 && kClassGroup[59] == 7, "COCO potted plant and bed order");


// S27 protagonist lock (user: 「主角一直在画面中间，按理说永远不掉识别才对？」).
// A third-person camera keeps the main character near the picture centre, yet
// every recognition is an independent pass, so a person under CONF_TH (or, until
// S27, out-scored by any other class) vanished for that frame. The track locks
// onto a person matching the protagonist prior LockAfter recognitions in a row;
// while locked, an anchor on the track counts from RESCUE_TH, and a recognition
// with nothing on the track is reported missing so the engine keeps the last
// published masks (latemask::MaxProtagonistHeld). Released after ReleaseMisses
// misses in a row. Boxes are in the 640 letterbox canvas.
constexpr float RESCUE_TH=.15f,SeenIoU=.3f,RescueIoU=.4f;
constexpr unsigned LockAfter=3,ReleaseMisses=24;
constexpr uint32_t PLocked=1,PSeen=2,PRescued=4,PMissing=8; // == yyworker::protagonist
struct ProtagonistBox {float x1=0,y1=0,x2=0,y2=0;};
// S28 (user: 「最好是按面积来取，这样远处的人自动过滤掉了，也不会闪」): a person
// whose box covers less than MinPersonArea of the picture is left to the scene
// look. One kept in the previous recognition (IoU >= SeenIoU) stays down to
// KeepPersonArea, so a person near the threshold does not flicker in and out.
// The locked protagonist is never filtered. 0.4% ~ a standing person 13% of the
// picture tall; distant NPCs are far smaller.
constexpr float MinPersonArea=.004f,KeepPersonArea=.0025f;
struct ProtagonistTrack {
 ProtagonistBox box;unsigned streak=0,missed=0;bool locked=false;
 std::vector<ProtagonistBox> keptPeople; // S28: people kept in the previous recognition
 uint64_t smallDropped=0,smallKept=0;
 // Worker log totals; missRuns: misses in a row before the next sighting 1-2/3-6/7-18/longer.
 uint64_t recognitions=0,lockedRecognitions=0,seen=0,rescued=0,missing=0,acquired=0,released=0,personFirstKept=0,missRuns[4]{};
};
struct DecodeReport {uint32_t protagonist=0;float protagonistScore=0;unsigned detections=0,personFirstKept=0;};
inline float BoxIoU(float ax1,float ay1,float ax2,float ay2,const ProtagonistBox& b){
 const float ix=std::max(0.f,std::min(ax2,b.x2)-std::max(ax1,b.x1)),iy=std::max(0.f,std::min(ay2,b.y2)-std::max(ay1,b.y1));
 const float inter=ix*iy,u=(ax2-ax1)*(ay2-ay1)+(b.x2-b.x1)*(b.y2-b.y1)-inter;
 return u>0?inter/u:0.f;
}
// Acquisition prior: centre within the middle 40% horizontally and 30-85%
// vertically, 12-95% of the picture height tall; the largest such person wins.
inline bool ProtagonistPrior(float x1,float y1,float x2,float y2,float contentW,float contentH){
 const float cx=(x1+x2)*.5f/contentW,cy=(y1+y2)*.5f/contentH,h=(y2-y1)/contentH;
 return cx>=.3f&&cx<=.7f&&cy>=.3f&&cy<=.85f&&h>=.12f&&h<=.95f;
}
inline unsigned MissRunBucket(unsigned run){return run<=2?0u:run<=6?1u:run<=18?2u:3u;}

// personFirst=false keeps DXL's exact 80-class argmax rule (equivalence fixture).
inline bool Decode(const float* det,size_t detCount,const float* proto,size_t protoCount,
 uint32_t width,uint32_t height,uint64_t frame,uint64_t capturedMs,DXL::SemanticMaskSnapshot& output,
 ProtagonistTrack* track=nullptr,DecodeReport* report=nullptr,bool personFirst=true) noexcept {
 if(!det||!proto||detCount!=116*8400||protoCount!=32*160*160||!width||!height||
    width>8192||height>8192||size_t(width)*height>33554432||!frame)return false;
 for(size_t i=0;i<detCount;++i)if(!std::isfinite(det[i]))return false;
 for(size_t i=0;i<protoCount;++i)if(!std::isfinite(proto[i]))return false;
 try {DXL::SemanticMaskSnapshot next;
	// det: (116, anchors)。排布（#71 数据反推钉死）：
	// 4 box + 80 cls(**已 sigmoid**，别再套) + 32 coef(logit)。
	const uint32_t anchors = 8400;
	const float* boxes = det;                     // 4 x anchors
	const float* classes = det + 4 * anchors;     // 80 x anchors（概率）
	const float* coefs = det + 84 * anchors;      // 32 x anchors（logit）

	// 多类实例收集 + NMS（#79 语义分组）：
	// 每个 anchor 取 80 类 argmax → conf + 类别 → 整合组（kClassGroup）；
	// 组开关过滤（worker 侧：全类解码的 proto 合成按实例数走，12 组全开 +
	// 繁忙场景会到几十个实例；过滤后典型 1~3 组。开关变化 ≤33ms 生效）；
	// NMS 只比同组实例（车和人的框本来就该重叠，跨组抑制会误杀）。
	struct Det { float x1, y1, x2, y2, conf; int anchor; uint8_t group; bool personFirst; };
    std::vector<Det> candidates;
    candidates.reserve(256);
    const uint32_t groupMask = 1; // 033 only requests people
    for (uint32_t a = 0; a < anchors; ++a) {
        // Only group 0 (person) is requested. Preserve the exact 80-class
        // argmax/tie rule, but skip low-person anchors before walking 79 classes.
        const float conf=classes[a];
        if(conf<CONF_TH)continue; // all tensor values were validated above
        bool otherWins=false;
        for(uint32_t c=1;c<80;++c)if(classes[size_t(c)*anchors+a]>conf){otherWins=true;break;}
        // S27: only people are wanted. A hat, umbrella or horse scoring higher on
        // the same anchor no longer discards the person on it.
        if(otherWins&&!personFirst)continue;
        constexpr uint8_t group=0;
        const float cx = boxes[a], cy = boxes[anchors + a];
        const float bw = boxes[2 * anchors + a], bh = boxes[3 * anchors + a];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(bw) ||
            !std::isfinite(bh) || bw <= 0 || bh <= 0 || std::abs(cx)>4096 || std::abs(cy)>4096 || bw>4096 || bh>4096) continue;
        candidates.push_back({cx-bw*.5f, cy-bh*.5f, cx+bw*.5f, cy+bh*.5f, conf, int(a), group, otherWins});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Det& a, const Det& b) { return a.conf > b.conf; });
    Det dets[64]; uint32_t nDets = 0;
    for (const Det& d : candidates) {
        bool drop = false;
        for (uint32_t j = 0; j < nDets; ++j) {
            const Det& o = dets[j];
            if (o.group != d.group) continue;
            const float ix = std::max(0.0f, std::min(d.x2,o.x2)-std::max(d.x1,o.x1));
            const float iy = std::max(0.0f, std::min(d.y2,o.y2)-std::max(d.y1,o.y1));
            const float inter = ix*iy;
            const float denom = (d.x2-d.x1)*(d.y2-d.y1)+(o.x2-o.x1)*(o.y2-o.y1)-inter;
            if (denom > 0 && inter/denom > IOU_TH) { drop = true; break; }
        }
        if (!drop) dets[nDets++] = d;
        if (nDets == 64) break;
    }
    unsigned personFirstKept=0;for(uint32_t i=0;i<nDets;++i)personFirstKept+=dets[i].personFirst?1u:0u;

    // S27 protagonist lock (see ProtagonistTrack above).
    const float canvasScale=float(INPUT_WH)/float(width>height?width:height);
    const float contentW=float(width)*canvasScale,contentH=float(height)*canvasScale;
    uint32_t pflags=0;float pscore=0;
    // S28 size filter (see MinPersonArea) before the protagonist logic reads dets.
    if(track){
     auto& t=*track;std::vector<ProtagonistBox> kept;uint32_t n=0;const float content=contentW*contentH;
     for(uint32_t i=0;i<nDets;++i){const Det& d=dets[i];const float area=content>0?(d.x2-d.x1)*(d.y2-d.y1)/content:0.f;
      bool keep=area>=MinPersonArea;
      if(!keep&&t.locked&&BoxIoU(d.x1,d.y1,d.x2,d.y2,t.box)>=SeenIoU)keep=true;
      if(!keep&&area>=KeepPersonArea)for(const auto& b:t.keptPeople)if(BoxIoU(d.x1,d.y1,d.x2,d.y2,b)>=SeenIoU){keep=true;++t.smallKept;break;}
      if(keep){dets[n++]=d;kept.push_back({d.x1,d.y1,d.x2,d.y2});}else ++t.smallDropped;
     }
     nDets=n;t.keptPeople=std::move(kept);
    }
    if(track){
     auto& t=*track;++t.recognitions;t.personFirstKept+=personFirstKept;
     if(t.locked){
      pflags|=PLocked;++t.lockedRecognitions;int best=-1;float bestIoU=0;
      for(uint32_t i=0;i<nDets;++i){const float iou=BoxIoU(dets[i].x1,dets[i].y1,dets[i].x2,dets[i].y2,t.box);if(iou>=SeenIoU&&iou>bestIoU){best=int(i);bestIoU=iou;}}
      if(best>=0){const Det& d=dets[best];t.box={d.x1,d.y1,d.x2,d.y2};pflags|=PSeen;pscore=d.conf;}
      else{
       // Rescue: the best anchor on the track from RESCUE_TH, whatever else it resembles.
       Det rescue{};bool found=false;
       for(uint32_t a=0;a<anchors;++a){
        const float conf=classes[a];if(conf<RESCUE_TH||(found&&conf<=rescue.conf))continue;
        const float cx=boxes[a],cy=boxes[anchors+a],bw=boxes[2*anchors+a],bh=boxes[3*anchors+a];
        if(!(bw>0)||!(bh>0)||std::abs(cx)>4096||std::abs(cy)>4096||bw>4096||bh>4096)continue;
        const float x1=cx-bw*.5f,y1=cy-bh*.5f,x2=cx+bw*.5f,y2=cy+bh*.5f;
        if(BoxIoU(x1,y1,x2,y2,t.box)<RescueIoU)continue;
        rescue={x1,y1,x2,y2,conf,int(a),0,false};found=true;
       }
       if(found&&nDets<64){dets[nDets++]=rescue;t.box={rescue.x1,rescue.y1,rescue.x2,rescue.y2};pflags|=PSeen|PRescued;pscore=rescue.conf;++t.rescued;}
       else pflags|=PMissing;
      }
      if(pflags&PSeen){if(t.missed)++t.missRuns[MissRunBucket(t.missed)];t.missed=0;++t.seen;}
      else{++t.missing;if(++t.missed>ReleaseMisses){++t.missRuns[MissRunBucket(t.missed)];t.locked=false;t.streak=0;t.missed=0;++t.released;}}
     }else{
      int pick=-1;float area=0;
      for(uint32_t i=0;i<nDets;++i){const Det& d=dets[i];if(!ProtagonistPrior(d.x1,d.y1,d.x2,d.y2,contentW,contentH))continue;
       const float ar=(d.x2-d.x1)*(d.y2-d.y1);if(ar>area){pick=int(i);area=ar;}}
      if(pick>=0){const Det& d=dets[pick];
       t.streak=t.streak&&BoxIoU(d.x1,d.y1,d.x2,d.y2,t.box)>=SeenIoU?t.streak+1:1;t.box={d.x1,d.y1,d.x2,d.y2};
       if(t.streak>=LockAfter){t.locked=true;t.missed=0;++t.acquired;pflags|=PLocked|PSeen;pscore=d.conf;}
      }else t.streak=0;
     }
    }

	// 双图（worker 中转，#78 软边 + #79 分组）：
	//   next.coverage  coverage 0..255 灰度（0=满强度、255=环境、中间=软边）
	//   next.groupIds 组 ID（0..11=组、255=无组）—— 消费端按组查强度。
	// 两条按同一像素同步写；发布也走同一版本号。
	const uint32_t fw = width, fh = height;
	if (!fw || !fh) return false;
	next.coverage.assign(size_t(fw) * fh, 255);
	next.groupIds.assign(size_t(fw) * fh, 255);
	// 640 画布 -> 原图 的缩放（letterbox 的逆）
	const float toFrame = float(fw > fh ? fw : fh) / float(INPUT_WH);
	for (uint32_t i = 0; i < nDets; ++i) {
		const Det& d = dets[i];
		// proto 合成（#71 修好的链）：coef(logit) @ proto(160x160) -> sigmoid。
		// **#78：保留连续 sigmoid（0..1 软边）**，不再 >0.5 阈值化 ——
		// proto 头自带亚像素边缘信息，二值化把它整个丢掉。
        float mask160[PROTO_WH * PROTO_WH]{};
        // Channel-major contiguous reads allow vectorization. Per-pixel floating
        // point addition order remains c=0..31, matching the original decoder.
        for(uint32_t c=0;c<32;++c){
            const float coefficient=coefs[c*anchors+d.anchor];
            const float* plane=proto+c*PROTO_WH*PROTO_WH;
            for(uint32_t p=0;p<PROTO_WH*PROTO_WH;++p)mask160[p]+=coefficient*plane[p];
        }
        for(float& value:mask160)value=1.f/(1.f+std::exp(-value));

		// 检测框裁剪（proto mask 常溢出框外一点）：框内像素才看 mask。
		// 640 坐标 -> 原图坐标 -> proto 160 网格（proto 覆盖整个 640 画布，#71）。
		const int bx1 = d.x1 > 0 ? int(d.x1 * toFrame) : 0;
		const int by1 = d.y1 > 0 ? int(d.y1 * toFrame) : 0;
		const int bx2 = d.x2 * toFrame < float(fw) ? int(d.x2 * toFrame) : int(fw);
		const int by2 = d.y2 * toFrame < float(fh) ? int(d.y2 * toFrame) : int(fh);
		// **#78 框边羽化**：硬裁剪在 proto 高值贴框边时显出直线锯齿；
		// 框边 feather 像素内 0..1 渐变。取框短边/4 和屏宽/200 的较小者
		//（1080p 约 5px，框越窄羽化越少，避免小框整个被羽没）。
		const float fwid = float(bx2 - bx1), fhgt = float(by2 - by1);
		const float feather = fwid > 0 && fhgt > 0
			? (fwid < fhgt ? fwid : fhgt) / 4.0f : 0.0f;
		const float featherF = feather < float(fw) / 200.0f
			? feather : float(fw) / 200.0f;
		const float gScale = float(PROTO_WH) / float(INPUT_WH);   // 640 -> 160
		for (int y = by1; y < by2; ++y) {
			if (y < 0 || y >= int(fh)) continue;
			// 框边羽化系数（上下边）：featherF 像素内 0..1
			const float ty = featherF > 0.0f
				? (float(y - by1) + 0.5f) / featherF : 1.0f;
			const float by2f = featherF > 0 ? (float(by2 - y) - 0.5f) / featherF : 1;
			const float edgeY = ty < 1.0f ? ty
				: (by2f < 1.0f ? by2f : 1.0f);
			for (int x = bx1; x < bx2; ++x) {
				if (x < 0 || x >= int(fw)) continue;
				// 同样处理左右边
				const float tx = featherF > 0.0f
					? (float(x - bx1) + 0.5f) / featherF : 1.0f;
				const float bx2f = featherF > 0 ? (float(bx2 - x) - 0.5f) / featherF : 1;
				const float edgeX = tx < 1.0f ? tx
					: (bx2f < 1.0f ? bx2f : 1.0f);
				const float edge = edgeX < edgeY ? edgeX : edgeY;
				if (edge <= 0.0f) continue;
				// 原图像素 -> 640 画布 -> proto 160 网格（**像素中心**，#78：
				// +0.5f —— 之前整数角对齐系统性偏半格，边缘偏移可见）
				const float canvasX = (float(x) + 0.5f) / toFrame;
				const float canvasY = (float(y) + 0.5f) / toFrame;
				const float gx = canvasX * gScale - 0.5f;
				const float gy = canvasY * gScale - 0.5f;
				// **#78 双线性采样 160 网格**（最近邻在 1080p 下每格 ≈12px 硬块）。
				// 越界夹边（框外 proto 无意义，框由羽化接管）。
				auto clampPos = [&](float v) {
					return v < 0.0f ? 0.0f : v > float(PROTO_WH - 1) ? float(PROTO_WH - 1) : v;
				};
				const float sx = clampPos(gx), sy = clampPos(gy);
				const int x0 = int(sx), y0 = int(sy);
				const int x1i = x0 + 1 < PROTO_WH ? x0 + 1 : PROTO_WH - 1;
				const int y1i = y0 + 1 < PROTO_WH ? y0 + 1 : PROTO_WH - 1;
				const float fx = sx - float(x0), fy = sy - float(y0);
				const float m00 = mask160[size_t(y0) * PROTO_WH + x0];
				const float m10 = mask160[size_t(y0) * PROTO_WH + x1i];
				const float m01 = mask160[size_t(y1i) * PROTO_WH + x0];
				const float m11 = mask160[size_t(y1i) * PROTO_WH + x1i];
				const float bilinear =
					(1.0f - fx) * (1.0f - fy) * m00 + fx * (1.0f - fy) * m10 +
					(1.0f - fx) * fy * m01 + fx * fy * m11;
				// 组度 t = sigmoid(双线性) × 框羽化；多实例重叠 winner-takes-group：
				// t 更大的实例赢（组 ID 跟着换），不再"后写的盖前写的"。
				const float t = bilinear * edge;
				uint8_t& covPx = next.coverage[size_t(y) * fw + x];
				uint8_t& grpPx = next.groupIds[size_t(y) * fw + x];
				const float cur = float(covPx) / 255.0f;   // 现值（1=环境）
				if (t > 1.0f - cur) {                      // 新实例更强 → 接管
					covPx = uint8_t((1.0f - t) * 255.0f + 0.5f);
					grpPx = d.group;
				}
			}
		}
	}


 next.width=width;next.height=height;next.version=frame;next.publishedMs=capturedMs;
 if(report){report->protagonist=pflags;report->protagonistScore=pscore;report->detections=nDets;report->personFirstKept=personFirstKept;}
 output=std::move(next);return true;
 }catch(...){return false;}
}
// UI: 100% person fidelity -> zero uplift inside the semantic person mask.
// An unavailable/stale mask must not restore a previous person's location.
inline bool Compose(uint8_t* rgba,size_t bytes,uint32_t pitch,uint32_t width,uint32_t height,
 const DXL::SemanticMaskSnapshot& mask,uint64_t now,float fidelity,float scene,float feather) noexcept {
 if(!rgba||!width||!height||width>8192||height>8192||pitch<size_t(width)*4||bytes<size_t(pitch)*height||
    !std::isfinite(fidelity)||!std::isfinite(scene)||!std::isfinite(feather)||fidelity<0||fidelity>1||scene<0||scene>1||feather<0||feather>8||
    !mask.Valid(now))return false;
 float strengths[DXL::SEM_GROUP_COUNT]{};strengths[0]=1-fidelity;
 DXL::ComposeSemanticMask(rgba,pitch,width,height,&mask,scene,strengths,1,feather);return true;
}
}
