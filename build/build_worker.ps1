param([string]$OutputDirectory,
      [string]$VisualStudio='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',[string]$MsvcVersion='14.44.35207',
      [string]$WindowsSdk='C:\Program Files (x86)\Windows Kits\10',[string]$WindowsSdkVersion='10.0.26100.0')
$ErrorActionPreference='Stop'
# 人物识别程序 033-person-worker.exe：和 V6.2 安装包里那份是同一条编译命令（一个编译单元，/O2 /MD，不带调试信息）。
# 它运行时从自己旁边加载 onnxruntime.dll / DirectML.dll 和 yolo11n-seg.onnx（安装包里带着）。
$repo=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $repo 'out\semantic'}
$source=Join-Path $repo 'semantic'
$msvc=Join-Path $VisualStudio ('VC\Tools\MSVC\'+$MsvcVersion)
if(!(Test-Path -LiteralPath (Join-Path $msvc 'bin\Hostx64\x64\cl.exe'))){throw ('找不到 MSVC '+$MsvcVersion+'：'+$msvc)}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$env:INCLUDE=(@("$msvc\include")+@(foreach($part in 'ucrt','um','shared','winrt'){"$WindowsSdk\Include\$WindowsSdkVersion\$part"})+@("$source\third_party\onnxruntime")) -join ';'
$env:LIB=@("$msvc\lib\x64","$WindowsSdk\Lib\$WindowsSdkVersion\ucrt\x64","$WindowsSdk\Lib\$WindowsSdkVersion\um\x64") -join ';'
Push-Location $OutputDirectory
try{
 & (Join-Path $msvc 'bin\Hostx64\x64\cl.exe') /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR (Join-Path $source 'person_worker.cpp') /Fe:033-person-worker.exe
 if($LASTEXITCODE){throw '识别程序编译失败'}
 Get-FileHash -LiteralPath (Join-Path $OutputDirectory '033-person-worker.exe')|Select-Object Path,Hash|Format-List
}finally{Pop-Location}
