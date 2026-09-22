param([ValidateSet('dlss','fsr21','fsr22','xess','ffx')][string]$Backend='fsr22',[switch]$RealNR,[switch]$ProxyMount,[int]$TestLeaseCapacity=0,[switch]$TwoPassProbe)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$taskVendor=Join-Path $taskRoot 'third_party\OptiScaler033'
$taskOut=Join-Path $taskRoot "build\033-integrated-candidate_20260906\smoke-$Backend"
if($RealNR){$taskOut+='-real-nr'}
if($ProxyMount){$taskOut+='-proxy'}
if($TestLeaseCapacity){$taskOut+="-capacity$TestLeaseCapacity"}
if($TwoPassProbe){$taskOut+='-two-pass';$env:K033_TWO_PASS_PROBE='1'}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$env:K033_TEST_HARDWARE='1'
$taskCore=Join-Path $taskRoot 'build\033-integrated-candidate_20260906\core\033-render-core.dll'
$taskCoreName='033-render-core.dll';if($ProxyMount){$taskCoreName='winmm.dll'}
Copy-Item -LiteralPath $taskCore -Destination (Join-Path $taskOut $taskCoreName) -Force
if($RealNR){
    Copy-Item -LiteralPath (Join-Path $taskRoot '分发包\DLSS5一键包 v5.0\工具\运行时\nvngx_dlssnr.dll') -Destination $taskOut -Force
    Copy-Item -LiteralPath (Join-Path $taskRoot 'build\033-integrated-candidate_20260906\addon\nvngx.dll_033.dll') -Destination $taskOut -Force
}
if($Backend -eq 'xess') {Copy-Item -LiteralPath (Join-Path $taskVendor 'external\xess\bin\libxess.dll') -Destination $taskOut -Force}
if($Backend -eq 'ffx') {
    foreach($part in @('external\FidelityFX-SDK-v2\Kits\FidelityFX\signedbin','external\FidelityFX-SDK\PrebuiltSignedDLL')) {
        Get-ChildItem -LiteralPath (Join-Path $taskVendor $part) -Filter '*.dll' | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $taskOut -Force}
    }
}
$taskDlss='false'
if($Backend -eq 'dlss') {
    $taskDlss='true'
    Copy-Item -LiteralPath (Join-Path $taskRoot '分发包\DLSS5一键包 v5.0\工具\运行时\nvngx_dlss.dll') -Destination $taskOut -Force
}
$taskIni=@"
[Upscalers]
Dx12Upscaler=$Backend
[DLSS]
Enabled=$taskDlss
[FrameGen]
Enabled=false
[Menu]
OverlayMenu=false
[Plugins]
LoadReShade=false
LoadAsiPlugins=false
[Hooks]
RestoreComputeSignature=false
RestoreGraphicSignature=false
[Hotfix]
SkipFirstFrames=0
DisableOverlays=true
[DlssNr]
Enabled=false
[Log]
LogToFile=true
LogLevel=2
"@
[IO.File]::WriteAllText((Join-Path $taskOut 'OptiScaler.ini'),$taskIni,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $taskOut 'dlss5-033.cfg'),"nrprovider=0`n",[Text.Encoding]::ASCII)
Push-Location $taskOut
try {
    $taskCommon=@('/nologo','/std:c++17','/O2','/MT','/EHa','/utf-8','/W3','/DNOMINMAX','/I',"$taskRoot\sdk",'/I',"$taskRoot\src")
    if($TestLeaseCapacity){$taskCommon+="/DK033_TEST_LEASE_CAPACITY=$TestLeaseCapacity"}
    if($RealNR){$taskCommon+=@('/DK033_REAL_ADDON_SMOKE','/DK033_INTEGRATED_CANDIDATE','/I',"$taskRoot\sdk\reshade-6.8.0\include",'/I',"$taskRoot\sdk\imgui-1.92.5-docking");$env:LIB+=";$taskRoot\sdk\lib"}
    & cl.exe @taskCommon /LD "$taskRoot\test\core_callback_fixture.cpp" /link /OUT:dlss5-033.addon64
    if($LASTEXITCODE){throw 'Fixture compile failed'}
    & cl.exe @taskCommon "$taskRoot\test\core_backend_smoke_test.cpp" d3d12.lib dxgi.lib d3dcompiler.lib winmm.lib user32.lib advapi32.lib psapi.lib ole32.lib windowscodecs.lib /link /OUT:core_backend_smoke_test.exe
    if($LASTEXITCODE){throw 'Core smoke test compile failed'}
    & .\core_backend_smoke_test.exe (Join-Path $taskOut $taskCoreName)
    if($LASTEXITCODE){throw "$Backend core smoke failed ($LASTEXITCODE)"}
}finally{Pop-Location}
