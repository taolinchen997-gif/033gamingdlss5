# 第三方组件与许可

033 自己的代码按 AGPL-3.0 发布（见 [LICENSE](LICENSE)）。下面这些第三方代码保留各自的许可，许可原文就在列出的位置。

## 核心 `engine/`（033-engine.dll、nvngx.dll_033.dll）

- **OptiScaler**（GPL-3.0，033 有改动）：`engine/third_party/OptiScaler033/`，上游 https://github.com/optiscaler/OptiScaler 。
  它带进来的第三方库在 `engine/third_party/OptiScaler033/external/`，各有许可：AMD FidelityFX SDK（MIT）、Intel XeSS（Intel 许可）、NVIDIA Streamline（MIT）、NVAPI（MIT）、spdlog（MIT）、magic_enum（MIT）、unordered_dense（MIT）、simpleini（MIT）、Vulkan 头文件、DirectX Agility SDK 头文件（Microsoft）；ImGui（MIT）在 `OptiScaler/include/imgui/`；RenoDX 颜色合成思路的署名（MIT）在 `Licenses/RenoDX_ATTRIBUTION.txt`；FreeType（FTL，`external/freetype/`）和 Microsoft Detours（MIT，`OptiScaler/library/detours/`）的目录里没有许可文件，许可说明见 `installer/033/notices/FreeType/` 和 `installer/033/notices/Detours/`。
- **libfacedetection**（于仕琪，BSD-3-Clause）：`engine/third_party/FaceDetect033/`
- **GPUPixel** 美颜混合算法的改编（PixPark，Apache-2.0）：`engine/third_party/GPUPixel033/`
- **AMD FidelityFX 帧插值交换链**的改编（MIT）：`engine/third_party/FramePacing033/`
- **原生多帧解锁模块**（MIT）：`engine/src/mfg/`
- **ReShade 示例代码**（BSD-3-Clause）：`engine/src/reshade_utils/`
- **AMD FidelityFX CAS**（MIT）：`engine/licenses/FidelityFX-CAS.txt`
- SDK 头文件：NVIDIA NGX（`engine/sdk/ngx/`，NVIDIA RTX SDK 许可）、NVIDIA Streamline（`engine/sdk/streamline/`，MIT）、ReShade 6.8.0 插件接口（`engine/sdk/reshade-6.8.0/`，BSD-3-Clause）、ImGui（`engine/sdk/imgui-1.92.5-docking/`，MIT）、Microsoft Detours（`engine/sdk/detours/`，MIT）
- DLSS5-Feeder（MIT）的经验、Insane-Shaders 动漫色阶的思路（CC0）：许可见 `installer/033/notices/`

## 人物识别 `semantic/`（033-person-worker.exe；核心也用到这里的头文件）

- **DXL**（LCPD15，AGPL-3.0）：`semantic/third_party/DXL/`，上游 https://github.com/LCPD15/DXL
- **ONNX Runtime** 头文件（MIT）与 **DirectML** 头文件：`semantic/third_party/onnxruntime/`
- 运行时用到的 **YOLO11** 分割模型（Ultralytics，AGPL-3.0）随安装包分发，说明在 `installer/033/notices/YOLO11/`

## 入口 `host/`（dxgi.dll）

- **ReShade 6.8.0**（Patrick Mours，BSD-3-Clause）：`host/LICENSE.md`
- 依赖库在 `host/deps/`，各带许可：ImGui、stb、fpng、glad、MinHook、SPIR-V 头文件、VMA、OpenVR、OpenXR、utfcpp、D3D12 头文件等

## 安装器 `installer/`

- 033 自己的 PowerShell 脚本与 `033安装器.exe` 的源码（AGPL-3.0）
- 安装包随附的全部许可说明：`installer/033/notices/`（`bundled/` 里是从编出这些程序的源码树中原样收集的许可原文）

## 安装包里、本仓库不带的文件

NVIDIA `nvngx_dlssnr.dll`、ONNX Runtime、DirectML、YOLO11 模型、微软 VC++ 运行库、20/30 系插帧转接件 nvidia_mfg_bridge（GPLv3，作者只发布了二进制）。它们的许可说明见 `installer/033/notices/`。
