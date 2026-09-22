"""Build a local reviewable distribution; never touch a game or desktop."""
from pathlib import Path
import argparse, hashlib, json, shutil, zipfile, subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--output',required=True)
parser.add_argument('--archive',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
out=Path(args.output).resolve()
out.mkdir(parents=True,exist_ok=True)
candidate=root/'build/033-integrated-candidate_20260906'
vendor=root/'third_party/OptiScaler033'
old=root/'分发包/DLSS5一键包 v5.0/工具'
payload=[]
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
    return h.hexdigest().upper()
def copy(src,dest,target=None):
    dst=out/dest;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    if target:payload.append({'Path':dest,'Target':target,'SHA256':digest(dst)})
copy(candidate/'core/033-render-core.dll','payload/033-render-core.dll','@CORE@')
copy(candidate/'addon/dlss5-033.addon64','payload/dlss5-033.addon64','dlss5-033.addon64')
copy(candidate/'addon/nvngx.dll_033.dll','payload/nvngx.dll_033.dll','nvngx.dll_033.dll')
copy(candidate/'core/nvngx.dll_dlssnr.dll','payload/nvngx.dll_dlssnr.dll','nvngx.dll_dlssnr.dll')
for name in ['nvngx_dlssnr.dll','nvngx_dlss.dll']:
    copy(old/'运行时'/name,'payload/'+name,name)
for name in ['libxess.dll','libxess_dx11.dll','libxess_fg.dll','libxell.dll']:
    copy(vendor/'external/xess/bin'/name,'payload/OptiScaler/'+name,'OptiScaler/'+name)
for sub in ['external/FidelityFX-SDK-v2/Kits/FidelityFX/signedbin','external/FidelityFX-SDK/PrebuiltSignedDLL']:
    for dll in sorted((vendor/sub).glob('*.dll')):
        copy(dll,'payload/OptiScaler/'+dll.name,'OptiScaler/'+dll.name)
for script in sorted((root/'deploy').glob('*.ps1')):
    # BOM makes the shipped scripts readable by Windows PowerShell 5.1 too.
    dst=out/'tools'/script.name;dst.parent.mkdir(parents=True,exist_ok=True)
    dst.write_text(script.read_text(encoding='utf-8-sig'),encoding='utf-8-sig')
for asset in ['diagnose.cmd','SWAPPER_SOURCE_NOTICE.md','swapper_reference_pins.json','licenses/swapper/LICENSE','licenses/swapper/THIRD_PARTY_NOTICES.md']:
    copy(root/'deploy'/asset,'tools/'+asset)
copy(vendor/'LICENSE','licenses/OptiScaler-GPL-3.0.txt')
copy(root/'src/mfg/MFG-UNLOCK-LICENSE.txt','licenses/MFG-UNLOCK-LICENSE.txt')
for name in ['组件来源与许可.txt','DLSS5-Feeder-LICENSE.txt']:
    copy(old/'署名'/name,'licenses/'+name)
for path in ['external/xess/LICENSE.txt','external/FidelityFX-SDK/LICENSE.txt','external/FidelityFX-SDK-v2/docs/license.md','external/nvapi/License.txt','external/streamline/license.txt']:
    src=vendor/path
    if src.exists():copy(src,'licenses/'+path.replace('/','_'))
copy(root/'third_party/freetype033/docs/FTL.TXT','licenses/FreeType-FTL.txt')
copy(root/'third_party/freetype033/docs/GPLv2.TXT','licenses/FreeType-GPLv2.txt')
# Existing fallback components are retained as a separate route. They are not
# injected beside the integrated upscaler into the same game/session.
for name in ['dlss5-feed.addon64','dlss5-feed.addon32','dlss5-feed-host64.exe','DLSS5_Feed.fx','ReShade.fxh','ReShadeUI.fxh']:
    copy(old/name,'compat/Feeder/'+name)
for src in sorted((old/'运动矢量LumeniteFX').rglob('*')):
    if src.is_file():copy(src,'compat/LumeniteFX/'+src.relative_to(old/'运动矢量LumeniteFX').as_posix())
readme="""# 033 完整控制台 / 闪烁修复 / 人像候选 2026-09-06

本候选修复已复现的 NR 资源槽位不足：燕云上一候选日志记录 116 次跳过；旧容量 16 在 48 条保留命令列表的独立压力测试失败，新容量 128 连续 160 帧通过，初始化后无漏处理。仍需用户确认游戏中偶发闪烁是否全部消失。

Insert 打开 033 完整控制台：同一界面包含 033 NR、人像、前置调色、合成、冻结对比和原生多帧设置，以及原有 OptiScaler 超分、替代帧生成、HUDfix、延迟、锐化、缩放、LOD、HDR、输入与兼容设置。Home 中的 033 页面也能进入。199 个上游编译单元完整保留，37 项 033 NR 控制通过版本化接口操作同一渲染器。并不是把 OptiScaler 的 DLL 改名字就宣称拥有 NVIDIA/AMD/Intel 模型。

人像自然起点：模型自动皮肤遮罩开启，皮肤结构 1.25，肤色保护 0.50；颜色保护减轻过锐和偏色，不做人脸几何检测，不增加一层 NR。肤色线索可能影响暖色物体；审美结果仍待同镜头比较。可在控制台点击此预设，或安装时明确加 -PortraitNatural。普通升级保留当前调色、锐化和人像参数。

两层 DLSS5 已在独立 128×96 小场景验证两个独立 feature 连续 20 帧，第二层 GPU 耗时约等于第一层。此实验只证明调用可行、像素有限；不证明人像更美、4K 稳定或游戏延迟。游戏候选仍为一层 NR。

保持 100% NR 与 6x 原生 MFG。核心的替代 FG / fakenvapi 默认关闭，让游戏原有 FG/Reflex 与 033 六倍路径继续工作；这些能力完整保留、可在控制台选择，不代表能把互斥的后端同时打开。替代 FG、RR、Vulkan、DX11、各厂商低延迟的可用性仍取决于显卡、驱动、游戏输入和相应运行库。本机验证的五种后端均由 NGX 输入触发；不能冒充 FSR/XeSS 原始接口或全游戏验收。

## 安装和精确回退

先由用户手动退出游戏，再运行 tools/integrated_install.ps1：

`-Action Plan -GameExe '游戏真实路径.exe'`

`-Action Install -GameExe '游戏真实路径.exe' [-PortraitNatural]`

本安装器升级已有 64 位 033/ReShade 原生超分路线；不会自动把 Feeder / 零引导路线改成另一种实验。

安装返回 Receipt。`-Action Restore -Receipt '实际 receipt.json 路径'` 恢复本次安装前每个文件的原值或原先不存在状态，不能把“换到下一个挂载名”当回退。安装后自行修改的文件会先阻止回退，避免覆盖。监督器只读，不启动或关闭游戏、不重挂载、不根据日志猜画质。

## 来源与复验

对应完整源码和构建依赖在 sources.zip。OptiScaler 核心上游为 Dagherbou/OptiScaler_DLSSNR 973761621353b99bee3dc7d4bb27b117fef2644f，GPL/MIT/第三方许可原文在 licenses 和源码原位置。运行库仍由各自供应商提供。

构建：test/build_integrated_core.ps1、test/build_capability_candidate.ps1。

真实模型独立测试：test/build_integrated_smoke.ps1 -Backend dlss -RealNR -ProxyMount。

槽位回归：设置 K033_STRESS_RETAINED_LISTS=1，同一测试默认通过；加 -TestLeaseCapacity 16 应出现 NR blink 失败。

双层独立试验：同一测试加 -TwoPassProbe；该选项不写游戏配置。

构建脚本目前包含本机 VS2022 / SDK 路径，其他机器应先调整。manifest.json 校验运行包与 sources.zip；发行 ZIP 旁另有 SHA256。validation 目录记录自动检查与局限。
"""
(out/'README.md').write_text(readme,encoding='utf-8-sig')
if args.archive:
    # SDKs keep required import libraries under x64/ and build scripts under
    # build/. Exclude metadata/generated objects, not those source directories.
    excluded={'.git','.vs','__pycache__','.cache'}
    with zipfile.ZipFile(out/'sources.zip','w',zipfile.ZIP_DEFLATED,compresslevel=3) as z:
        for rel in ['src','sdk','test','deploy','third_party/OptiScaler033','third_party/freetype033']:
            base=root/rel
            for p in sorted(base.rglob('*')):
                if p.is_file() and not any(part in excluded for part in p.relative_to(base).parts) and p.name not in {'.git','.env'} and p.suffix.lower() not in {'.obj','.pdb','.pch','.ilk','.tlog','.lastbuildstate'}:
                    z.write(p,p.relative_to(root).as_posix())
        # Match the legacy runtime lookup expected by the reproducible smoke
        # scripts without embedding a second copy of the model in this ZIP.
        z.writestr('RESTORE-RUNTIMES.txt','Copy payload/nvngx_dlss.dll and payload/nvngx_dlssnr.dll from the distribution into 分发包/DLSS5一键包 v5.0/工具/运行时 before running the standalone smoke script.\n')
files=[]
for p in sorted(out.rglob('*')):
    if p.is_file() and p.name!='manifest.json':files.append({'Path':p.relative_to(out).as_posix(),'Bytes':p.stat().st_size,'SHA256':digest(p)})
manifest={'Version':1,'PackageId':'033-unified-portrait-flicker-20260906','RenoDxBaselineHash':'88BD4DB2B8931B4AB385DE36E54FA96DC329DCA9DADC55B0BCD85B6760D9C337','CoreUpstream':'973761621353b99bee3dc7d4bb27b117fef2644f','Files':files,'Payload':payload}
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
if args.archive:
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=3) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,out.name+'/'+p.relative_to(out).as_posix(),compress_type=zipfile.ZIP_STORED if p.suffix=='.zip' else zipfile.ZIP_DEFLATED)
    archive.with_suffix('.zip.sha256').write_text(digest(archive)+'  '+archive.name+'\n',encoding='utf-8')
print(json.dumps({'Package':str(out),'Files':len(files),'Payload':len(payload),'Archive':args.archive},ensure_ascii=False))
