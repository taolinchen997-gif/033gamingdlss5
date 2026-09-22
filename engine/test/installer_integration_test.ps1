param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# Inert files only; install via the real dispatcher, never execute a PE fixture.
$ErrorActionPreference='Stop'
$deploy=Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy'
. (Join-Path $deploy 'deployment_transaction.ps1')
. (Join-Path $deploy 'entry_assets.ps1')
$work=Join-Path $OutputDirectory ('ni-'+[Guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $work|Out-Null
$results=[Collections.Generic.List[object]]::new()
function Check($Ok,[string]$Message){if(-not $Ok){throw $Message}}
function Case([string]$Name,[scriptblock]$Body){
    try{& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output "PASS $Name"}
    catch{$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message;Stack=$_.ScriptStackTrace});Write-Output "FAIL $Name : $($_.Exception.Message)"}
}
function Put([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($true))}
function Pe([string]$Path,[byte]$Tag){
    $b=[byte[]]::new(1024)
    foreach($pair in @(@(0,0x5a4d),@(132,0x8664),@(134,1),@(148,240),@(152,0x20b))){[BitConverter]::GetBytes([uint16]$pair[1]).CopyTo($b,$pair[0])}
    foreach($pair in @(@(60,128),@(128,0x4550),@(212,512),@(260,16),@(400,512),@(404,4096),@(408,512),@(412,512))){[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($b,$pair[0])}
    $b[1000]=$Tag;[IO.File]::WriteAllBytes($Path,$b)
}
function Snapshot([string]$Directory){
    $state=@{}
    Get-ChildItem -LiteralPath $Directory -File -Force|ForEach-Object {$state[$_.Name]=@{Hash=(Get-033FileHash $_.FullName);Time=$_.LastWriteTimeUtc.Ticks;Attributes=[int]$_.Attributes}}
    return $state
}
function Exact([string]$Directory,$Before,[string[]]$Names=@()){
    if(-not $Names.Count){$Names=@($Before.Keys)}
    foreach($name in $Names){$item=Get-Item -LiteralPath (Join-Path $Directory $name) -Force
        Check ((Get-033FileHash $item.FullName) -eq $Before[$name].Hash -and $item.LastWriteTimeUtc.Ticks -eq $Before[$name].Time -and [int]$item.Attributes -eq $Before[$name].Attributes) ('Bytes/metadata changed: '+$name)
    }
}
function Fixture([bool]$Scene=$true){
    $base=Join-Path $work ([Guid]::NewGuid().ToString('N').Substring(0,6));$game=Join-Path $base '游戏';$package=Join-Path $base 'pkg';$toolDir=Join-Path $package 'tools'
    New-Item -ItemType Directory -Path $game,$toolDir|Out-Null
    Get-ChildItem -LiteralPath $deploy -File|Copy-Item -Destination $toolDir
    $exe=Join-Path $game 'arbitrary-cpu-fixture.exe'
    Pe $exe 1;Pe (Join-Path $game 'version.dll') 2;Pe (Join-Path $game 'dinput8.dll') 3
    Pe (Join-Path $package 'core.dll') 4;Pe (Join-Path $package 'scene.dll') 5;Put (Join-Path $package 'notice.txt') 'inert license fixture'
    $core=Get-033FileHash (Join-Path $game 'version.dll');$adapter=Get-033FileHash (Join-Path $game 'dinput8.dll')
    # Selection comes from this hashed inert catalogue, not a game-name branch.
    $rules=@();if($Scene){$rules=@(@{GameExe=[IO.Path]::GetFileName($exe);Id='re4-tdb71';AdapterTarget='dinput8.dll';AdapterAbi=1;SceneSchema=71;AllowedCoreBefore=@($core);AllowedAdapterBefore=@($adapter)})}
    Write-033Json (Join-Path $package 'catalogue.json') @{Schema=1;DefaultRoute='existing-interface-auto';Adapters=$rules}
    $manifest=@{Schema=1;Kind='033-native-input-upgrade';PackageId='inert-integration';AllowedNoticeBefore=@();Files=@()}
    foreach($pair in @(@('Core','core.dll'),@('SceneAdapter','scene.dll'),@('Notice','notice.txt'),@('Catalogue','catalogue.json'))){$entry=@{Path=$pair[1];SHA256=(Get-033FileHash (Join-Path $package $pair[1]))};$manifest[$pair[0]]=$entry;$manifest.Files+=@($entry)}
    Write-033Json (Join-Path $package 'native-input-package.json') $manifest
    Write-033Json (Join-Path $game '_033-integrated.json') @{Version=1;PackageId='before';CoreMount='version.dll';CoreHash=$core;GameExe=$exe;ComponentHashes=@(@{Path='version.dll';After=$core},@{Path='dinput8.dll';After=$adapter})}
    $settings=@{'dlss5-033.cfg'="inject=1`r`npasses=3`r`nimagefgmult=3`r`nhotkey=122`r`n";'dlss5-033.state'='7';'ReShade.ini'="[RenoDX.MFGUnlock]`r`nForceMultiplier=6`r`n";'OptiScaler.ini'='untouched user choice';'local_config.ini'='unchanged'}
    foreach($name in $settings.Keys){Put (Join-Path $game $name) $settings[$name];[IO.File]::SetAttributes((Join-Path $game $name),[IO.FileAttributes]::ReadOnly)}
    foreach($name in @('dlss5_check.ps1','检查有没有生效.cmd','按Home没反应就点我.cmd')){Put (Join-Path $game $name) 'inert old entry';[IO.File]::SetAttributes((Join-Path $game $name),[IO.FileAttributes]::ReadOnly)}
    @{Game=$game;Exe=$exe;Package=$package;Tools=$toolDir;Installer=(Join-Path $toolDir 'integrated_install.ps1');Before=(Snapshot $game);Settings=@($settings.Keys)}
}
function Install($Fixture){& $Fixture.Installer -Action Install -GameExe $Fixture.Exe -PackageRoot $Fixture.Package|ConvertFrom-Json}
function Restore($Fixture,[string]$Receipt){& $Fixture.Installer -Action Restore -Receipt $Receipt|Out-Null}
function VerifyEntries($Fixture){Check (@(Get-033EntryAssetPlan $Fixture.Game $Fixture.Tools).Count -eq 0) 'An entry/dependency was not installed';Exact $Fixture.Game $Fixture.Before $Fixture.Settings}
Case 'native dispatcher installs payload and all aliases in one exact transaction' {
    $f=Fixture;$plan=& $f.Installer -Action Plan -GameExe $f.Exe -PackageRoot $f.Package|ConvertFrom-Json
    Exact $f.Game $f.Before
    Check ($plan.Targets.Count -eq 14 -and $plan.EntryAssets -eq 'planned') 'Native plan omitted entry targets'
    $result=Install $f;VerifyEntries $f
    $receipt=Get-Content -LiteralPath $result.Receipt -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($receipt.Files.Count -eq 14 -and $receipt.Status -eq 'installed' -and $result.Status -eq 'installed-awaiting-manual-validation' -and -not $result.GameAccepted) 'Payload/entries split across transactions or falsely accepted'
    Check (@(Get-ChildItem -LiteralPath (Join-Path $f.Game '_033transactions') -Directory).Count -eq 1) 'More than one installation transaction'
    Restore $f $result.Receipt;Exact $f.Game $f.Before
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game 'integrated_supervisor.ps1'))) 'New dependency not removed on rollback'
}
Case 'already-current native payload repairs entries only and then repeats without writes' {
    $f=Fixture;$first=Install $f
    $nativeNames=@('version.dll','dinput8.dll','_033-integrated.json','033-native-input-NOTICE.txt')
    $before=Snapshot $f.Game
    Put (Join-Path $f.Game 'dlss5_check.ps1') 'inert stale alias'
    $entryBefore=Snapshot $f.Game
    $repair=Install $f;VerifyEntries $f;Exact $f.Game $before $nativeNames
    $receipt=Get-Content -LiteralPath $repair.Receipt -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($repair.Status -eq 'already-installed' -and $repair.EntryAssets -eq 'installed' -and $receipt.Files.Count -eq 1 -and $receipt.Files[0].Path -eq 'dlss5_check.ps1') 'Current native payload was rewritten or entry repair skipped'
    $stable=Snapshot $f.Game;$repeat=Install $f;Exact $f.Game $stable
    Check ($repeat.EntryAssets -eq 'current' -and -not $repeat.PSObject.Properties['Receipt']) 'Repeat created another transaction'
    Restore $f $repair.Receipt;Exact $f.Game $entryBefore
}
Case 'unmatched catalogue updates only route-independent entries and restores exactly' {
    $f=Fixture $false;$result=Install $f;VerifyEntries $f
    Exact $f.Game $f.Before @('version.dll','dinput8.dll','_033-integrated.json')
    Check ($result.Status -eq 'preserve-existing-interface' -and $result.Targets.Count -eq 10 -and -not $result.Adapter -and -not $result.GameAccepted) 'Unmatched executable selected a scene adapter'
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game '033-native-input-NOTICE.txt'))) 'Unmatched install touched native payload'
    $repeat=Install $f;Check ($repeat.EntryAssets -eq 'current' -and $repeat.Targets.Count -eq 0 -and -not $repeat.PSObject.Properties['Receipt']) 'Unmatched repeated entry install wrote again'
    Restore $f $result.Receipt;Exact $f.Game $f.Before
}
Case 'entry write failure rolls back prior native payload and legacy entry metadata together' {
    $f=Fixture;$module=Join-Path $f.Tools 'deployment_transaction.ps1'
    $text=[IO.File]::ReadAllText($module)
    $needle='function Invoke-033TransactionStep([string]$Stage,[string]$Receipt,[string]$Path) {}'
    Check ($text.Contains($needle)) 'Reviewed injection hook missing'
    $replacement=@'
function Invoke-033TransactionStep([string]$Stage,[string]$Receipt,[string]$Path) {
    if($Stage -eq 'after-write' -and $Path -eq 'dlss5_check.ps1'){throw 'INERT FIXTURE ENTRY FAILURE'}
}
'@
    Put $module ($text.Replace($needle,$replacement))
    $blocked=$false;try{Install $f|Out-Null}catch{$blocked=$_.Exception.Message.Contains('INERT FIXTURE ENTRY FAILURE')}
    Check $blocked 'Injected entry failure did not reach the shared transaction'
    Exact $f.Game $f.Before
    $receipts=@(Get-ChildItem -LiteralPath (Join-Path $f.Game '_033transactions') -Filter receipt.json -Recurse)
    Check ($receipts.Count -eq 1) 'Failure used separate payload/entry transactions'
    $receipt=Get-Content -LiteralPath $receipts[0].FullName -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($receipt.Status -eq 'restored' -and $receipt.Files.Count -eq 14) 'Failed transaction not exactly restored'
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game 'deployment_transaction.ps1'))) 'Introduced entry survived failed transaction'
}
Case 'external entry edit blocks exact rollback without changing the native payload' {
    $f=Fixture;$result=Install $f;Put (Join-Path $f.Game 'dlss5_check.ps1') 'external user edit'
    $before=Snapshot $f.Game;$blocked=$false
    try{Restore $f $result.Receipt}catch{$blocked=$_.Exception.Message.Contains('External modification blocks restore')}
    Check $blocked 'Conflicting entry was overwritten';Exact $f.Game $before
}
Case 'missing entry source fails preflight before native files change' {
    $f=Fixture;Remove-Item -LiteralPath (Join-Path $f.Tools 'diagnose.cmd')
    $blocked=$false;try{Install $f|Out-Null}catch{$blocked=$_.Exception.Message.Contains('Entry asset missing')}
    Check $blocked 'Missing entry asset accepted';Exact $f.Game $f.Before
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game '_033transactions'))) 'Missing source reached transaction writes'
}
$summary=@{PowerShell=$PSVersionTable.PSVersion.ToString();Passed=@($results|Where-Object {$_.Passed}).Count;Failed=@($results|Where-Object {-not $_.Passed}).Count;Fixtures=$work;Results=$results.ToArray();GameOrGPUExecution=$false}
Write-033Json (Join-Path $OutputDirectory 'integration-results.json') $summary
Write-Output "Installer integration: $($summary.Passed) passed, $($summary.Failed) failed"
if($summary.Failed){throw 'Installer integration regressions; see integration-results.json'}
