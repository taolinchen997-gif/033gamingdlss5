param([string]$OutputDirectory='E:\033插件\build\universal-fg-test')
$ErrorActionPreference='Stop';$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64";$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\033-single-engine_20260906\033-engine.dll') -Destination (Join-Path $OutputDirectory 'winmm.dll') -Force
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'OptiScaler.ini'),"[Menu]`nOverlayMenu=false`n[Plugins]`nLoadReShade=false`nLoadAsiPlugins=false`n[FrameGen]`nFGInput=nofg`nFGOutput=nofg`n[Hotfix]`nDisableOverlays=true`n[Log]`nLogToFile=true`nLogLevel=2`n",[Text.Encoding]::ASCII)
Push-Location $OutputDirectory
try{& cl.exe /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "$PSScriptRoot\universal_fg_present_test.cpp" d3d12.lib dxgi.lib d3dcompiler.lib user32.lib /link /OUT:universal_fg_present_test.exe
 if($LASTEXITCODE){throw 'Universal FG test compilation failed'}
 & .\universal_fg_present_test.exe (Join-Path $OutputDirectory 'winmm.dll') (Join-Path $taskRoot 'third_party\OptiScaler033\external\FidelityFX-SDK-v2\Kits\FidelityFX\signedbin\amd_fidelityfx_framegeneration_dx12.dll')
 if($LASTEXITCODE){throw "Universal FG test failed: $LASTEXITCODE"}
 & cl.exe /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "$PSScriptRoot\universal_fg_colour_test.cpp" d3d12.lib dxgi.lib d3dcompiler.lib /link /OUT:universal_fg_colour_test.exe
 if($LASTEXITCODE){throw 'Colour test compilation failed'}
 & .\universal_fg_colour_test.exe
 if($LASTEXITCODE){throw 'Universal FG colour check failed'}
}finally{Pop-Location}
