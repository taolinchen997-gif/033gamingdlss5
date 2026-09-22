param([string]$OutputDirectory='E:\033插件\build\framegen-swapchain-probe')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try{
 & cl.exe /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR "$PSScriptRoot\framegen_swapchain_probe.cpp" d3d12.lib d3dcompiler.lib dxgi.lib user32.lib /link /OUT:framegen_swapchain_probe.exe
 if($LASTEXITCODE){throw 'Swapchain probe compile failed'}
 $taskLibrary=Join-Path $taskRoot 'third_party\OptiScaler033\external\FidelityFX-SDK-v2\Kits\FidelityFX\signedbin\amd_fidelityfx_framegeneration_dx12.dll'
 Write-Output ('FFX_LIBRARY_SHA256='+ (Get-FileHash -LiteralPath $taskLibrary).Hash)
 & .\framegen_swapchain_probe.exe $taskLibrary
 if($LASTEXITCODE){throw "Swapchain probe failed: $LASTEXITCODE"}
}finally{Pop-Location}
