param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# Compile and execute only the pinned CPU test and private file fixtures. No production
# preferences thread, runtime DLL, desktop/game, GPU, SDK or service execution.
$ErrorActionPreference='Stop'
foreach($taskInjection in @('CL','_CL_','LINK','_LINK_')){if([Environment]::GetEnvironmentVariable($taskInjection,'Process')){throw ('Unexpected compiler option environment: '+$taskInjection)}}
$taskEngine=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskExpected=[IO.Path]::GetFullPath((Join-Path $taskEngine 'build/parallel-InstallerBarrierCpu'))
if([IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\') -ine $taskExpected){throw 'Use only the reviewed InstallerBarrierCpu wrapper output'}
for($taskCursor=$taskExpected;$taskCursor;$taskCursor=[IO.Path]::GetDirectoryName($taskCursor)){
    if(Test-Path -LiteralPath $taskCursor){
        $taskItem=Get-Item -LiteralPath $taskCursor -Force
        if(-not $taskItem.PSIsContainer -or ($taskItem.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Fixture output ancestor is not a plain physical directory'}
    }
}
$taskManifestPath=Join-Path $PSScriptRoot 'shared_settings_barrier_inputs.json'
$taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$taskRepo=Split-Path -Parent $taskEngine
$taskSources=@()
foreach($taskRow in $taskManifest.files){
    $taskSource=[IO.Path]::GetFullPath((Join-Path $taskRepo $taskRow.path))
    if(-not $taskSource.StartsWith($taskEngine+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'CPU source escaped this engine'}
    $taskHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant()
    if($taskHash -cne $taskRow.sha256){throw ('Pinned CPU source changed: '+$taskRow.path)}
    $taskSources+=@([ordered]@{path=$taskRow.path;sha256=$taskHash})
}
if($taskSources.Count -ne 14){throw 'Incomplete CPU source closure'}
$taskRun=Join-Path $taskExpected ('run-'+[Guid]::NewGuid().ToString('N'))
if(Test-Path -LiteralPath $taskRun){throw 'Preserve existing fixture run'}
[void](New-Item -ItemType Directory -Path $taskRun)
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskSdkVersion='10.0.26100.0'
$taskCategories=@([ordered]@{Name='unchanged-runtime-header-counterexample';Cases=3},[ordered]@{Name='source-derived-dirty-boundary-model';Cases=1},[ordered]@{Name='closed-fixture-protocol-model';Cases=8})
$taskState=[ordered]@{StartedUtc=[DateTime]::UtcNow.ToString('o');RunDirectory=$taskRun;PowerShell=$PSVersionTable.PSVersion.ToString();Sources=$taskSources;ExpectedCategories=$taskCategories;Architectures=@();Completed=$false;Error=$null;ProductionAvailable=$false;Installable=$false;SafeToResetProduction=$false;NativeRuntimeExecuted=$false;ManifestSHA256=(Get-FileHash -LiteralPath $taskManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()}
$taskEnvironment=@{}
foreach($taskName in @('INCLUDE','LIB','PATH','TEMP','TMP','K033_TEST_HARDWARE')){$taskEnvironment[$taskName]=[Environment]::GetEnvironmentVariable($taskName,'Process')}
function Save-033BarrierResult {
    $taskJson=$taskState|ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText((Join-Path $taskRun 'suite-results.json'),$taskJson,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $taskExpected 'barrier-suite-results.json'),$taskJson,[Text.UTF8Encoding]::new($false))
}
function Invoke-033BarrierNative([string]$Program,[string[]]$Arguments,[string]$Log){
    $taskSavedPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
    $PSNativeCommandUseErrorActionPreference=$false
    try{& $Program @Arguments 2>&1|Tee-Object -FilePath $Log;$script:taskNativeExit=$LASTEXITCODE}
    finally{$ErrorActionPreference=$taskSavedPreference}
    if($script:taskNativeExit){throw ('Reviewed CPU command failed with exit '+$script:taskNativeExit+': '+$Program)}
}
Save-033BarrierResult
try{
    $env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskSdkVersion\ucrt;$taskSdk\Include\$taskSdkVersion\um;$taskSdk\Include\$taskSdkVersion\shared"
    $env:K033_TEST_HARDWARE='0'
    foreach($taskArch in @('x64','x86')){
        $taskDirectory=Join-Path $taskRun $taskArch;[void](New-Item -ItemType Directory -Path $taskDirectory)
        $env:TEMP=$taskDirectory;$env:TMP=$taskDirectory
        $env:LIB="$taskMsvc\lib\$taskArch;$taskSdk\Lib\$taskSdkVersion\ucrt\$taskArch;$taskSdk\Lib\$taskSdkVersion\um\$taskArch"
        $taskCompiler=Join-Path $taskMsvc "bin/Hostx64/$taskArch/cl.exe"
        $env:PATH=(Split-Path -Parent $taskCompiler)+';'+$taskEnvironment.PATH
        $taskExe=Join-Path $taskDirectory 'shared_settings_barrier_cpu_test.exe'
        $taskObj=Join-Path $taskDirectory 'shared_settings_barrier_cpu_test.obj'
        Push-Location $taskDirectory
        try{
            $taskArgs=@('/nologo','/std:c++17','/O2','/MT','/EHsc','/utf-8','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR',(Join-Path $PSScriptRoot 'shared_settings_barrier_cpu_test.cpp'),"/Fo:$taskObj","/Fe:$taskExe",'/link','kernel32.lib')
            Invoke-033BarrierNative -Program $taskCompiler -Arguments $taskArgs -Log (Join-Path $taskDirectory 'compile.log')
            Invoke-033BarrierNative -Program $taskExe -Arguments @() -Log (Join-Path $taskDirectory 'test.log')
            $taskResult=Get-Content -LiteralPath (Join-Path $taskDirectory 'barrier-results.json') -Raw|ConvertFrom-Json
            if($taskResult.failures -ne 0 -or $taskResult.cases -ne 12 -or $taskResult.productionAvailable -ne $false -or $taskResult.installable -ne $false -or $taskResult.safeToResetProduction -ne $false -or $taskResult.nativeRuntimeExecuted -ne $false){throw 'Invalid CPU result or scope'}
            if(@($taskResult.categories).Count -ne 3 -or @($taskResult.results).Count -ne 12){throw 'Missing categorized CPU evidence'}
            $taskSum=0
            foreach($taskCategory in $taskCategories){
                $taskMatched=@($taskResult.categories|Where-Object {$_.name -ceq $taskCategory.Name})
                if($taskMatched.Count -ne 1 -or $taskMatched[0].cases -ne $taskCategory.Cases -or $taskMatched[0].failures -ne 0){throw 'Invalid CPU evidence category'}
                $taskRows=@($taskResult.results|Where-Object {$_.category -ceq $taskCategory.Name})
                if($taskRows.Count -ne $taskCategory.Cases -or @($taskRows|Where-Object {$_.passed -ne $true -or $_.failures -ne 0}).Count){throw 'Invalid categorized case result'}
                $taskSum+=$taskMatched[0].checks
            }
            if($taskSum -ne $taskResult.checks){throw 'Category check counts do not sum to total'}
            $taskState.Architectures+=@([ordered]@{Architecture=$taskArch;Checks=$taskResult.checks;Failures=$taskResult.failures;Cases=$taskResult.cases;Categories=$taskResult.categories;CompilerSHA256=(Get-FileHash -LiteralPath $taskCompiler -Algorithm SHA256).Hash.ToLowerInvariant();ExecutableSHA256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant();ResultSHA256=(Get-FileHash -LiteralPath (Join-Path $taskDirectory 'barrier-results.json') -Algorithm SHA256).Hash.ToLowerInvariant()})
        }finally{Pop-Location}
        Save-033BarrierResult
    }
    foreach($taskRow in $taskSources){if((Get-FileHash -LiteralPath (Join-Path $taskRepo $taskRow.path) -Algorithm SHA256).Hash -ine $taskRow.sha256){throw ('CPU source changed during execution: '+$taskRow.path)}}
    $taskState.Completed=$true
    Write-Output ('033 shared settings barrier CPU: both architectures completed; '+$taskRun)
}catch{$taskState.Error=$_.Exception.Message;throw}
finally{
    foreach($taskName in $taskEnvironment.Keys){[Environment]::SetEnvironmentVariable($taskName,$taskEnvironment[$taskName],'Process')}
    Save-033BarrierResult
}
