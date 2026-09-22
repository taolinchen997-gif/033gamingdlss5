param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$DeployRoot=(Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy')
)
# CPU/filesystem fixtures only. No DLL/EXE is loaded, no game or driver tool runs.
$ErrorActionPreference='Stop'
foreach($source in (Get-ChildItem -LiteralPath $DeployRoot -Filter '*.ps1')){
    $tokens=$null;$parseErrors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($source.FullName,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count){throw "Installer script does not parse in this PowerShell version: $($source.Name): $parseErrors"}
}
$results=[Collections.Generic.List[object]]::new()
$fixtureRoot=Join-Path $OutputDirectory ('fixtures-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
function Verify($Ok,[string]$Message){if(-not $Ok){throw $Message}}
function Case([string]$Name,[scriptblock]$Body){
    try {& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output "PASS $Name"}
    catch {$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message});Write-Output "FAIL $Name : $($_.Exception.Message)"}
}
function Put([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($true))}
function Hash([string]$Path){if(Test-Path -LiteralPath $Path -PathType Leaf){return (Get-FileHash -LiteralPath $Path).Hash};return $null}
function Fixture([string]$Inject='1'){
    $root=Join-Path $fixtureRoot ([Guid]::NewGuid().ToString('N'))
    $game=Join-Path $root 'game';$pkg=Join-Path $root 'package';$toolsDir=Join-Path $pkg 'tools'
    New-Item -ItemType Directory -Path $game,$toolsDir,(Join-Path $pkg 'payload') -Force | Out-Null
    Get-ChildItem -LiteralPath $DeployRoot -File | Copy-Item -Destination $toolsDir
    # Minimal AMD64 PE import table. Deliberately non-runnable.
    $bytes=[byte[]]::new(1024)
    function U16($At,$Value){[BitConverter]::GetBytes([uint16]$Value).CopyTo($bytes,$At)}
    function U32($At,$Value){[BitConverter]::GetBytes([uint32]$Value).CopyTo($bytes,$At)}
    U16 0 0x5a4d;U32 60 128;U32 128 0x4550;U16 132 0x8664;U16 134 1;U16 148 240
    U16 152 0x20b;U32 260 16;U32 272 4096;U32 276 40;U32 400 512;U32 404 4096;U32 408 512;U32 412 512
    U32 524 4160;[Text.Encoding]::ASCII.GetBytes("winmm.dll`0").CopyTo($bytes,576)
    $exe=Join-Path $game 'fixture.exe';[IO.File]::WriteAllBytes($exe,$bytes)
    Put (Join-Path $game 'winmm.dll') 'previous core'
    Put (Join-Path $game 'dlss5-033.addon64') 'previous adapter'
    Put (Join-Path $game 'dlss5-033.cfg') "# user comment`r`nengine=0`r`ninject=$Inject`r`nimagefg=0`r`nwork=80`r`nmodelfull=0`r`npasses=3`r`npasswork=85`r`ncarrier=0`r`npregrade=0`r`nprehighlights=7`r`nuserkey=keep`r`n`r`n"
    Put (Join-Path $game 'dlss5-033.state') '7'
    Put (Join-Path $game 'ReShade.ini') "[RenoDX.MFGUnlock]`r`nForceMultiplier=5`r`n[User]`r`nKeep=1`r`n`r`n"
    Put (Join-Path $game 'OptiScaler.ini') "[FrameGen]`r`nEnabled=true`r`nFGInput=dlssg`r`nFGOutput=dlssg`r`n[User]`r`nKeep=1`r`n`r`n"
    Put (Join-Path $pkg 'payload/033-engine.dll') 'candidate core'
    Put (Join-Path $pkg 'payload/adapter.dll') 'candidate adapter'
    $files=@(@{Path='payload/033-engine.dll';SHA256=(Hash (Join-Path $pkg 'payload/033-engine.dll'))},@{Path='payload/adapter.dll';SHA256=(Hash (Join-Path $pkg 'payload/adapter.dll'))})
    $payload=@(@{Path=$files[0].Path;SHA256=$files[0].SHA256;Target='@CORE@'},@{Path=$files[1].Path;SHA256=$files[1].SHA256;Target='dlss5-033.addon64'})
    Put (Join-Path $pkg 'manifest.json') (@{Version=1;PackageId='offline-fixture';EngineLayout='single-033';PreserveModelSettings=$true;ExtraPassPercent=50;UniversalFrameGeneration=@{Prepared=$true};Files=$files;Payload=$payload}|ConvertTo-Json -Depth 8)
    Put (Join-Path $game '_033-integrated.json') (@{Version=1;EngineLayout='single-033';CoreMount='winmm.dll';CoreHash=(Hash (Join-Path $game 'winmm.dll'));GameExe=$exe;PackageId='previous-fixture'}|ConvertTo-Json)
    $before=@{};Get-ChildItem -LiteralPath $game -File|ForEach-Object{$before[$_.Name]=(Hash $_.FullName)}
    return @{Game=$game;Exe=$exe;Package=$pkg;Installer=(Join-Path $toolsDir 'integrated_install.ps1');Supervisor=(Join-Path $toolsDir 'integrated_supervisor.ps1');Before=$before}
}
function SameSettings($F,[string[]]$Names=@('dlss5-033.cfg','dlss5-033.state','ReShade.ini','OptiScaler.ini')){
    foreach($name in $Names){Verify ((Hash (Join-Path $F.Game $name)) -eq $F.Before[$name]) "Settings bytes changed: $name"}
}
Case 'managed upgrade preserves all settings and counter; exact rollback' {
    $f=Fixture
    $plan=& $f.Installer -Action Plan -GameExe $f.Exe -PackageRoot $f.Package|ConvertFrom-Json
    SameSettings $f
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    SameSettings $f
    Verify ($plan.Multiplier -eq 'preserve') 'Plan promises a multiplier change'
    & $f.Installer -Action Restore -Receipt $installed.Receipt|Out-Null
    foreach($name in $f.Before.Keys){Verify ((Hash (Join-Path $f.Game $name)) -eq $f.Before[$name]) "Rollback differs: $name"}
}
Case 'existing zero-guide route can upgrade without enabling native injection' {
    $f=Fixture '0'
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    SameSettings $f
    Verify ($installed.Status -eq 'installed') 'Upgrade refused existing presentation route'
}
Case 'managed record must belong to selected executable' {
    $f=Fixture;$recordPath=Join-Path $f.Game '_033-integrated.json'
    $record=Get-Content -LiteralPath $recordPath -Raw|ConvertFrom-Json;$record.GameExe=Join-Path $f.Game 'different.exe'
    Put $recordPath ($record|ConvertTo-Json)
    $blocked=$false;try {& $f.Installer -Action Plan -GameExe $f.Exe -PackageRoot $f.Package|Out-Null}catch{$blocked=$true}
    Verify $blocked 'Foreign executable record accepted'
    SameSettings $f
}
Case 'explicit highlights change is narrow and preserves counter' {
    $f=Fixture
    & $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package -PreHighlightsPercent 9|Out-Null
    SameSettings $f @('dlss5-033.state','ReShade.ini','OptiScaler.ini')
    $cfg=[IO.File]::ReadAllText((Join-Path $f.Game 'dlss5-033.cfg'))
    foreach($line in @('engine=0','carrier=0','work=80','passwork=85','pregrade=1','prehighlights=9','userkey=keep')){Verify ($cfg -match ('(?m)^'+[regex]::Escape($line)+'\r?$')) "Unexpected config: $line"}
}
Case 'universal preparation preserves disabled engine and unrelated settings' {
    $f=Fixture '0'
    & $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package -ImageFrameGen|Out-Null
    SameSettings $f @('dlss5-033.state','ReShade.ini')
    $cfg=[IO.File]::ReadAllText((Join-Path $f.Game 'dlss5-033.cfg'))
    foreach($line in @('engine=0','inject=0','imagefg=1','work=80','passwork=85','pregrade=0')){Verify ($cfg -match ('(?m)^'+[regex]::Escape($line)+'\r?$')) "Unexpected route config: $line"}
    $opti=[IO.File]::ReadAllText((Join-Path $f.Game 'OptiScaler.ini'))
    Verify ($opti -match 'Enabled=false' -and $opti -match 'FGInput=nofg' -and $opti -match 'Keep=1') 'Universal preparation does not preserve unrelated INI data'
}
Case 'removed portrait preset is rejected without writes' {
    $f=Fixture;$blocked=$false
    try {& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package -PortraitNatural|Out-Null}catch{$blocked=$true}
    Verify $blocked 'Removed skin preset was reintroduced'
    foreach($name in $f.Before.Keys){Verify ((Hash (Join-Path $f.Game $name)) -eq $f.Before[$name]) "Blocked install wrote $name"}
}
Case 'diagnose distinguishes configured route from verified runtime' {
    $f=Fixture '0';$before=@{}+ $f.Before
    $report=& $f.Supervisor -GameExe $f.Exe|ConvertFrom-Json
    Verify ($report.Verdict -eq 'unverified' -and $report.Actions.Count -eq 0) 'Diagnosis claims runtime success'
    Verify ($report.Installation.Status -eq 'core-hash-matched') 'Mounted core not verified'
    Verify ($report.Route.ConfiguredInput -eq 'presentation-or-feeder' -and $report.Route.NativeDLSS -eq 'unverified' -and $report.Route.NativeFrameGeneration -eq 'unverified') 'Configuration confused with native capability'
    Verify ($report.Route.AbnormalExitCounter -eq '7') 'Crash counter not reported'
    foreach($name in $before.Keys){Verify ((Hash (Join-Path $f.Game $name)) -eq $before[$name]) "Diagnosis changed $name"}
    Put (Join-Path $f.Game 'winmm.dll') 'unrelated replacement'
    $report=& $f.Supervisor -GameExe $f.Exe|ConvertFrom-Json
    Verify ($report.Installation.Status -eq 'core-hash-mismatch') 'Replaced core reported healthy'
}
Case 'diagnose works without a package and explains absent old-game route' {
    $f=Fixture
    # An unconfigured fixture with no prior 033 record is not a supported route.
    # Delete only these exact files in the fixture created by this script.
    foreach($name in @('_033-integrated.json','dlss5-033.cfg')){Remove-Item -LiteralPath (Join-Path $f.Game $name)}
    $report=& $f.Installer -Action Diagnose -GameExe $f.Exe -PackageRoot (Join-Path $f.Game 'no-package')|ConvertFrom-Json
    Verify ($report.Installation.Status -eq 'unmanaged' -and $report.Route.ConfiguredInput -eq 'unconfigured') 'Unconfigured old-game support was inferred'
    Verify ($report.Route.Note -match 'DLSS' -and $report.Route.Note -match '补帧') 'No plain-language route explanation'
}
Case 'untouched settings may change after upgrade without blocking binary undo' {
    $f=Fixture
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    Put (Join-Path $f.Game 'dlss5-033.cfg') "engine=0`r`ninject=0`r`nuserkey=later edit"
    Put (Join-Path $f.Game 'dlss5-033.state') '8'
    $cfgHash=Hash (Join-Path $f.Game 'dlss5-033.cfg');$counterHash=Hash (Join-Path $f.Game 'dlss5-033.state')
    & $f.Installer -Action Restore -Receipt $installed.Receipt|Out-Null
    Verify ((Hash (Join-Path $f.Game 'winmm.dll')) -eq $f.Before['winmm.dll']) 'Core not restored'
    Verify ((Hash (Join-Path $f.Game 'dlss5-033.cfg')) -eq $cfgHash -and (Hash (Join-Path $f.Game 'dlss5-033.state')) -eq $counterHash) 'Rollback overwrote later settings'
}
Case 'binary edit still blocks exact restore before any mutation' {
    $f=Fixture
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    Put (Join-Path $f.Game 'dlss5-033.addon64') 'third-party edit'
    $coreHash=Hash (Join-Path $f.Game 'winmm.dll');$blocked=$false
    try {& $f.Installer -Action Restore -Receipt $installed.Receipt|Out-Null}catch{$blocked=$true}
    Verify ($blocked -and (Hash (Join-Path $f.Game 'winmm.dll')) -eq $coreHash) 'Rollback overwrote files before rejecting later binary edit'
}
Case 'missing managed settings block Plan while absent counter initializes reversibly' {
    $f=Fixture;$optiPath=Join-Path $f.Game 'OptiScaler.ini';$opti=[IO.File]::ReadAllBytes($optiPath)
    Remove-Item -LiteralPath $optiPath
    $blocked=$false;try {& $f.Installer -Action Plan -GameExe $f.Exe -PackageRoot $f.Package|Out-Null}catch{$blocked=$true}
    Verify $blocked 'Missing managed INI accepted by Plan'
    [IO.File]::WriteAllBytes($optiPath,$opti)
    $statePath=Join-Path $f.Game 'dlss5-033.state';Remove-Item -LiteralPath $statePath
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    Verify ([IO.File]::ReadAllText($statePath) -eq '0') 'Absent counter not initialized'
    & $f.Installer -Action Restore -Receipt $installed.Receipt|Out-Null
    Verify (-not(Test-Path -LiteralPath $statePath)) 'Undo did not restore original counter absence'
}
Case 'package cannot overwrite preserved config through a payload alias' {
    $f=Fixture;$manifestPath=Join-Path $f.Package 'manifest.json'
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    $manifest.Payload+=@{Path=$manifest.Files[0].Path;SHA256=$manifest.Files[0].SHA256;Target='.\dlss5-033.cfg'}
    Put $manifestPath ($manifest|ConvertTo-Json -Depth 8)
    $blocked=$false;try {& $f.Installer -Action Plan -GameExe $f.Exe -PackageRoot $f.Package|Out-Null}catch{$blocked=$true}
    Verify $blocked 'Payload bypassed settings preservation'
    SameSettings $f
}
Case 'unmanaged native integration keeps occupied proxy and supports exact remount undo' {
    $f=Fixture
    foreach($name in @('_033-integrated.json','winmm.dll','OptiScaler.ini')){Remove-Item -LiteralPath (Join-Path $f.Game $name)}
    Put (Join-Path $f.Game 'dxgi.dll') 'existing ReShade proxy'
    Put (Join-Path $f.Game '_安装记录.txt') "proxy=dxgi.dll`r`nproxyall=dxgi.dll`r`nproxycands=dxgi.dll,d3d11.dll,dinput8.dll`r`nEnableHooks=`r`n"
    $recordHash=Hash (Join-Path $f.Game '_安装记录.txt');$dxgiHash=Hash (Join-Path $f.Game 'dxgi.dll')
    $installed=& $f.Installer -Action Install -GameExe $f.Exe -PackageRoot $f.Package
    Verify ($installed.CoreMount -eq 'winmm.dll' -and (Hash (Join-Path $f.Game 'dxgi.dll')) -eq $dxgiHash) 'Existing ReShade proxy overwritten'
    Verify ((Hash (Join-Path $f.Game 'dlss5-033.state')) -eq $f.Before['dlss5-033.state']) 'First integration reset existing counter'
    & $f.Installer -Action Restore -Receipt $installed.Receipt|Out-Null
    Verify (-not(Test-Path -LiteralPath (Join-Path $f.Game 'winmm.dll'))) 'First integration undo retained new core'
    $remount=Join-Path (Split-Path -Parent $f.Installer) 'integrated_remount.ps1'
    $receipt=& $remount -GameRoot $f.Game -Auto
    Verify ((Test-Path -LiteralPath (Join-Path $f.Game 'd3d11.dll')) -and -not(Test-Path -LiteralPath (Join-Path $f.Game 'dxgi.dll'))) 'Fixture remount failed'
    & $remount -GameRoot $f.Game -Undo -Receipt $receipt|Out-Null
    Verify ((Hash (Join-Path $f.Game 'dxgi.dll')) -eq $dxgiHash -and (Hash (Join-Path $f.Game '_安装记录.txt')) -eq $recordHash -and -not(Test-Path -LiteralPath (Join-Path $f.Game 'd3d11.dll'))) 'Remount undo failed to restore original bytes'
}
Case 'existing installer lifecycle suite runs on inert payloads' {
    $f=Fixture;$renodx=Join-Path $fixtureRoot 'renodx-fixture.addon64';Put $renodx 'reviewed inert RenoDX fixture'
    $manifestPath=Join-Path $f.Package 'manifest.json';$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    $manifest|Add-Member -NotePropertyName RenoDxBaselineHash -NotePropertyValue (Hash $renodx)
    Put $manifestPath ($manifest|ConvertTo-Json -Depth 8)
    & (Join-Path $PSScriptRoot 'integrated_installer_test.ps1') -PackageRoot $f.Package -SmokeExe $f.Exe -RenoDxBaseline $renodx
}
$summary=@{Suite='InstallerCpu';Fixtures=$fixtureRoot;DeployRoot=$DeployRoot;PowerShell=$PSVersionTable.PSVersion.ToString();Passed=@($results|Where-Object {$_.Passed}).Count;Failed=@($results|Where-Object {-not $_.Passed}).Count;Results=$results.ToArray();GpuExecuted=$false;GameExecuted=$false;DriverToolsExecuted=$false}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'installer-results.json'),($summary|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ("Installer preservation: {0} passed, {1} failed" -f $summary.Passed,$summary.Failed)
if($summary.Failed){throw 'Installer regression failures; see installer-results.json'}
