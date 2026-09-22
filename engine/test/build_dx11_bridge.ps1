param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$LegacyFeeder)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskOut=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('run-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(-not $taskOut.StartsWith($taskRoot+'\build\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output escaped worktree'}
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$taskCapture=Join-Path $taskRoot '..\tools\capture_engine_inputs.py'
# Existing reviewed CPU/build call graph; no new executable or product run.
& python $taskCapture before $taskOut
if($LASTEXITCODE){throw 'Core source capture failed'}
$taskExit=0
try {
 $taskBuildArgs=@{OutputDirectory=$taskOut};if($LegacyFeeder){$taskBuildArgs.LegacyFeeder=$true}
 & (Join-Path $PSScriptRoot 'build_single_engine.ps1') @taskBuildArgs 1> (Join-Path $taskOut 'core.stdout.txt') 2> (Join-Path $taskOut 'core.stderr.txt')
 $taskExit=$LASTEXITCODE
 if($taskExit){throw "Core compiler failed $taskExit"}
} catch {
 $taskExit=if($LASTEXITCODE){$LASTEXITCODE}else{-1}
 $_ | Out-String | Set-Content -LiteralPath (Join-Path $taskOut 'core.exception.txt') -Encoding UTF8
 throw
} finally {
 @{script='engine/test/build_single_engine.ps1';output=$taskOut;exit=$taskExit}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $taskOut 'core-command.json') -Encoding UTF8
 Get-Content -LiteralPath (Join-Path $taskOut 'core.stdout.txt') -Tail 22
 Get-Content -LiteralPath (Join-Path $taskOut 'core.stderr.txt') -ErrorAction SilentlyContinue -Tail 10
}
& python $taskCapture after $taskOut
if($LASTEXITCODE){throw 'Core sources changed while building'}
