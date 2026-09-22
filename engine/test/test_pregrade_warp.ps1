param([string]$OutputDirectory='E:\033插件\build\pregrade-warp')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$msvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$sdk='C:\Program Files (x86)\Windows Kits\10';$v='10.0.26100.0'
$env:INCLUDE="$msvc\include;$sdk\Include\$v\ucrt;$sdk\Include\$v\um;$sdk\Include\$v\shared;$sdk\Include\$v\winrt"
$env:LIB="$msvc\lib\x64;$sdk\Lib\$v\ucrt\x64;$sdk\Lib\$v\um\x64;$root\sdk\lib"
$env:K033_TEST_HARDWARE='0'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try {
 & "$msvc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /EHa /utf-8 /I "$root\sdk\reshade-6.8.0\include" /I "$root\sdk\imgui-1.92.5-docking" /I "$root\sdk" /I "$root\src" "$PSScriptRoot\pregrade_test.cpp" d3dcompiler.lib d3d12.lib dxgi.lib ole32.lib windowscodecs.lib detours.lib /link /OUT:pregrade_test.exe
 if($LASTEXITCODE){throw 'Pregrade WARP compile failed'}
 & .\pregrade_test.exe
 if($LASTEXITCODE){throw 'Pregrade WARP test failed'}
} finally {Pop-Location}
