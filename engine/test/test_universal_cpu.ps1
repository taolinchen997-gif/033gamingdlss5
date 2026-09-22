param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$BuildOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$taskSource=Join-Path $taskRoot 'third_party\FramePacing033'
$taskFfx=Join-Path $taskRoot 'third_party\OptiScaler033\external\FidelityFX-SDK-v2\Kits\FidelityFX'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try {
 $taskArgs=@('/nologo','/std:c++20','/O2','/MD','/EHsc','/utf-8','/DNOMINMAX','/DNDEBUG','/DUNICODE','/D_UNICODE','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR')
 $taskSources=@('FrameInterpolationSwapchainDX12','FrameInterpolationSwapchainDX12_Helpers','FrameInterpolationSwapchainDX12_UiComposition','FrameInterpolationSwapchainDX12_DebugPacing','backend_subset') | ForEach-Object {Join-Path $taskSource ($_+'.cpp')}
 & cl.exe @taskArgs "/FI$taskSource\build_prefix.h" /LD @taskSources "$taskFfx\api\internal\ffx_assert.cpp" "$taskFfx\api\internal\ffx_message.cpp" d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib dwmapi.lib winmm.lib user32.lib /link /OUT:033-framegen-provider.dll
 if($LASTEXITCODE){throw 'Private image provider build failed'}
 if($BuildOnly){return}
 # Compilation and CPU policy/shader compilation only. No provider/engine load,
 # window, D3D device creation, WARP, driver write or actual shader dispatch.
 & cl.exe @taskArgs "/I$taskRoot\sdk" "$PSScriptRoot\universal_fg_cpu_test.cpp" d3dcompiler.lib /link /OUT:universal_fg_cpu_test.exe
 if($LASTEXITCODE){throw 'Universal CPU test compilation failed'}
 & .\universal_fg_cpu_test.exe
 if($LASTEXITCODE){throw 'Universal CPU checks failed'}
} finally {Pop-Location}
