# 033 燕云定制版：第三方组件与许可

本包只给燕云十六声（yysls.exe）用。下面是包里真实带着的文件，以及编进 033 自己程序里的第三方代码；原始许可放在同目录对应文件夹，不能用 033 的说明替代它们。`bundled/` 里是从编出这些程序的源码树里原样收集的全部许可原文。

## 源码

核心、人物识别、入口和安装器的全部源码：https://github.com/taolinchen997-gif/033gamingdlss5
核心编进了 OptiScaler（GPL-3.0），核心和人物识别编进了 DXL 的代码（AGPL-3.0），所以这两部分按 AGPL-3.0 提供源码；入口基于 ReShade（BSD-3-Clause）。

## 装进游戏目录的文件

- 入口 `dxgi.dll`：基于 ReShade 6.8.0（Patrick Mours，BSD-3-Clause），见 `ReShade/LICENSE.md`；它用到的第三方库（ImGui、stb、MinHook、glad、SPIR-V、VMA、OpenVR / OpenXR 头文件、utfcpp、D3D12 头文件等）的许可见 `bundled/host/`。
- 核心 `033-runtime/033-engine.dll` 与转发器 `033-runtime/nvngx.dll_033.dll`：033 自己的代码（B站 @热心网友033）。核心里编进了下列第三方源码：
  - OptiScaler（GPL-3.0，`OptiScaler/LICENSE`，上游 https://github.com/optiscaler/OptiScaler ，033 有改动），以及它带进来的 AMD FidelityFX（MIT）、Intel XeSS 接口、NVIDIA Streamline SDK（MIT）、NVAPI、spdlog、magic_enum、unordered_dense、simpleini、ImGui、Vulkan 头文件、DirectX Agility SDK 头文件、RenoDX 的颜色合成思路（MIT），许可原文见 `bundled/engine/`；Microsoft Detours（MIT，`Detours/`）；FreeType（FTL，`FreeType/`）。
  - NVIDIA NGX SDK（`nvidia-ngx/LICENSE.txt`）。
  - 原生多帧解锁模块（MIT，`MFG-Unlock/LICENSE.txt`）：40/50 系的 3×–6× 原生多帧。
  - AMD FidelityFX CAS（MIT）：内置清晰度的算法基础；AMD FidelityFX 帧插值交换链的改编（MIT，`FramePacing/`）。
  - libfacedetection（于仕琪，BSD-3-Clause，`FaceDetect/`）：人脸检测。
  - GPUPixel 的美颜混合算法改编（PixPark，Apache-2.0，`GPUPixel/`）。
  - DXL 的人物遮罩代码（LCPD15，AGPL-3.0，`DXL/`）。
  - Insane-Shaders 的动漫色阶思路（CC0，`InsaneShaders033/`）：前置调色「动漫色阶」。
  - DLSS5-Feeder（MIT，`Feeder-LICENSE`）：共享纹理与后台处理的经验来源。
- NVIDIA 神经渲染模型 `033-runtime/nvngx_dlssnr.dll`：NVIDIA 原件，条款见 `nvidia-ngx/LICENSE.txt`。
- 人物识别 `033-runtime/semantic/`：`033-person-worker.exe`（033 自己的代码，含 DXL 改编部分，`DXL/`，AGPL-3.0）；ONNX Runtime（MIT，`ONNXRuntime/`）；DirectML（`DirectML/`）；YOLO11 分割模型（Ultralytics，AGPL-3.0，`YOLO11/`）。
- 微软 VC++ 2015–2022 x64 运行库（`033-runtime/` 与 `033-runtime/semantic/`，`MSVC/`）。
- 只在 RTX 20/30 系显卡上装的插帧转接件：nvidia_mfg_bridge v0.504（作者 gt2333588，基于 Nukem9 的 dlssg-to-fsr3，GPLv3），许可、说明与来源见 `nvidia_mfg_bridge/`；40/50 系电脑不会装它。它的作者只发布了二进制，033 手里没有它的源码，要源码请找原作者：https://github.com/gt2333588/nvidia_mfg_bridge 。

## 本包不带

dgVoodoo2、REFramework、NVIDIA 光流 FRUC、Vulkan 层、RE 游戏适配器和通用版的其它路线都不在本包里，它们的许可说明也已拿掉。

每个文件的 SHA256 指纹见 `工具/署名/文件指纹.txt`，可以用「维护工具\验真.cmd」逐个核对。
