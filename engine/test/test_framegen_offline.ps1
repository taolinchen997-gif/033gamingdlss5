param([string]$OutputDirectory='E:\033插件\build\framegen-offline',[switch]$BuiltEngine)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try {
 & cl.exe /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "$PSScriptRoot\framegen_offline_test.cpp" d3d11.lib d3dcompiler.lib dxgi.lib /link /OUT:framegen_offline_test.exe
 if($LASTEXITCODE){throw 'Frame-generation compile failed'}
 & .\framegen_offline_test.exe
 if($LASTEXITCODE){throw 'Frame-generation offline checks failed'}
 & cl.exe /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "$PSScriptRoot\framegen_dx12_test.cpp" d3d12.lib d3dcompiler.lib dxgi.lib /link /OUT:framegen_dx12_test.exe
 if($LASTEXITCODE){throw 'DX12 frame-generation compile failed'}
 & .\framegen_dx12_test.exe
 if($LASTEXITCODE){throw 'DX12 frame-generation offline checks failed'}
 if($BuiltEngine){
  $taskEngine=Join-Path $OutputDirectory 'winmm.dll'
  Copy-Item -LiteralPath (Join-Path $taskRoot 'build\033-single-engine_20260906\033-engine.dll') -Destination $taskEngine -Force
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'OptiScaler.ini'),"[Menu]`nOverlayMenu=false`n[Plugins]`nLoadReShade=false`nLoadAsiPlugins=false`n[Log]`nLogToFile=true`nLogLevel=2`n[Hotfix]`nDisableOverlays=true`n",[Text.Encoding]::ASCII)
  Write-Output ('BUILT_ENGINE_SHA256='+ (Get-FileHash -LiteralPath $taskEngine).Hash)
  & .\framegen_dx12_test.exe $taskEngine
  if($LASTEXITCODE){throw 'Built-engine DX12 frame-generation checks failed'}
 }
}finally{Pop-Location}
