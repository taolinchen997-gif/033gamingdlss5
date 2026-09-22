$ErrorActionPreference='Stop'
$msvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$sdk='C:\Program Files (x86)\Windows Kits\10';$ver='10.0.26100.0'
$env:INCLUDE="$msvc\include;$sdk\Include\$ver\ucrt;$sdk\Include\$ver\um;$sdk\Include\$ver\shared;$sdk\Include\$ver\winrt"
$env:LIB="$msvc\lib\x64;$sdk\Lib\$ver\ucrt\x64;$sdk\Lib\$ver\um\x64"
$env:PATH="$msvc\bin\Hostx64\x64;"+$env:PATH
$out='E:\033插件\build\033-shader-driver-check_20260906'
New-Item -ItemType Directory -Path $out -Force|Out-Null
Push-Location $out
$previous=$env:K033_TEST_HARDWARE
try{
    $env:K033_TEST_HARDWARE='1'
    foreach($name in @('nr_color_roundtrip_test','nr_color_stability_test','gputime_fence_test')){
        & cl.exe /nologo /std:c++17 /O2 /MT /EHa /utf-8 /W3 (Join-Path $PSScriptRoot "$name.cpp") d3d12.lib dxgi.lib d3dcompiler.lib /link "/OUT:$name.exe"
        if($LASTEXITCODE){throw "$name compilation failed"}
        & (Join-Path $out "$name.exe")
        if($LASTEXITCODE){throw "$name hardware verification failed"}
    }
}finally{$env:K033_TEST_HARDWARE=$previous;Pop-Location}
