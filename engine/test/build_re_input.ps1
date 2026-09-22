param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $taskOutput.StartsWith($taskRoot+'\build\',[StringComparison]::OrdinalIgnoreCase)){throw 'Build output must stay in engine/build'}
$taskSource=Join-Path $taskRoot 'ref\REFramework033'
$taskCmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if(-not (Test-Path -LiteralPath $taskCmake)){throw 'Reviewed Visual Studio CMake not found'}
# Configure/build only. No test, game process, graphics device or DLL loading.
$taskLocalDeps=@()
foreach($taskDep in @('asmjit','openxr','directxtk','directxtk12','safetyhook','freetype','bddisasm','kananlib')){
    $taskDepPath=Join-Path $taskSource ('dependencies\fetched\'+$taskDep+'-src')
    if(Test-Path -LiteralPath $taskDepPath){$taskLocalDeps+=('-DFETCHCONTENT_SOURCE_DIR_'+$taskDep.ToUpper()+'='+$taskDepPath)}
}
& $taskCmake -S $taskSource -B $taskOutput -G 'Visual Studio 17 2022' -A x64 -DCMKR_SKIP_GENERATION=ON -DCMAKE_BUILD_TYPE=Release -DDEVELOPER_MODE=OFF @taskLocalDeps
if($LASTEXITCODE){throw 'RE input configure failed'}
& $taskCmake --build $taskOutput --config Release --target REFramework -- /m:2 /v:minimal /fl "/flp:logfile=$taskOutput\build.log;verbosity=normal;encoding=UTF-8"
if($LASTEXITCODE){throw 'RE input build failed'}
