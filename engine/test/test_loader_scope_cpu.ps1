param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try {
    $taskSource=Join-Path $PSScriptRoot 'loader_scope_cpu_test.cpp'
    $taskOptions=@('/nologo','/std:c++20','/O2','/MD','/EHsc','/utf-8','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR')
    & cl.exe @taskOptions /DK033_TEST_LEGACY $taskSource /Folegacy_scope.obj /link /OUT:legacy_scope.exe
    if($LASTEXITCODE){throw 'Legacy scope fixture failed to compile'}
    & (Join-Path $OutputDirectory 'legacy_scope.exe')
    if($LASTEXITCODE -ne 1){throw 'Legacy baseline must fail the deterministic checks'}
    Write-Output 'Expected v32 failure confirmed without concurrent mutation of the legacy map.'
    & cl.exe @taskOptions $taskSource /Foloader_scope.obj /link /OUT:loader_scope.exe
    if($LASTEXITCODE){throw 'Loader scope checks failed to compile'}
    & (Join-Path $OutputDirectory 'loader_scope.exe')
    if($LASTEXITCODE){throw 'Loader scope checks failed'}
} finally { Pop-Location }
