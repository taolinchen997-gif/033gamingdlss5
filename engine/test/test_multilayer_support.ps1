param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64;$taskRoot\sdk\lib"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$env:K033_TEST_HARDWARE='1'
$taskOut=Join-Path $taskRoot 'build\multilayer-support'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
Push-Location $taskOut
try{
 foreach($taskTest in @('resolve_leases_test','gputime_submission_test','nr_color_roundtrip_test','nr_color_stability_test','portrait_resolve_test','pregrade_test')){
  & cl.exe /nologo /std:c++17 /O2 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /EHa /utf-8 /I "$taskRoot\sdk\reshade-6.8.0\include" /I "$taskRoot\sdk\imgui-1.92.5-docking" /I "$taskRoot\sdk" /I "$taskRoot\src" "$taskRoot\test\$taskTest.cpp" d3dcompiler.lib d3d12.lib dxgi.lib ole32.lib windowscodecs.lib detours.lib /link "/OUT:$taskTest.exe"
  if($LASTEXITCODE){throw "$taskTest compile failed"}
  & (Join-Path $taskOut "$taskTest.exe")
  if($LASTEXITCODE){throw "$taskTest failed"}
 }
}finally{Pop-Location}
