# 编译

## 需要准备

- Windows 10 / 11 x64
- Visual Studio 2022 Build Tools：MSVC **14.44.35207**（「MSVC v143 - VS 2022 C++ x64/x86 生成工具 (v14.44)」）和 MSBuild
- Windows SDK **10.0.26100.0**
- Python 3（入口的 GLAD 生成用，只用标准库）
- .NET Framework 4.x（Windows 自带的 `csc.exe`，编启动器用）

脚本默认用这些工具的默认安装路径，装在别处请改脚本开头的路径。**仓库请放在短路径下**（例如 `C:\src\033`）：OptiScaler 里有很深的目录，路径超过 260 个字符时编译工具会找不到文件。克隆前可以先执行 `git config --global core.longpaths true`。

所有脚本都只编译、跑 CPU 自检，不启动游戏，也不用显卡。

## 1. 核心（033-engine.dll、nvngx.dll_033.dll）

```
powershell -ExecutionPolicy Bypass -File engine\test\build_single_engine.ps1 -OutputDirectory out\engine
```

先编译并运行几项 CPU 自检（其中一项顺便把调色着色器编成字节码头文件），再用 MSBuild 编 `engine/third_party/OptiScaler033/OptiScaler/OptiScaler.vcxproj` 并把 033 的对象文件链接进去，得到 `033-engine.dll`，最后编 `nvngx.dll_033.dll`。输出目录里还会出现 `dlss5-033.addon64` 和 `033-framegen-provider.dll`，燕云定制版不用它们。

## 2. 人物识别（033-person-worker.exe）

```
powershell -ExecutionPolicy Bypass -File build\build_worker.ps1
```

输出 `out\semantic\033-person-worker.exe`。运行时它从自己旁边加载 `onnxruntime.dll`、`DirectML.dll` 和 `yolo11n-seg.onnx`（安装包里带着）。

## 3. 入口（dxgi.dll）

```
python build\build_host.py
```

先离线生成 GLAD（只读 `host\deps\khronos` 里的 XML，不联网），再编 `host\ReShade.vcxproj`（Release x64），输出 `out\host\dxgi.dll`。

## 4. 安装器启动器（033安装器.exe）

```
powershell -ExecutionPolicy Bypass -File build\build_launcher.ps1
```

## 5. 组装安装包

`installer/` 就是安装包里除程序文件以外的全部内容。把上面编出来的文件和第三方原件（NVIDIA `nvngx_dlssnr.dll`、ONNX Runtime、DirectML、YOLO11 模型、VC++ 运行库、20/30 转接件）放到 `installer\033\033-package.json` 里 `Profiles[0].Files` 各项 `Source` 写的位置（`installer\033\payload\…`），SHA-256 要和清单对上。安装器装之前会逐个核对，对不上就不装。

## 核对：和 V6.2 安装包逐字节比

```
python build\verify_binaries.py <安装包里的文件> <自己编的文件> [--map 原编译路径=自己的编译路径]
```

编译器会把编译时间，以及源码和输出所在的路径写进程序。这个工具比较前把时间戳和调试 GUID 清零，并把 `--map` 给的路径换成对方的路径。路径要等长，所以请在和原编译路径一样长的目录里编。其余每个字节都必须相同。

## 2026-09-22 用本仓库核对的结果

| 文件 | 结果 |
|---|---|
| `nvngx.dll_033.dll` | 和安装包逐字节一致（只差时间戳） |
| `033-person-worker.exe` | 和安装包逐字节一致（只差时间戳） |
| `dxgi.dll` | 和安装包逐字节一致（只差时间戳和调试 GUID） |
| `033安装器.exe` | 只差时间戳和 C# 编译器每次随机生成的 16 字节模块 ID |
| `033-engine.dll` | 见下 |

`033-engine.dll`：MSVC 给匿名命名空间起内部名字时用到源码文件的完整路径，而这个名字会影响一部分数据的排列顺序，所以换个目录编，核心会有数据前后挪动（大小不变）。核对办法：在同一个目录里，分别用本仓库和编出安装包那份核心的完整源码树（7330 个文件，包括本仓库去掉的文档、示例、备份和测试数据）各编一次，两份 `033-engine.dll` 逐字节一致（只差时间戳和核心自己记下的编译时间字符串）；两份和安装包里的核心比，差的字节也完全一样（都是 9840 个，只因为编译目录不同）。也就是说，本仓库就是安装包里那份核心的完整源码，去掉的文件编译时用不到。

