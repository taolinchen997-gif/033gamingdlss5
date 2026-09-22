param([string]$OutputDirectory=(Join-Path (Split-Path -Parent $PSScriptRoot) 'build\033-single-engine_20260906'))
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskVendor=Join-Path $taskRoot 'third_party\OptiScaler033'
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$taskBuild='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$taskOut=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')+'\'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64;$taskRoot\sdk\lib"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
# The retained core overlay uses ImGui 19196, while the ReShade interface uses
# 19250. Isolate every public type/symbol in the host translation unit. Never
# share contexts, allocators or accidentally coalesce incompatible inline code.
$taskImgui=[IO.File]::ReadAllText((Join-Path $taskRoot 'sdk\imgui-1.92.5-docking\imgui.h'))
$taskTokens=[regex]::Matches($taskImgui,'\bIm[A-Za-z0-9_]+\b') | ForEach-Object {$_.Value} | Sort-Object -Unique
$taskConditional=[regex]::Matches($taskImgui,'(?m)^\s*#\s*(?:ifndef|ifdef|define)\s+(Im[A-Za-z0-9_]+)') | ForEach-Object {$_.Groups[1].Value}
$taskMacros=@('#pragma once')
foreach($token in $taskTokens){if($token -ne 'ImTextureID' -and $token -notin $taskConditional){$taskMacros+="#define $token K033Host_$token"}}
$taskNamespace=Join-Path $taskOut 'reshade_imgui_isolation.h'
[IO.File]::WriteAllLines($taskNamespace,$taskMacros,[Text.Encoding]::ASCII)
# Callbacks live in the engine, while ReShade owns the small registration
# adapter. Use the SDK's explicit-owner exports instead of address guessing.
$taskReshadeSdk=Join-Path $taskOut 'reshade-sdk'
New-Item -ItemType Directory -Path $taskReshadeSdk -Force | Out-Null
$taskReshade=[IO.File]::ReadAllText((Join-Path $taskRoot 'sdk\reshade-6.8.0\include\reshade.hpp'))
foreach($verb in @('Register','Unregister')){
 $taskReshade=$taskReshade.Replace("`"ReShade${verb}Event`"","`"ReShade${verb}EventForAddon`"")
 $taskReshade=$taskReshade.Replace("`"ReShade${verb}Overlay`"","`"ReShade${verb}OverlayForAddon`"")
}
$taskReshade=$taskReshade.Replace('reinterpret_cast<void(*)(addon_event, void *)>','reinterpret_cast<void(*)(void *, addon_event, void *)>')
$taskReshade=$taskReshade.Replace('func(ev, reinterpret_cast<void *>(callback));','func(internal::get_current_module_handle(), ev, reinterpret_cast<void *>(callback));')
$taskReshade=$taskReshade.Replace('reinterpret_cast<void(*)(const char *, void(*)(api::effect_runtime *))>','reinterpret_cast<void(*)(void *, const char *, void(*)(api::effect_runtime *))>')
$taskReshade=$taskReshade.Replace('func(title, callback);','func(internal::get_current_module_handle(), title, callback);')
[IO.File]::WriteAllText((Join-Path $taskReshadeSdk 'reshade.hpp'),$taskReshade,[Text.UTF8Encoding]::new($false))
# Match the core project's mutex ABI policy. Games can preload a private
# MSVCP 14.29; the new constexpr constructor would leave its lock object invalid.
$taskCommon=@('/nologo','/std:c++17','/O2','/MD','/EHa','/utf-8','/W3','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR','/DK033_MONOLITHIC_ENGINE','/DK033_INTEGRATED_CANDIDATE','/DK033_BETA2_RESHADE_HOST',
 '/I',$taskReshadeSdk,'/I',"$taskRoot\sdk\reshade-6.8.0\include",'/I',"$taskRoot\sdk\imgui-1.92.5-docking",'/I',"$taskRoot\sdk",'/I',"$taskRoot\src")
Push-Location $taskOut
try {
 # CPU-only SR dimensions/bank policy and shared codec migration fixtures.
 # Both codec executables operate on one private file in this fixed build cwd.
 & cl.exe @taskCommon "$taskRoot\test\nr_layer_sr_cpu_test.cpp" /Fe:nr_layer_sr_cpu_test.exe
 if($LASTEXITCODE){throw 'Independent NR layer SR CPU compilation failed'}
 & .\nr_layer_sr_cpu_test.exe
 if($LASTEXITCODE){throw 'Independent NR layer SR CPU checks failed'}
 & cl.exe @taskCommon "$taskRoot\test\beta2_sr_settings_cpu_test.cpp" /Fe:beta2_sr_settings_cpu_test.exe
 if($LASTEXITCODE){throw 'SR settings CPU compilation failed'}
 & .\beta2_sr_settings_cpu_test.exe
 if($LASTEXITCODE){throw 'SR settings CPU checks failed'}
 & cl.exe @taskCommon /DK033_OLD_SR_READER "$taskRoot\test\beta2_sr_settings_cpu_test.cpp" /Fe:beta2_sr_settings_old_reader_cpu_test.exe
 if($LASTEXITCODE){throw 'Legacy SR settings reader CPU compilation failed'}
 & .\beta2_sr_settings_old_reader_cpu_test.exe
 if($LASTEXITCODE){throw 'Legacy settings compatibility CPU checks failed'}
 # Actual generated SDK with CPU import stubs. Each process has a fresh SDK
 # static cache: old null-owner ordering, fixed ordering, foreign-owner refusal.
 & cl.exe @taskCommon "$taskRoot\test\beta2_sdk_owner_cpu_test.cpp" /Fe:beta2_sdk_owner_cpu_test.exe
 if($LASTEXITCODE){throw 'SDK owner regression compilation failed'}
 foreach($taskCase in @('old','fixed','foreign')){
  & .\beta2_sdk_owner_cpu_test.exe $taskCase
  if($LASTEXITCODE){throw "SDK owner regression failed: $taskCase"}
 }
 # Pure CPU policy only: no D3D headers/devices, DLL loading or window creation.
 & cl.exe /nologo /std:c++17 /O2 /MD /utf-8 "$taskRoot\test\beta2_nr_cpu_test.cpp" /Fe:beta2_nr_cpu_test.exe
 if($LASTEXITCODE){throw 'Beta2 NR CPU test compilation failed'}
 & .\beta2_nr_cpu_test.exe
 if($LASTEXITCODE){throw 'Beta2 NR CPU contract failed'}
 # S54 policy failures and shader compilation only; neither creates a device.
 & cl.exe @taskCommon "$taskRoot\test\beta2_grade_cpu_test.cpp" /Fe:beta2_grade_cpu_test.exe
 if($LASTEXITCODE){throw 'S54 grade CPU compilation failed'}
 & .\beta2_grade_cpu_test.exe
 if($LASTEXITCODE){throw 'S54 grade CPU contract failed'}
 & cl.exe @taskCommon "$taskRoot\test\beta2_grade_shader_compile.cpp" /Fe:beta2_grade_shader_compile.exe /link d3dcompiler.lib
 if($LASTEXITCODE){throw 'S54 shader compiler fixture compilation failed'}
 & .\beta2_grade_shader_compile.exe
 if($LASTEXITCODE){throw 'S54 grade shader compilation failed'}
 & cl.exe @taskCommon /I $taskOut /c "$taskRoot\src\beta2_grade.cpp" /Fo033-grade.obj
 if($LASTEXITCODE){throw 'S54 grade native compilation failed'}
 # Reuse S53's shared settings owner as native objects in this feature core.
 # Neither object is loaded/executed by this build script.
 & cl.exe @taskCommon /c "$taskRoot\src\beta2_shared_settings.cpp" /Fo033-shared-settings.obj
 if($LASTEXITCODE){throw 'Beta2 shared settings compilation failed'}
 & cl.exe @taskCommon /c "$taskRoot\runtime\compat\preferences_native.cpp" /Fo033-preferences.obj
 if($LASTEXITCODE){throw 'Beta2 preferences owner compilation failed'}
 & cl.exe @taskCommon /c "/FI$taskNamespace" "$taskRoot\src\dlss5_033.cpp" /Fo033-renderer.obj
 if($LASTEXITCODE){throw 'Single-engine renderer compilation failed'}
 & cl.exe @taskCommon /c "$taskRoot\src\universal_fg.cpp" /Fo033-framegen.obj
 if($LASTEXITCODE){throw 'Universal frame-generation compilation failed'}
 $faceObjects=@()
 foreach($faceSource in @('facedetectcnn','facedetectcnn-model','facedetectcnn-data')){
  & cl.exe @taskCommon /c "$taskRoot\third_party\FaceDetect033\src\$faceSource.cpp" "/Fo$faceSource.obj"
  if($LASTEXITCODE){throw "Face detector compilation failed: $faceSource"}
  $faceObjects+="$faceSource.obj"
 }
 & lib.exe /nologo /OUT:033-face.lib @faceObjects
 if($LASTEXITCODE){throw 'Static face detector library failed'}
 & cl.exe /nologo /std:c++17 /O2 /MD /utf-8 /LD "$taskRoot\src\engine_reshade_adapter.cpp" /link /OUT:dlss5-033.addon64
 if($LASTEXITCODE){throw 'Registration adapter compilation failed'}
 $env:_CL_='/utf-8 /MP4 /DK033_BETA2_RESHADE_HOST'
 # Keep the renderer object explicit: its entry points are discovered through
 # GetProcAddress, so a normal archive linker would otherwise discard it.
 & $taskBuild (Join-Path $taskVendor 'OptiScaler\OptiScaler.vcxproj') /t:Build /m:1 /v:minimal /nologo /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$taskVendor\" "/p:OutDir=$taskOut" "/p:IntDir=${taskOut}obj\" /p:TargetName=033-engine /p:GenerateMapFile=true "/p:MapFileName=${taskOut}033-engine.map" /p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false "/p:K033EngineObject=${taskOut}033-renderer.obj" "/p:K033FramegenObject=${taskOut}033-framegen.obj" "/p:K033FaceLibrary=${taskOut}033-face.lib" "/p:K033SettingsObject=${taskOut}033-shared-settings.obj" "/p:K033PreferencesObject=${taskOut}033-preferences.obj" "/p:K033GradeObject=${taskOut}033-grade.obj"
 if($LASTEXITCODE){throw 'Single-engine link failed'}
 & cl.exe @taskCommon /LD "$taskRoot\src\nr033fwd.cpp" /link /OUT:nvngx.dll_033.dll
 if($LASTEXITCODE){throw 'NR runtime forwarder compilation failed'}
 & (Join-Path $PSScriptRoot 'test_universal_cpu.ps1') -OutputDirectory $taskOut -BuildOnly
 if($LASTEXITCODE){throw 'Private universal presentation build failed'}
 Get-FileHash -LiteralPath (Join-Path $taskOut '033-engine.dll'),(Join-Path $taskOut 'dlss5-033.addon64'),(Join-Path $taskOut 'nvngx.dll_033.dll'),(Join-Path $taskOut '033-framegen-provider.dll') | Select-Object Path,Hash | ConvertTo-Json
}finally{Pop-Location}
