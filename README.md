# 033 燕云定制版 · 源码

《燕云十六声》专用的画质插件（作者 B站 @热心网友033）：借用游戏自带的 DLSS 做神经降噪（NR），外加分层超分（SR）、前置调色、人物 / 场景分开调节和帧生成（FG）。国服（`yysls.exe`）和国际版（Where Winds Meet，`wwm.exe`，例如 Steam 版）都能用。

本仓库是 **燕云领先版 V6.2.1** 安装包里所有由 033 编译的程序的完整源码，用来履行 GPL-3.0 / AGPL-3.0 的源码义务。V6.2 的源码在本仓库较早的提交里。

> **安装包不在 GitHub 上。** 包里带有 NVIDIA 的运行库，不适合放在这里。安装包请到作者的 B站视频下载：[BV1w9aw6oE7x](https://www.bilibili.com/video/BV1w9aw6oE7x/)（作者主页：[B站 @热心网友033](https://space.bilibili.com/88101991)）。

## 目录

| 目录 | 内容 | 编出来的文件（安装包里的位置） |
|---|---|---|
| `engine/` | 核心：画面处理、面板、NR / SR / FG、人物 / 场景分区。编进了 OptiScaler（033 改版） | `033-runtime/033-engine.dll`、`033-runtime/nvngx.dll_033.dll` |
| `semantic/` | 人物识别程序（YOLO11 分割 + DXL 的遮罩处理）；核心也用到这里的头文件 | `033-runtime/semantic/033-person-worker.exe` |
| `host/` | 入口，ReShade 6.8.0 改版 | `dxgi.dll` |
| `installer/` | 安装器：PowerShell 脚本、界面图片、清单、许可说明，以及 `033安装器.exe` 的源码（`installer/launcher/`） | 安装包里除程序文件以外的全部内容 |
| `build/` | 编译脚本和逐字节核对工具 | |

安装包里其余的文件不是 033 编译的，原样分发：NVIDIA 的 `nvngx_dlssnr.dll`、ONNX Runtime、DirectML、YOLO11 模型、微软 VC++ 运行库，以及只给 RTX 20/30 系装的插帧转接件 nvidia_mfg_bridge。

## 和安装包对得上

按 [BUILD.md](BUILD.md) 编出来的转发器、人物识别、入口和 V6.2.1 安装包里的文件逐字节一致，只差编译时间戳（.NET 启动器还有一个每次编译都会变的模块 ID）；核心换个目录编时，MSVC 会按源码路径改动一部分数据的排列，核对方法和结果见 BUILD.md。

| 文件 | V6.2.1 安装包里的 SHA-256 |
|---|---|
| `033-runtime/033-engine.dll` | `f126c5149d8deadd6ad5b1a3f1f1b0f19fd358610c104d3fdb4b209a13d556a5` |
| `033-runtime/nvngx.dll_033.dll` | `7deb0f8842ec6ce2f2609d87adde87194270fa60519dca542eedde57da20ac5b` |
| `033-runtime/semantic/033-person-worker.exe` | `ff9f40dc249ccf6642a485a14d58d8f569a6d3645d0b6a8e1e123f91e034be11` |
| `dxgi.dll` | `418e69676aa2168fcbffdde2e6409b4448e07507a007369e7b9dd8ed2bb7bf59` |
| `033安装器.exe` | `96178a99df6dfca564ccbbc1a9ff4d8dd0f15cde703e9018aced71f55a2cb894` |

## 许可证

- 033 自己的代码按 **AGPL-3.0** 发布（[LICENSE](LICENSE)）。核心编进了 OptiScaler（GPL-3.0），核心和人物识别编进了 DXL（AGPL-3.0），所以整体按 AGPL-3.0。
- 第三方代码保留各自的许可，见 [NOTICE.md](NOTICE.md) 和各目录里的 LICENSE 文件。
- 20/30 系插帧转接件 nvidia_mfg_bridge（GPLv3，作者 gt2333588，基于 Nukem9 的 dlssg-to-fsr3）只以二进制发布，本仓库没有它的源码，请向原作者索取：https://github.com/gt2333588/nvidia_mfg_bridge

## 声明

本项目与网易、NVIDIA、AMD、Intel 没有关系。往在线游戏里加载第三方程序有风险（包括账号风险），请自行判断，后果自负。
