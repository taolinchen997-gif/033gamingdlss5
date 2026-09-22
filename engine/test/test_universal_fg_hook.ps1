param([string]$OutputDirectory='E:\033插件\build\universal-fg-hook-test',[switch]$OverlayEnabled,[switch]$WithReShade,[switch]$WithFeeder,[string]$FeederAddon,[switch]$FeederEffects)
$ErrorActionPreference='Stop';$root=Split-Path -Parent $PSScriptRoot
$msvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$sdk='C:\Program Files (x86)\Windows Kits\10';$v='10.0.26100.0'
$env:INCLUDE="$msvc\include;$sdk\Include\$v\ucrt;$sdk\Include\$v\um;$sdk\Include\$v\shared;$sdk\Include\$v\winrt"
$env:LIB="$msvc\lib\x64;$sdk\Lib\$v\ucrt\x64;$sdk\Lib\$v\um\x64"
New-Item -ItemType Directory -Path $OutputDirectory,(Join-Path $OutputDirectory 'OptiScaler') -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'build\033-single-engine_20260906\033-engine.dll') -Destination (Join-Path $OutputDirectory 'winmm.dll') -Force
$amd=Join-Path $root 'third_party\OptiScaler033\external\FidelityFX-SDK-v2\Kits\FidelityFX\signedbin\amd_fidelityfx_framegeneration_dx12.dll'
Copy-Item -LiteralPath $amd -Destination (Join-Path $OutputDirectory 'OptiScaler') -Force
$menu=if($OverlayEnabled){'true'}else{'false'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'OptiScaler.ini'),"[Menu]`nOverlayMenu=$menu`n[Plugins]`nLoadReShade=false`nLoadAsiPlugins=false`n[FrameGen]`nFGInput=nofg`nFGOutput=nofg`n[Hotfix]`nDisableOverlays=true`n[Log]`nLogToFile=true`nLogLevel=2`n",[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'dlss5-033.cfg'),"imagefg=1`ninject=0`ncarrier=0`nengine=1`n",[Text.Encoding]::ASCII)
$extra=@()
if($WithReShade){
 Copy-Item -LiteralPath 'E:\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4\dxgi.dll' -Destination $OutputDirectory -Force
 Copy-Item -LiteralPath (Join-Path $root 'build\033-single-engine_20260906\dlss5-033.addon64') -Destination $OutputDirectory -Force
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'dlss5-033.state'),'0',[Text.Encoding]::ASCII)
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'ReShade.ini'),"[GENERAL]`nNoReloadOnInit=1`nPerformanceMode=1`n[INPUT]`nKeyOverlay=36,0,0,0`n[OVERLAY]`nTutorialProgress=4`n[RenoDX.DLSS5]`nEnableHooks=0`n",[Text.Encoding]::ASCII)
 $extra=@('/DUNIVERSAL_FG_RESHADE_TEST')
 if($WithFeeder){
  $feedPath=if($FeederAddon){$FeederAddon}else{'E:\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4\dlss5-feed.addon64'}
  Copy-Item -LiteralPath $feedPath -Destination (Join-Path $OutputDirectory 'dlss5-feed.addon64') -Force
  $extra+='/DUNIVERSAL_FG_FEEDER_TEST'
 }
 # Select the actual 033 tab in ReShade's native docking layout. This fixture
 # uses only a hidden private window and never sends global keyboard input.
 $layout="Window=[Window][热心网友033],Pos=0,,0,Size=192,,144,Collapsed=0,DockId=0x00000001,,0`nDocking=[Docking][Data],DockSpace ID=0xB0DF600F Window=0xCC18005E Pos=0,,0 Size=192,,144 Split=X, DockNode ID=0x00000001 Parent=0xB0DF600F SizeRef=192,,144 Selected=0x45CA78C8, DockNode ID=0x00000002 Parent=0xB0DF600F SizeRef=1,,144 CentralNode=1`n"
 $ini=Join-Path $OutputDirectory 'ReShade.ini'
 [IO.File]::WriteAllText($ini,([IO.File]::ReadAllText($ini).Replace("[OVERLAY]`n","[OVERLAY]`n$layout")),[Text.UTF8Encoding]::new($false))
 if($FeederEffects){
  if(-not $FeederAddon){throw 'Effects test requires the explicit mock-NGX Feeder path'}
  $extra+='/DUNIVERSAL_FG_FEEDER_EFFECTS_TEST'
  $shaders=Join-Path $OutputDirectory 'shaders';New-Item -ItemType Directory -Path $shaders -Force|Out-Null
  foreach($name in @('DLSS5_Feed.fx','ReShade.fxh')){Copy-Item -LiteralPath (Join-Path 'E:\Steam\steamapps\common\RESIDENT EVIL 4  BIOHAZARD RE4\reshade-shaders\Shaders' $name) -Destination $shaders -Force}
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'dlss5-feed.cfg'),"enabled=1`nmode=2`ncreate_delay=0`nwarmup_rebuild=0`nlog_frames=3`n",[Text.Encoding]::ASCII)
  [IO.File]::WriteAllText((Join-Path $OutputDirectory 'test-preset.ini'),"Techniques=DLSS5_Feed@DLSS5_Feed.fx`nTechniqueSorting=DLSS5_Feed@DLSS5_Feed.fx`n",[Text.Encoding]::ASCII)
  $contents=[IO.File]::ReadAllText($ini).Replace('NoReloadOnInit=1','NoReloadOnInit=0').Replace("[GENERAL]`n","[GENERAL]`nEffectSearchPaths=.\shaders`nPresetPath=.\test-preset.ini`nPreprocessorDefinitions=DLSS5_MV_PROVIDER=0`n")
  [IO.File]::WriteAllText($ini,$contents,[Text.UTF8Encoding]::new($false))
 }
}
Push-Location $OutputDirectory
try{
 if($WithReShade){
  & "$msvc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /LD "$PSScriptRoot\universal_fg_ui_probe.cpp" user32.lib /link /OUT:universal_fg_ui_probe.addon64
  if($LASTEXITCODE){throw 'ReShade UI probe compile failed'}
 }
 & "$msvc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /DUNIVERSAL_FG_HOOK_TEST @extra "$PSScriptRoot\universal_fg_present_test.cpp" d3d12.lib dxgi.lib d3dcompiler.lib user32.lib winmm.lib /link /OUT:universal_fg_hook_test.exe
 if($LASTEXITCODE){throw 'Hook test compile failed'}
 & .\universal_fg_hook_test.exe (Join-Path $OutputDirectory 'winmm.dll') $amd
 if($LASTEXITCODE){throw 'Actual game-style swapchain hook test failed'}
 if($WithReShade){
  if(-not(Select-String -LiteralPath (Join-Path $OutputDirectory 'dlss5-033.log') -SimpleMatch '[ui] 033 panel drawn by ReShade' -Quiet)){throw '033 panel callback was not drawn by the actual ReShade runtime'}
  Write-Output 'PASS actual 033 panel callback drawn by ReShade'
 }
}finally{Pop-Location}
