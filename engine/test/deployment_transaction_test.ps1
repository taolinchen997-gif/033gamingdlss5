$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'deployment_transaction.ps1')
$taskRoot=Join-Path (Split-Path -Parent $PSScriptRoot) ('build\transaction-tests\'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskRoot -Force | Out-Null
$taskPayload=Join-Path $taskRoot 'payload.bin';[IO.File]::WriteAllBytes($taskPayload,[byte[]](1,2,3,4,5))
$taskChecks=0
function Check($Ok,[string]$Name){$script:taskChecks++;if(-not $Ok){throw "FAIL: $Name"}}
$taskGame=Join-Path $taskRoot 'game';New-Item -ItemType Directory -Path $taskGame | Out-Null
[IO.File]::WriteAllText((Join-Path $taskGame 'dxgi.dll'),'original-proxy')
[IO.File]::WriteAllText((Join-Path $taskGame '_安装记录.txt'),"proxy=dxgi.dll`r`nEnableHooks=`r`n")
$taskBefore=(Get-FileHash -LiteralPath (Join-Path $taskGame '_安装记录.txt')).Hash
$taskConfig=Join-Path $taskRoot 'new-record.txt';[IO.File]::WriteAllText($taskConfig,"proxy=d3d11.dll`nEnableHooks=1`n")
$taskPlan=@(@{Path='d3d11.dll';Source=(Join-Path $taskGame 'dxgi.dll')},@{Path='dxgi.dll';Source=$null},@{Path='_安装记录.txt';Source=$taskConfig})
$taskReceipt=Invoke-033Transaction $taskGame $taskPlan 'mount change'
Check ((Test-Path -LiteralPath (Join-Path $taskGame 'd3d11.dll')) -and -not(Test-Path -LiteralPath (Join-Path $taskGame 'dxgi.dll'))) 'mount change applied'
Restore-033Transaction $taskReceipt | Out-Null
Check (([IO.File]::ReadAllText((Join-Path $taskGame 'dxgi.dll'))) -eq 'original-proxy') 'undo restores exact original mount'
Check (-not(Test-Path -LiteralPath (Join-Path $taskGame 'd3d11.dll')) -and -not(Test-Path -LiteralPath (Join-Path $taskGame 'dinput8.dll'))) 'undo never cycles to next candidate'
Check ((Get-FileHash -LiteralPath (Join-Path $taskGame '_安装记录.txt')).Hash -eq $taskBefore) 'empty hook and CRLF restored byte for byte'
Restore-033Transaction $taskReceipt | Out-Null;Check $true 'idempotent undo'
$taskReceipt=Invoke-033Transaction $taskGame @(@{Path='new.dll';Source=$taskPayload})
[IO.File]::WriteAllText((Join-Path $taskGame 'new.dll'),'user edit')
$blocked=$false;try{Restore-033Transaction $taskReceipt | Out-Null}catch{$blocked=$true}
Check $blocked 'later edit blocks undo before mutation'
Check (([IO.File]::ReadAllText((Join-Path $taskGame 'new.dll'))) -eq 'user edit') 'later edit preserved'
foreach($bad in @('..\outside.dll','E:\outside.dll','sub\..\outside.dll')){$blocked=$false;try{Get-033Path $taskGame $bad | Out-Null}catch{$blocked=$true};Check $blocked 'path traversal rejected'}
$blocked=$false;try{Invoke-033Transaction $taskGame @(@{Path='a.dll';Source=$taskPayload},@{Path='A.dll';Source=$taskPayload}) | Out-Null}catch{$blocked=$true}
Check $blocked 'case-insensitive duplicate rejected'
Write-Output "Exact file transaction: $taskChecks checks, 0 failures"
