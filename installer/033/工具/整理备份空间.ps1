param([switch]$Apply,[string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer'))
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'managed/managed_transaction.ps1')
# No game/driver/registry/process operations. Default invocation is read-only.
$result=Invoke-033VaultMaintenance -Vault $Vault -PlanOnly:(-not $Apply)
$result|ConvertTo-Json -Depth 8
if(-not $Apply){Write-Host '这是只读检查。加 -Apply 后会整理备份库：保留原件及历史记录，合并相同副本，回收已提交事务的多余安装副本。'}
