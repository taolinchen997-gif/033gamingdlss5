param([ValidateSet('dlss','fsr21','fsr22','xess','ffx')][string]$Backend='dlss',[ValidateRange(1,4)][int]$Passes=1,[switch]$Reconfigure,[switch]$LegacyGameRuntime,[switch]$Stress,[switch]$Benchmark,[switch]$Wide,[ValidateRange(50,100)][int]$ExtraWork=100,[switch]$ControlAudit,[switch]$EdgeAudit,[switch]$StyleSwitch,[ValidateRange(0,3)][int]$InputStyle=0,[string]$BinaryDirectory='',[ValidatePattern('^[a-z0-9-]*$')][string]$TestLabel='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskVendor=Join-Path $taskRoot 'third_party\OptiScaler033'
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$env:K033_TEST_HARDWARE='1'
$env:K033_TEST_BENCHMARK=if($Benchmark){'1'}else{'0'}
$env:K033_TEST_WIDE=if($Wide){'1'}else{'0'}
$env:K033_TEST_STYLE_SWITCH=if($StyleSwitch){'1'}else{'0'}
$env:K033_TEST_EDGE_AUDIT=if($EdgeAudit){'1'}else{'0'}
$env:K033_TEST_CONTROL_AUDIT=if($ControlAudit -or $EdgeAudit){'1'}else{'0'}
$env:K033_TEST_STRESS=if($Stress){'1'}else{'0'}
$env:K033_TEST_RECONFIGURE=if($Reconfigure){'1'}else{'0'}
$env:K033_TEST_LEGACY_CRT=if($LegacyGameRuntime){'1'}else{'0'}
$taskOut=Join-Path $taskRoot "build\033-single-engine_20260906\gpu-$Backend"
if($Passes -ne 1){$taskOut+="-passes$Passes"}
if($Reconfigure){$taskOut+='-reconfigure'}
if($Stress){$taskOut+='-stress'}
if($ControlAudit){$taskOut+='-controls'}
if($EdgeAudit){$taskOut+='-edgeprobe'}
if($StyleSwitch){$taskOut+='-styleswitch'}
if($InputStyle){$taskOut+="-style$InputStyle"}
if($Benchmark){$taskOut+='-720p'}
if($Wide){$taskOut+='-5120x2160'}
if($ExtraWork -ne 100){$taskOut+="-extra$ExtraWork"}
if($LegacyGameRuntime){$taskOut+='-legacy-crt'}
if($TestLabel){$taskOut+='-'+$TestLabel}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
if($LegacyGameRuntime){foreach($taskDll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')){Copy-Item -LiteralPath (Join-Path 'E:\yysls\yysls_medium\Engine\Binaries\Win64rh' $taskDll) -Destination $taskOut -Force}}
$taskBuild=if($BinaryDirectory){$BinaryDirectory}else{Join-Path $taskRoot 'build\033-single-engine_20260906'}
Copy-Item -LiteralPath (Join-Path $taskBuild '033-engine.dll') -Destination (Join-Path $taskOut 'winmm.dll') -Force
foreach($name in @('dlss5-033.addon64','nvngx.dll_033.dll')){Copy-Item -LiteralPath (Join-Path $taskBuild $name) -Destination $taskOut -Force}
Copy-Item -LiteralPath (Join-Path $taskRoot '备份\燕云_033六倍手测通过_20260906_034539\game\dxgi.dll') -Destination $taskOut -Force
foreach($name in @('nvngx_dlss.dll','nvngx_dlssnr.dll')){Copy-Item -LiteralPath (Join-Path $taskRoot "分发包\DLSS5一键包 v5.0\工具\运行时\$name") -Destination $taskOut -Force}
if($Backend -eq 'xess'){Copy-Item -LiteralPath (Join-Path $taskVendor 'external\xess\bin\libxess.dll') -Destination $taskOut -Force}
if($Backend -eq 'ffx'){foreach($part in @('external\FidelityFX-SDK-v2\Kits\FidelityFX\signedbin','external\FidelityFX-SDK\PrebuiltSignedDLL')){Get-ChildItem -LiteralPath (Join-Path $taskVendor $part) -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $taskOut -Force}}}
$taskIni=@"
[Upscalers]
Dx12Upscaler=$Backend
[DLSS]
Enabled=true
[FrameGen]
Enabled=false
FGInput=nofg
FGOutput=nofg
[Plugins]
LoadReShade=false
LoadAsiPlugins=false
[fakenvapi]
UseFakenvapi=false
[Menu]
OverlayMenu=true
[Hooks]
RestoreComputeSignature=false
RestoreGraphicSignature=false
[Hotfix]
SkipFirstFrames=0
[Log]
LogToFile=true
LogLevel=2
[DlssNr]
Enabled=true
"@
[IO.File]::WriteAllText((Join-Path $taskOut 'OptiScaler.ini'),$taskIni,[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $taskOut 'dlss5-033.cfg'),"engine=1`ninject=1`nwork=100`npasses=$Passes`npasswork=$ExtraWork`nprestyle=$InputStyle`nprestylestrength=100`nmodelfull=1`nreplica=1`npreset=3`nstyle=2`nrestorestate=0`ngputime=1`nmfg=0`ncreate_delay=0`n",[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $taskOut 'dlss5-033.state'),'0',[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $taskOut 'ReShade.ini'),"[GENERAL]`nNoReloadOnInit=1`n[OVERLAY]`nTutorialProgress=4`nShowClock=0`nShowFPS=0`nShowFrameTime=0`n",[Text.Encoding]::ASCII)
Push-Location $taskOut
try{
 & cl.exe /nologo /std:c++17 /O2 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /utf-8 /EHsc /LD /I "$taskBuild\reshade-sdk" /I "$taskRoot\sdk\reshade-6.8.0\include" /I "$taskRoot\sdk\imgui-1.92.5-docking" /I "$taskRoot\src" "$taskRoot\test\single_engine_reshade_ui_probe.cpp" /link /OUT:zz-033-ui-test.addon64
 if($LASTEXITCODE){throw 'Actual ReShade UI probe compile failed'}
 & cl.exe /nologo /std:c++17 /O2 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /utf-8 /EHsc /I "$taskRoot\src" /I "$taskRoot\sdk" "$taskRoot\test\single_engine_gpu_test.cpp" d3d12.lib dxgi.lib d3dcompiler.lib winmm.lib user32.lib /link /OUT:single_engine_gpu_test.exe
 if($LASTEXITCODE){throw 'GPU probe compile failed'}
 Write-Output ('ENGINE_SHA256='+ (Get-FileHash -LiteralPath (Join-Path $taskOut 'winmm.dll')).Hash)
 Write-Output ('FORWARDER_SHA256='+ (Get-FileHash -LiteralPath (Join-Path $taskOut 'nvngx.dll_033.dll')).Hash)
 & .\single_engine_gpu_test.exe (Join-Path $taskOut 'winmm.dll')
 if($LASTEXITCODE){throw "Single-engine GPU probe failed: $LASTEXITCODE"}
}finally{Pop-Location}
