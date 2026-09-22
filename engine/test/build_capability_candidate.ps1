param([string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\033-integrated-candidate_20260906\addon'))
$ErrorActionPreference = 'Stop'
$taskMsvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk = 'C:\Program Files (x86)\Windows Kits\10'
$taskSdkVersion = '10.0.26100.0'
$taskRoot = Split-Path -Parent $PSScriptRoot
$env:INCLUDE = "$taskMsvc\include;$taskSdk\Include\$taskSdkVersion\ucrt;$taskSdk\Include\$taskSdkVersion\um;$taskSdk\Include\$taskSdkVersion\shared;$taskSdk\Include\$taskSdkVersion\winrt"
$env:LIB = "$taskMsvc\lib\x64;$taskSdk\Lib\$taskSdkVersion\ucrt\x64;$taskSdk\Lib\$taskSdkVersion\um\x64;$taskRoot\sdk\lib"
$env:PATH = "$taskMsvc\bin\Hostx64\x64;" + $env:PATH
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
Push-Location $OutputDirectory
try {
    & "$taskSdk\bin\$taskSdkVersion\x64\rc.exe" /nologo /fo dlss5_033.res "$taskRoot\src\dlss5_033.rc"
    if ($LASTEXITCODE) { throw 'Resource compiler failed' }
    $common = @('/nologo','/std:c++17','/O2','/MT','/EHa','/utf-8','/W3','/DK033_INTEGRATED_CANDIDATE',
        '/I',"$taskRoot\sdk\reshade-6.8.0\include",'/I',"$taskRoot\sdk\imgui-1.92.5-docking",'/I',"$taskRoot\sdk",'/I',"$taskRoot\src")
    & cl.exe @common /LD "$taskRoot\src\dlss5_033.cpp" dlss5_033.res user32.lib advapi32.lib dxgi.lib psapi.lib ole32.lib windowscodecs.lib /link /OUT:dlss5-033.addon64
    if ($LASTEXITCODE) { throw 'Add-on build failed' }
    & cl.exe @common /LD "$taskRoot\src\nr033fwd.cpp" /link /OUT:nvngx.dll_033.dll
    if ($LASTEXITCODE) { throw 'Forwarder build failed' }
    foreach ($test in @('scale_shader_compile_test','gpu_pipeline_create_test','nr_contract_test','nr_color_roundtrip_test','nr_color_stability_test','gputime_fence_test','nr_call_scope_test','mfg_latency_test','gputime_submission_test','nr_feature_routing_test','config_store_test','motion_sharpen_test','resolve_leases_test','pregrade_test','render_core_policy_test','nr_snapshot_test','portrait_resolve_test')) {
        $testPath = Join-Path $taskRoot "test\$test.cpp"
        if (-not (Test-Path -LiteralPath $testPath)) { continue }
        & cl.exe @common $testPath d3dcompiler.lib d3d12.lib dxgi.lib ole32.lib windowscodecs.lib detours.lib /link "/OUT:$test.exe"
        if ($LASTEXITCODE) { throw "$test build failed" }
        & (Join-Path $OutputDirectory "$test.exe")
        if ($LASTEXITCODE) { throw "$test failed" }
    }
    Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'dlss5-033.addon64'),(Join-Path $OutputDirectory 'nvngx.dll_033.dll') | Select-Object Path,Hash | ConvertTo-Json
} finally { Pop-Location }
