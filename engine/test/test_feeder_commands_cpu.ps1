param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskFeeder=Join-Path $taskRoot 'ref\DLSS5-Feeder-repo'
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskNativeLog=Join-Path $OutputDirectory ('native-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.log')

# Test the actual production function, not a separately maintained copy. The
# surrounding Feeder DLL requires NGX/ReShade; the fixture supplies CPU doubles
# for every object and wait used by this one function. No DLL/device is loaded.
$taskSourcePath=Join-Path $taskFeeder 'src\dlss5-feed.cpp'
$taskSource=[IO.File]::ReadAllText($taskSourcePath)
$taskStart=$taskSource.IndexOf('static bool BeginCommands()')
$taskEnd=$taskSource.IndexOf('static UINT64 EndCommands()')
if($taskStart -lt 0 -or $taskEnd -le $taskStart){throw 'Feeder command function boundaries changed; review extraction'}
$taskFunction=$taskSource.Substring($taskStart,$taskEnd-$taskStart)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'feeder_begin_commands_under_test.h'),$taskFunction,[Text.UTF8Encoding]::new($false))
Get-FileHash -LiteralPath $taskSourcePath | Select-Object Path,Hash | ConvertTo-Json | Tee-Object -FilePath $taskNativeLog
Push-Location $OutputDirectory
try {
    $taskCommon=@('/nologo','/std:c++20','/O2','/MD','/EHsc','/utf-8','/W4','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR')
    foreach($taskTest in @('queue_safety_cpu_test','feeder_commands_cpu_test')) {
        & "$taskMsvc\bin\Hostx64\x64\cl.exe" @taskCommon /I $OutputDirectory "$PSScriptRoot\$taskTest.cpp" /link "/OUT:$taskTest.exe" 2>&1 | Tee-Object -FilePath $taskNativeLog -Append
        if($LASTEXITCODE){throw "$taskTest compilation failed"}
        & (Join-Path $OutputDirectory "$taskTest.exe") 2>&1 | Tee-Object -FilePath $taskNativeLog -Append
        if($LASTEXITCODE){throw "$taskTest failed"}
    }
    # Compile the complete affected translation unit as well. /c emits an OBJ
    # only: no addon linking/loading, NGX initialization or driver invocation.
    $taskIncludes=@('/I',"$taskFeeder\external\reshade\include",'/I',"$taskRoot\sdk\ngx",'/I',"$taskRoot\third_party\OptiScaler033\external\vulkan\include",'/I',"$taskFeeder\external\imgui",'/I',"$taskFeeder\external\minhook\include")
    & "$taskMsvc\bin\Hostx64\x64\cl.exe" @taskCommon @taskIncludes /c "$taskSourcePath" /Fofeeder-compile-only.obj 2>&1 | Tee-Object -FilePath $taskNativeLog -Append
    if($LASTEXITCODE){throw 'Full Feeder translation unit compilation failed'}
    Write-Output 'Feeder CPU failure injection and full translation unit compile completed; no graphics device or driver calls.'
} finally { Pop-Location }
