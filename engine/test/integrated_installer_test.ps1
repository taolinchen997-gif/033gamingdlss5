param(
    [string]$PackageRoot=(Join-Path (Split-Path -Parent $PSScriptRoot) '分发包\033整合候选_20260906_自动验证版'),
    [string]$SmokeExe=(Join-Path (Split-Path -Parent $PSScriptRoot) 'build\033-integrated-candidate_20260906\smoke-dlss-real-nr-proxy\core_backend_smoke_test.exe'),
    [string]$RenoDxBaseline=(Join-Path (Split-Path -Parent $PSScriptRoot) '分发包\DLSS5一键包 v5.0\工具\运行时\renodx-dlss5.addon64')
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
. (Join-Path $PackageRoot 'tools\deployment_transaction.ps1')
. (Join-Path $PackageRoot 'tools\package_tools.ps1')
$checks=0
function Check($Ok,[string]$Name){$script:checks++;if(-not $Ok){throw "FAIL: $Name"}}
$root=Join-Path $repo ('build\integrated-installer-tests\'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$exe=Join-Path $root 'fixture.exe'
Copy-Item -LiteralPath $SmokeExe -Destination $exe
$original=[ordered]@{
    'dlss5-033.addon64'='previous add-on';'ReShade.ini'="[RenoDX.MFGUnlock]`r`nForceMultiplier=6`r`n[RenoDX.DLSS5]`r`nEnableHooks=`r`n[User]`r`nSpecial=Keep`r`n";
    'dlss5-033.cfg'="work=50`r`npasses=4`r`ninject=1`r`nuserkey=unchanged`r`npreset=3`r`nmas=1`r`npreexposure=7`r`n";'dlss5-033.state'='1';
    'dxgi.dll'='original reshade mount';'_安装记录.txt'="proxy=dxgi.dll`r`nproxyall=dxgi.dll`r`nproxycands=dxgi.dll,d3d11.dll,dinput8.dll`r`nEnableHooks=`r`n";
    'dlss5_supervisor.ps1'='original supervisor';'dlss5_remount.ps1'='original remount'
}
foreach($name in $original.Keys){[IO.File]::WriteAllText((Join-Path $root $name),$original[$name],[Text.UTF8Encoding]::new($false))}
Copy-Item -LiteralPath $RenoDxBaseline -Destination (Join-Path $root 'renodx-dlss5.addon64')
$before=@{};Get-ChildItem -LiteralPath $root -File | ForEach-Object {$before[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
$installer=Join-Path $PackageRoot 'tools\integrated_install.ps1'
$plan=& $installer -Action Plan -GameExe $exe | ConvertFrom-Json
Check ($plan.CoreMount -eq 'winmm.dll' -and -not(Test-Path -LiteralPath (Join-Path $root 'winmm.dll'))) 'plan selects a real imported vacant name without installation'
$installed=& $installer -Action Install -GameExe $exe
Check ($installed.Status -eq 'installed') 'complete installation'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'renodx-dlss5.addon64'))) 'known RenoDX removed from active addon set'
Check ((Get-FileHash -LiteralPath (Join-Path $root 'dxgi.dll')).Hash -eq $before['dxgi.dll']) 'existing ReShade mount preserved'
$cfg=[IO.File]::ReadAllText((Join-Path $root 'dlss5-033.cfg'))
Check ($cfg -match '(?m)^work=100\r?$' -and $cfg -match 'userkey=unchanged' -and $cfg -match '(?m)^preset=3\r?$') '100% model and unknown settings preserved'
Check ($cfg -match '(?m)^mas=1\r?$' -and $cfg -match '(?m)^preexposure=7\r?$') 'user sharpening and pre-grade survive installation'
$taskManifest=Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($taskManifest.PSObject.Properties['EngineLayout'] -and $taskManifest.EngineLayout -eq 'single-033'){
    Check ($cfg -match '(?m)^passes=1\r?$') 'single-engine candidate starts with one independent model pass'
    $taskInstalledRecord=Get-Content -LiteralPath (Join-Path $root '_033-integrated.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($taskInstalledRecord.EngineLayout -eq 'single-033') 'single-engine installation recorded explicitly'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'winmm.dll')).Hash -eq (Get-FileHash -LiteralPath (Join-Path $PackageRoot 'payload/033-engine.dll')).Hash) 'mounted binary is the combined engine'
}
$rs=[IO.File]::ReadAllText((Join-Path $root 'ReShade.ini'))
Check ($rs -match 'ForceMultiplier=6' -and $rs -match 'Special=Keep') '6x and unrelated ReShade settings preserved'
$opti=[IO.File]::ReadAllText((Join-Path $root 'OptiScaler.ini'))
Check ($opti -match 'FGInput=nofg' -and $opti -match 'UseFakenvapi=false') 'core does not replace the native FG/Reflex baseline'
$report1=& (Join-Path $PackageRoot 'tools\integrated_supervisor.ps1') -GameExe $exe | ConvertFrom-Json
$report2=& (Join-Path $PackageRoot 'tools\integrated_supervisor.ps1') -GameExe $exe | ConvertFrom-Json
$report3=& (Join-Path $PackageRoot 'tools\integrated_supervisor.ps1') -GameExe $exe | ConvertFrom-Json
Check ($report1.Verdict -eq 'unverified' -and $report1.Actions.Count -eq 0 -and ($report1|ConvertTo-Json -Depth 10) -eq ($report2|ConvertTo-Json -Depth 10) -and ($report2|ConvertTo-Json -Depth 10) -eq ($report3|ConvertTo-Json -Depth 10)) 'three identical diagnostic runs never invent verdicts or actions'
$preserveModelUpgrade=$taskManifest.PSObject.Properties['EngineLayout'] -and $taskManifest.EngineLayout -eq 'single-033'
if($preserveModelUpgrade){
    $edited=Set-033FlatValue $cfg 'passes' '3';$edited=Set-033FlatValue $edited 'work' '80';$edited=Set-033FlatValue $edited 'passwork' '85'
    [IO.File]::WriteAllText((Join-Path $root 'dlss5-033.cfg'),$edited,[Text.UTF8Encoding]::new($false))
    $taskRsPath=Join-Path $root 'ReShade.ini'
    [IO.File]::AppendAllText($taskRsPath,"`r`n`r`n",[Text.UTF8Encoding]::new($false))
    $taskRsUpgradeHash=(Get-FileHash -LiteralPath $taskRsPath).Hash
}
$blocked=$false;try{& $installer -Action Plan -GameExe $exe -PortraitNatural | Out-Null}catch{$blocked=$true}
Check $blocked 'removed portrait preset rejected'
$reinstalled=& $installer -Action Install -GameExe $exe
$portrait=[IO.File]::ReadAllText((Join-Path $root 'dlss5-033.cfg'))
$expectedWork=if($preserveModelUpgrade){80}else{100}
Check ($portrait -notmatch '(?m)^skin=125\r?$' -and $portrait -notmatch '(?m)^faceboost=50\r?$' -and $portrait -match "(?m)^work=$expectedWork\r?$" -and $portrait -match '(?m)^mas=1\r?$') 'upgrade preserves resolution/sharpening without restoring removed skin preset'
if($preserveModelUpgrade){
    Check ($portrait -match '(?m)^passes=3\r?$' -and $portrait -match '(?m)^passwork=85\r?$') 'managed upgrade preserves layer settings despite package defaults'
}
if($preserveModelUpgrade){Check ((Get-FileHash -LiteralPath $taskRsPath).Hash -eq $taskRsUpgradeHash) 'managed upgrade preserves exact ReShade bytes including trailing blank lines'}
Check ($reinstalled.CoreMount -eq 'winmm.dll') 'repeat installation retains the same mount'
& $installer -Action Restore -Receipt $reinstalled.Receipt | Out-Null
if($preserveModelUpgrade){
    Check ([IO.File]::ReadAllText((Join-Path $root 'dlss5-033.cfg')) -eq $edited) 'upgrade rollback restores the user-edited layer settings exactly'
    # The earlier receipt correctly rejects intervening edits. Return this test
    # fixture to that receipt's installed state before testing its rollback.
    [IO.File]::WriteAllText((Join-Path $root 'dlss5-033.cfg'),$cfg,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($taskRsPath,$rs,[Text.UTF8Encoding]::new($false))
}
& $installer -Action Restore -Receipt $installed.Receipt | Out-Null
$exact=$true;foreach($name in $before.Keys){$exact=$exact -and ((Get-FileHash -LiteralPath (Join-Path $root $name)).Hash -eq $before[$name])}
Check $exact 'install and repeated install rollback restore every original byte including empty EnableHooks'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'winmm.dll')) -and -not(Test-Path -LiteralPath (Join-Path $root 'OptiScaler.ini'))) 'rollback restores original absence'
$remount=Join-Path $PackageRoot 'tools\integrated_remount.ps1'
$receipt=& $remount -GameRoot $root -Auto
Check ((Test-Path -LiteralPath (Join-Path $root 'd3d11.dll')) -and -not(Test-Path -LiteralPath (Join-Path $root 'dxgi.dll'))) 'auto chooses next mount as a new operation'
& $remount -GameRoot $root -Undo -Receipt $receipt | Out-Null
Check ((Get-FileHash -LiteralPath (Join-Path $root 'dxgi.dll')).Hash -eq $before['dxgi.dll'] -and -not(Test-Path -LiteralPath (Join-Path $root 'd3d11.dll')) -and -not(Test-Path -LiteralPath (Join-Path $root 'dinput8.dll'))) 'undo restores exact prior mount instead of cycling'
Check ((Get-FileHash -LiteralPath (Join-Path $root '_安装记录.txt')).Hash -eq $before['_安装记录.txt']) 'remount record roundtrip is byte exact'
[IO.File]::WriteAllText((Join-Path $root 'winmm.dll'),'occupied by another component')
$blocked=$false;try{& $installer -Action Install -GameExe $exe | Out-Null}catch{$blocked=$true}
Check $blocked 'occupied imported mount rejected'
Check ([IO.File]::ReadAllText((Join-Path $root 'winmm.dll')) -eq 'occupied by another component') 'unrelated proxy remains untouched'
$badPackage=Join-Path $root 'bad-package';New-Item -ItemType Directory -Path $badPackage | Out-Null
Write-033Json (Join-Path $badPackage 'manifest.json') @{Version=1;Files=@(@{Path='missing.dll';SHA256='00'});Payload=@()}
$blocked=$false;try{Test-033Package $badPackage | Out-Null}catch{$blocked=$true}
Check $blocked 'missing or damaged payload rejected before installation'
$blocked=$false;try{Assert-033Closed (Split-Path -Parent (Get-Process -Id $PID).Path)}catch{$blocked=$true}
Check $blocked 'running target-directory process blocks file mutation'
"Integrated installer / exact remount / read-only supervisor: $checks checks, 0 failures"
