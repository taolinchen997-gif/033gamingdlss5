param([string]$OutputDirectory='E:\033插件\build\portrait-controls-tests',[string]$Fixture='',
 [string[]]$Tests=@('mfg_identity_test','mfg_latency_test','scale_shader_compile_test','resolve_leases_test','nr_color_roundtrip_test','stacked_quality_test','config_store_test'))
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64;$taskRoot\sdk\lib"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$env:K033_TEST_HARDWARE='0'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$common=@('/nologo','/std:c++17','/O2','/MD','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR','/EHa','/utf-8','/I',"$taskRoot\sdk\reshade-6.8.0\include",'/I',"$taskRoot\sdk\imgui-1.92.5-docking",'/I',"$taskRoot\sdk",'/I',"$taskRoot\src")
Push-Location $OutputDirectory
try {
 & cl.exe @common /LD "$PSScriptRoot\mfg_identity_fixture.cpp" /link /OUT:mfg_identity_fixture.dll
 if($LASTEXITCODE){throw 'MFG fixture compile failed'}
 foreach($test in $Tests){
  & cl.exe @common "$PSScriptRoot\$test.cpp" d3dcompiler.lib d3d12.lib dxgi.lib ole32.lib windowscodecs.lib detours.lib /link "/OUT:$test.exe"
  if($LASTEXITCODE){throw "$test compile failed"}
  & (Join-Path $OutputDirectory "$test.exe")
  if($LASTEXITCODE){throw "$test failed"}
 }
 if($Fixture){
  $faces=@('facedetectcnn','facedetectcnn-model','facedetectcnn-data') | ForEach-Object {"$taskRoot\third_party\FaceDetect033\src\$_.cpp"}
  & cl.exe @common "$PSScriptRoot\portrait_input_test.cpp" @faces d3dcompiler.lib d3d12.lib dxgi.lib ole32.lib windowscodecs.lib detours.lib /link /OUT:portrait_input_test.exe
  if($LASTEXITCODE){throw 'Portrait test compile failed'}
  & .\portrait_input_test.exe $Fixture
  if($LASTEXITCODE){throw 'Portrait test failed'}
 }
}finally{Pop-Location}
