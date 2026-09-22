param([switch]$LegacyGameRuntime)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$taskOut=Join-Path $taskRoot 'build\033-single-engine_20260906\ui-test'
if($LegacyGameRuntime){$taskOut+='-legacy-crt'}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if($LegacyGameRuntime){foreach($taskDll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')){Copy-Item -LiteralPath (Join-Path 'E:\yysls\yysls_medium\Engine\Binaries\Win64rh' $taskDll) -Destination $taskOut -Force}}
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\033-single-engine_20260906\033-engine.dll') -Destination (Join-Path $taskOut 'winmm.dll') -Force
[IO.File]::WriteAllText((Join-Path $taskOut 'OptiScaler.ini'),"[Menu]`nOverlayMenu=false`n[Plugins]`nLoadReShade=false`nLoadAsiPlugins=false`n[Log]`nLogToFile=true`nLogLevel=2`n[Hotfix]`nDisableOverlays=true`n",[Text.Encoding]::ASCII)
Push-Location $taskOut
try{
 $taskSafety=Join-Path $taskOut 'safety';New-Item -ItemType Directory -Path $taskSafety -Force | Out-Null
 & cl.exe /nologo /std:c++17 /MD /utf-8 /EHsc /I "$taskRoot\src" "$taskRoot\test\single_engine_safety_test.cpp" /link "/OUT:$taskSafety\single_engine_safety_test.exe"
 if($LASTEXITCODE){throw 'Safety fixture compile failed'}
 & "$taskSafety\single_engine_safety_test.exe"
 if($LASTEXITCODE){throw 'Safety fixture failed'}
 & cl.exe /nologo /std:c++17 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /utf-8 /EHsc /I "$taskRoot\src" "$taskRoot\test\single_engine_ui_test.cpp" /link /OUT:single_engine_ui_test.exe
 if($LASTEXITCODE){throw 'UI fixture compile failed'}
 Write-Output ('ENGINE_SHA256='+ (Get-FileHash -LiteralPath (Join-Path $taskOut 'winmm.dll')).Hash)
 & .\single_engine_ui_test.exe (Join-Path $taskOut 'winmm.dll')
 if($LASTEXITCODE){throw "Single-engine UI fixture failed: $LASTEXITCODE"}
}finally{Pop-Location}
