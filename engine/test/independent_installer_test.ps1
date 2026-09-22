param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# All files are inert fixtures under the reviewed wrapper's single output root.
# No fixture PE, runtime, SDK, game, GUI, driver or background worker is executed.
$ErrorActionPreference='Stop'
$deploy=Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy'
. (Join-Path $deploy 'independent_transaction.ps1')
$legacyRestore=Join-Path $PSScriptRoot 'fixtures/independent-installer/restore-before-gap.ps1'
if((Get-033FileHash $legacyRestore) -ine 'ccb8b3b669f5cedb296509c41caaaf9279a059b51c8e3798496610467ec294eb'){throw 'Legacy regression source changed'}
. $legacyRestore
$expected=Get-033PhysicalRoot (Join-Path (Split-Path -Parent $PSScriptRoot) 'build/parallel-InstallerIndependentCpu')
if((Get-033PhysicalRoot $OutputDirectory) -ine $expected){throw 'Use only the approved InstallerIndependentCpu wrapper output'}
$work=Join-Path $expected ('fixtures-'+[Guid]::NewGuid().ToString('N').Substring(0,6));[void][IO.Directory]::CreateDirectory($work)
$results=[Collections.Generic.List[object]]::new();$script:stepHook=$null
function Invoke-033IndependentStep([string]$Stage,[string]$Receipt,[string]$Path){if($script:stepHook){& $script:stepHook $Stage $Receipt $Path}}
function Check($Ok,[string]$Message){if(-not $Ok){throw $Message}}
function MustFail([scriptblock]$Body,[string]$Message){$failed=$false;try{& $Body|Out-Null}catch{$failed=$true;Check ($_.Exception.Message -like ('*'+$Message+'*')) ('Unexpected failure: '+$_.Exception.Message)};Check $failed ('Expected rejection: '+$Message)}
function Case([string]$Name,[scriptblock]$Body){
    try{& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output ('PASS '+$Name)}
    catch{$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message;Stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$Name+': '+$_.Exception.Message)}
    finally{$script:stepHook=$null}
}
function Put([string]$Path,[string]$Text){[void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path));[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Hash([string]$Path){$hash=Get-033FileHash $Path;if($hash){return $hash.ToLowerInvariant()};return $null}
function Pe([string]$Path,[string]$Arch,[bool]$Dll,[string[]]$Imports=@(),[byte]$Tag=7){
    $b=[byte[]]::new(2048);$optional=if($Arch -eq 'x64'){240}else{224};$section=152+$optional
    foreach($pair in @(@(0,0x5a4d),@(132,$(if($Arch -eq 'x64'){0x8664}else{0x14c})),@(134,1),@(148,$optional),@(150,$(if($Dll){0x2000}else{0})),@(152,$(if($Arch -eq 'x64'){0x20b}else{0x10b})),@(220,2))){[BitConverter]::GetBytes([uint16]$pair[1]).CopyTo($b,$pair[0])}
    foreach($pair in @(@(60,128),@(128,0x4550),@(212,512),@(($section+8),1536),@(($section+12),4096),@(($section+16),1536),@(($section+20),512))){[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($b,$pair[0])}
    $directory=if($Arch -eq 'x64'){264}else{248};[BitConverter]::GetBytes([uint32]16).CopyTo($b,$directory-4)
    if($Imports.Count){[BitConverter]::GetBytes([uint32]4096).CopyTo($b,$directory+8);[BitConverter]::GetBytes([uint32](20*($Imports.Count+1))).CopyTo($b,$directory+12)
        for($i=0;$i -lt $Imports.Count;$i++){[BitConverter]::GetBytes([uint32](4288+48*$i)).CopyTo($b,524+20*$i);[Text.Encoding]::ASCII.GetBytes($Imports[$i]+[char]0).CopyTo($b,704+48*$i)}
    }
    $b[2000]=$Tag;[void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path));[IO.File]::WriteAllBytes($Path,$b)
}
function NewFixture([string]$Arch='x64',[bool]$ExistingSettings=$false){
    $base=Join-Path $work ([Guid]::NewGuid().ToString('N').Substring(0,6));$game=Join-Path $base 'game';$settings=Join-Path $base 'settings';$vault=Join-Path $base 'vault';$pkg=Join-Path $base 'package';$evidence=Join-Path $base 'ownership'
    foreach($path in @($game,$settings,$vault,$pkg,$evidence)){[void][IO.Directory]::CreateDirectory($path)}
    $exe=Join-Path $game 'inert-game.exe';Pe $exe $Arch $false @($(if($Arch -eq 'x64'){'d3d11.dll'}else{'d3d9.dll'}))
    foreach($name in @('winmm.dll','old-addon.addon64','old-owned.cfg','old-replaced.dll')){Put (Join-Path $game $name) ('inert old 033 '+$name)}
    foreach($name in @('other-mod.dll','game.save','crash.log','history.zip','nvngx_dlss.dll')){Put (Join-Path $game $name) ('protected fixture '+$name)}
    Write-033Json (Join-Path $game '_033-integrated.json') @{Version=1;GameExe=$exe;PackageId='old-fixture';CoreMount='winmm.dll';CoreHash=(Hash (Join-Path $game 'winmm.dll'))}
    Copy-Item -LiteralPath (Join-Path $game '_033-integrated.json') -Destination (Join-Path $evidence 'owner.json')
    $legacyDir=Join-Path $evidence 'legacy';[void][IO.Directory]::CreateDirectory($legacyDir)
    Put (Join-Path $legacyDir 'original.bin') 'original unrelated Mod before any 033'
    $legacyFiles=@();$inventory=@();$ownedTargets=@()
    $ownedNames=@('winmm.dll','old-addon.addon64','old-owned.cfg','old-replaced.dll','_033-integrated.json')
    foreach($name in $ownedNames){
        $exists=$name -eq 'old-replaced.dll'
        $legacyFiles+=@(@{Path=$name;Existed=$exists;BeforeHash=if($exists){Hash (Join-Path $legacyDir 'original.bin')}else{$null};BeforeAttributes=32;BeforeTime=if($exists){'2020-01-02T03:04:05.0000000Z'}else{$null};Backup=if($exists){'original.bin'}else{$null};AfterHash=(Hash (Join-Path $game $name))})
    }
    $legacyPath=Join-Path $legacyDir 'receipt.json';Write-033Json $legacyPath @{Version=2;Status='installed';Root=$game;Files=$legacyFiles}
    foreach($file in (Get-ChildItem -LiteralPath $game -File)){$inventory+=@(@{path=$file.Name;sha256=(Hash $file.FullName);disposition=if($file.Name -in $ownedNames){'owned'}else{'protected'}})}
    foreach($name in $ownedNames){$ownedTargets+=@(@{path=$name;receipt=@{path='legacy/receipt.json';sha256=(Hash $legacyPath)}})}
    $ledgerPath=Join-Path $evidence 'ownership.json';Write-033Json $ledgerPath @{schemaVersion=1;gameRoot=$game;gameExeSHA256=(Hash $exe);ownerRecord=@{path='owner.json';sha256=(Hash (Join-Path $evidence 'owner.json'))};inventory=$inventory;ownedTargets=$ownedTargets}
    if($ExistingSettings){Put (Join-Path $settings 'settings.ini') 'old mixed global settings must not be imported'}
    # Establish the permanent lock identity in setup; transactions never erase it.
    $setupLock=[Installer033.SettingsLockV1]::new((Join-Path $settings 'settings.ini.033lock'));$setupLock.Dispose()
    Put (Join-Path $pkg 'evidence/fixture.json') '{"fixtureOnly":true,"nativeExecuted":false,"notAProductionAcceptance":true}'
    $ref=@{path='evidence/fixture.json';sha256=(Hash (Join-Path $pkg 'evidence/fixture.json'))}
    $files=@()
    function AddFile([string]$Id,[string]$Target,[string]$Role,[string]$Kind,[string]$Bitness,[string[]]$Dependencies=@()){
        $source='parts/'+$Id+$(if($Kind -eq 'dll'){'.dll'}elseif($Kind -eq 'exe'){'.exe'}else{'.txt'})
        if($Kind -eq 'data'){Put (Join-Path $pkg $source) $(if($Role -eq 'clean-defaults'){'; CPU FIXTURE DEFAULTS ONLY'+[Environment]::NewLine+'fixture=neutral'}else{'inert fixture '+$Id})}
        else{Pe (Join-Path $pkg $source) $Bitness ($Kind -eq 'dll') @() ([byte](10+$files.Count))}
        $file=@{id=$Id;sourcePath=$source;targetRoot=if($Role -eq 'clean-defaults'){'shared-settings'}else{'game'};targetPath=$Target;role=$Role;sha256=(Hash (Join-Path $pkg $source));bytes=(Get-Item -LiteralPath (Join-Path $pkg $source)).Length;pe=@{kind=$Kind;architecture=$Bitness;subsystem=if($Kind -eq 'data'){$null}else{2}};dependsOn=$Dependencies;licenseFileIds=@(if($Kind -ne 'data'){'license'});provenanceFileIds=@(if($Kind -ne 'data'){'provenance'})}
        return $file
    }
    $files+=@(AddFile 'license' '033-runtime/licenses/fixture/LICENSE' 'license' 'data' 'none')
    $files+=@(AddFile 'provenance' '033-runtime/provenance/fixture/SOURCE.json' 'provenance' 'data' 'none')
    $files+=@(AddFile 'defaults' 'settings.ini' 'clean-defaults' 'data' 'none')
    $files+=@(AddFile 'worker' '033-runtime/033-worker64.exe' 'worker' 'exe' 'x64')
    $files+=@(AddFile 'runtime' '033-runtime/033-runtime.dll' 'runtime' 'dll' 'x64' @('worker'))
    $files+=@(AddFile 'client' '033-runtime/033-worker-client32.dll' 'client' 'dll' 'x86' @('worker'))
    $files+=@(AddFile 'compat' '033-runtime/compat32/033-compat32.dll' 'compatibility' 'dll' 'x86' @('client','worker'))
    $files+=@(AddFile 'entry64' 'd3d11.dll' 'entry' 'dll' 'x64' @('runtime'))
    $files+=@(AddFile 'entry32' 'd3d9.dll' 'entry' 'dll' 'x86' @('client','compat','worker'))
    $routes=@();foreach($bits in @('x64','x86')){$routes+=@(@{id=$bits;exeArchitecture=$bits;state='verified';entryFileId=if($bits -eq 'x64'){'entry64'}else{'entry32'};selectors=@(@{importName=if($bits -eq 'x64'){'d3d11.dll'}else{'d3d9.dll'};importKind='normal';api=if($bits -eq 'x64'){'d3d11'}else{'d3d9'};evidence=$ref});requiredFileIds=@();requiredExternalDependencyIds=@('models','vendor');evidence=$ref;pendingReasons=@()})}
    $bindings=@();foreach($file in $files){foreach($id in $file.dependsOn){$dep=@($files|Where-Object {$_.id -eq $id})[0];$bindings+=@(@{id=$file.id+'-'+$id;state='verified';consumerFileId=$file.id;dependencyFileId=$id;expectedDependencySha256=$dep.sha256;protocols=@(@{domain='c-api';name='inert-fixture-interface';version='fixture-1';byteSize=1;evidence=$ref});evidence=$ref;pendingReasons=@()})}}
    $capabilities=@();foreach($id in @('sr','nr','fg','single-panel','automatic-bridge','managed-lifecycle','normal-present','no-aux-window')){$capabilities+=@(@{id=$id;state='verified';routeIds=@('x64','x86');evidence=$ref;pendingReasons=@()})}
    $external=@();foreach($pair in @(@('models','model'),@('vendor','other-required-runtime'))){$external+=@(@{id=$pair[0];kind=$pair[1];required=$false;state='not-required';requirement='inert CPU fixture has no vendor/model execution';verification=$ref;copyPolicy='verify-only';pendingReasons=@()})}
    # installable=true exercises a synthetic positive branch only. These PEs
    # have no code/entry point; no S35/S36 file or production claim is used.
    $manifest=@{schemaVersion=1;packageKind='033-independent-runtime';packageId='cpu-fixture-only';buildId='inert-no-native-code';installable=$true;readiness=@{contractState='reviewed';codeAndOfflineComplete=$true;runtimeBuildEvidence=$ref;integrationReviewEvidence=$ref;gameAcceptance='not-run';blockers=@()};files=$files;routes=$routes;bindings=$bindings;externalDependencies=$external;requiredCapabilities=$capabilities;cleanDefaults=@{state='verified';scope='shared-user-all-games';location='LocalAppData/033Runtime/settings.ini';fileId='defaults';settingsSchema='inert-fixture-only';sourceEvidence=$ref;existingPolicy='explicit-global-plan-only';legacyImportPolicy='never';pendingReasons=@()}}
    $policyPath=Join-Path $pkg 'evidence/requirements.json'
    Write-033Json $policyPath @{schemaVersion=1;requiredCapabilityIds=@($capabilities|ForEach-Object {$_.id});requiredRouteIds=@('x64','x86');requiredExternalDependencyIds=@('models','vendor');rejectedBuildIds=@()}
    $manifest.requirementsProfile=@{path='evidence/requirements.json';sha256=(Hash $policyPath)}
    $manifestPath=Join-Path $pkg '033-independent-package.json';Write-033Json $manifestPath $manifest
    $context=@{Workspace=$expected;GameState='closed';GlobalSessionsExcluded=$true;PendingSavesDrained=$true}
    $before=@{};foreach($file in (Get-ChildItem -LiteralPath $game -File)){$before[$file.Name]=Get-033MigrationSnapshot $game $file.Name}
    return @{Base=$base;Game=$game;Exe=$exe;Settings=$settings;Vault=$vault;Package=$pkg;Manifest=$manifest;ManifestPath=$manifestPath;RequirementsSHA256=(Hash $policyPath);LedgerPath=$ledgerPath;Context=$context;Before=$before;SettingsBefore=(Get-033MigrationSnapshot $settings 'settings.ini')}
}
function SaveManifest($Fixture){Write-033Json $Fixture.ManifestPath $Fixture.Manifest}
function Package($Fixture){Read-033IndependentPackage $Fixture.Package (Hash $Fixture.ManifestPath) $Fixture.RequirementsSHA256}
function Plan($Fixture,[switch]$Reset,[switch]$Production){
    $planArguments=@{GameExe=$Fixture.Exe;PackageRoot=$Fixture.Package;ManifestSHA256=(Hash $Fixture.ManifestPath);RequirementsSHA256=$Fixture.RequirementsSHA256;LedgerPath=$Fixture.LedgerPath;LedgerSHA256=(Hash $Fixture.LedgerPath);SettingsRoot=$Fixture.Settings;VaultRoot=$Fixture.Vault;ResetSharedSettings=$Reset}
    $selectionPath=Join-Path $Fixture.Base 'target-selection.json'
    Write-033Json $selectionPath (Get-033TargetSelection @($Fixture.Exe) $Fixture.Exe 'Explicit inert fixture target; never a live game')
    $targetArguments=$planArguments.Clone();$targetArguments.Remove('GameExe')
    $readonlyPlan=New-033TargetBoundPlan -TargetReportPath $selectionPath -TargetReportSHA256 (Hash $selectionPath) @targetArguments
    if(-not $Production){$planArguments.FixtureContext=$Fixture.Context}
    $plan=if($Production){$readonlyPlan}else{New-033IndependentPlan @planArguments}
    if(-not $Production){$plan|Add-Member -NotePropertyName TargetEvidence -NotePropertyValue $readonlyPlan.TargetEvidence}
    $path=Join-Path $Fixture.Base 'plan.json';Write-033Json $path $plan
    return @{Data=$plan;Path=$path;SHA256=(Hash $path)}
}
function Execute($Fixture,$Plan){Invoke-033IndependentFixture $Plan.Path $Plan.SHA256 $Fixture.Context}
function Restore($Fixture,[string]$Receipt){Restore-033IndependentFixture $Receipt (Hash $Receipt) $Fixture.Context}
function ExactBefore($Fixture){foreach($name in $Fixture.Before.Keys){Assert-033MigrationBefore $Fixture.Game $name $Fixture.Before[$name]};Assert-033MigrationBefore $Fixture.Settings 'settings.ini' $Fixture.SettingsBefore}
Case 'strict JSON rejects duplicate keys and keeps ISO dates as strings' {
    MustFail {[Installer033.StrictJsonV1]::Parse('{"id":1,"id":2}')} 'Duplicate JSON key'
    $value=[Installer033.StrictJsonV1]::Parse('{"value":"2026-09-08T00:00:00Z"}')
    Check ($value['value'] -is [string]) 'JSON coerced an identity string into DateTime'
}
Case 'x64 selects one modern entry and necessary runtime closure; exact migration undo' {
    $f=NewFixture;$p=Plan $f;Check $p.Data.Ready ($p.Data.Blockers -join '; ');ExactBefore $f
    Check ('entry64' -in $p.Data.SelectedFileIds -and 'client' -notin $p.Data.SelectedFileIds -and 'compat' -notin $p.Data.SelectedFileIds) 'x64 closure contains unnecessary x86 payload'
    $result=Execute $f $p
    Check ($result.Status -eq 'installed-in-fixture-only' -and -not(Test-Path -LiteralPath (Join-Path $f.Game 'winmm.dll'))) 'Old owned entry was not retired'
    Check ([IO.File]::ReadAllText((Join-Path $f.Game 'old-replaced.dll')) -eq 'original unrelated Mod before any 033') 'Original pre-033 Mod was not restored'
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game 'd3d9.dll')) -and -not(Test-Path -LiteralPath (Join-Path $f.Game '033-runtime/033-worker-client32.dll'))) 'Unselected entry/client installed'
    $receipt=Read-033StrictJson $result.Receipt
    Check (@($receipt.Files|Where-Object {$_.OriginalPre033Archive}).Count -eq 1) 'Original-pre-033 archive not distinguished from migration-before'
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
    Check ([IO.File]::ReadAllText((Join-Path $f.Game 'old-replaced.dll')) -eq 'inert old 033 old-replaced.dll') 'Undo mistakenly restored pre-033 bytes instead of migration-before'
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Game 'd3d11.dll'))) 'New entry absence not restored'
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'x86 selects embedded loader entry/client/compat/worker without dev host or runtime64' {
    $f=NewFixture 'x86';$p=Plan $f;Check $p.Data.Ready ($p.Data.Blockers -join '; ')
    $result=Execute $f $p
    foreach($name in @('d3d9.dll','033-runtime/033-worker-client32.dll','033-runtime/compat32/033-compat32.dll','033-runtime/033-worker64.exe')){Check (Test-Path -LiteralPath (Join-Path $f.Game $name)) ('Missing x86 closure: '+$name)}
    foreach($name in @('d3d11.dll','033-runtime/033-runtime.dll','033-host.exe','033-runtime/033-loader32.dll')){Check (-not(Test-Path -LiteralPath (Join-Path $f.Game $name))) ('Unexpected optional/development component: '+$name)}
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'non-installable milestone stays plan-only even with a complete-looking fixture' {
    $f=NewFixture;$f.Manifest.installable=$false;SaveManifest $f;$p=Plan $f
    Check (-not $p.Data.Ready) 'Non-installable package became ready';MustFail {Execute $f $p} 'ready offline fixture';ExactBefore $f
}
Case 'pending external model/default fields cannot be omitted as no dependency' {
    $f=NewFixture;$f.Manifest.installable=$false;$f.Manifest.externalDependencies[0].state='pending';$f.Manifest.externalDependencies[0].verification=$null;$f.Manifest.externalDependencies[0].pendingReasons=@('models unresolved')
    $f.Manifest.cleanDefaults.state='pending';$f.Manifest.cleanDefaults.fileId=$null;$f.Manifest.cleanDefaults.sourceEvidence=$null;$f.Manifest.cleanDefaults.pendingReasons=@('final defaults unresolved');SaveManifest $f
    $p=Plan $f;Check (-not $p.Data.Ready -and ($p.Data.Blockers -join ';') -like '*Pending external*' -and ($p.Data.Blockers -join ';') -like '*defaults*') 'Pending dependency/default disappeared'
}
Case 'wrong embedded hash and wrong PE architecture are rejected before planning changes' {
    $f=NewFixture;$f.Manifest.bindings[0].expectedDependencySha256='0'*64;SaveManifest $f;MustFail {Package $f} 'binding mismatch';ExactBefore $f
    $f=NewFixture;$f.Manifest.files[4].pe.architecture='x86';SaveManifest $f;MustFail {Package $f} 'PE mismatch';ExactBefore $f
}
Case 'required pending rows cannot be deleted and a rejected build cannot regain installability' {
    $f=NewFixture;$f.Manifest.externalDependencies=@($f.Manifest.externalDependencies|Where-Object {$_.id -ne 'models'});SaveManifest $f
    MustFail {Package $f} 'Required policy item omitted';ExactBefore $f
    $f=NewFixture;$policyPath=Join-Path $f.Package 'evidence/requirements.json';$policy=Read-033StrictJson $policyPath;$policy.rejectedBuildIds=@($f.Manifest.buildId);Write-033Json $policyPath $policy
    $f.RequirementsSHA256=Hash $policyPath;$f.Manifest.requirementsProfile.sha256=$f.RequirementsSHA256;SaveManifest $f
    MustFail {Package $f} 'unresolved blockers';ExactBefore $f
}
Case 'missing license, development host and cyclic dependency are rejected' {
    $f=NewFixture;$f.Manifest.files[4].licenseFileIds=@();SaveManifest $f;MustFail {Package $f} 'license is missing'
    $f=NewFixture;$f.Manifest.files[3].targetPath='033-runtime/033-host.exe';SaveManifest $f;MustFail {Package $f} 'Development payload'
    $f=NewFixture;$f.Manifest.files[0].dependsOn=@('entry64');SaveManifest $f;MustFail {Plan $f} 'dependency cycle'
}
Case 'unknown API normal import and protected new entry name produce blocked plans' {
    $f=NewFixture;Pe $f.Exe 'x64' $false @('opengl32.dll');$ledger=Read-033StrictJson $f.LedgerPath;$ledger.gameExeSHA256=Hash $f.Exe;foreach($item in $ledger.inventory){if($item.path -eq 'inert-game.exe'){$item.sha256=Hash $f.Exe}};Write-033Json $f.LedgerPath $ledger
    $p=Plan $f;Check (-not $p.Data.Ready -and ($p.Data.Blockers -join ';') -like '*selection*') 'Unknown API guessed an entry'
    $f=NewFixture;Put (Join-Path $f.Game 'd3d11.dll') 'unowned other Mod';$p=Plan $f
    Check (-not $p.Data.Ready -and ($p.Data.Blockers -join ';') -like '*unknown/protected owner*') 'Existing entry filename was blindly overwritten'
}
Case 'legacy source receipt, current ownership and original backup hashes are independent gates' {
    $f=NewFixture;Put (Join-Path $f.Game 'winmm.dll') 'external change';MustFail {Plan $f} 'Inventory changed'
    $f=NewFixture;Put (Join-Path (Split-Path -Parent $f.LedgerPath) 'legacy/original.bin') 'changed original';MustFail {Plan $f} 'Original pre-033 backup'
    $f=NewFixture;Put (Join-Path (Split-Path -Parent $f.LedgerPath) 'legacy/receipt.json') '{}';MustFail {Plan $f} 'Evidence missing or changed'
}
Case 'unknown inventory is reported and all unrelated Mod/save/native/history files remain untouched' {
    $f=NewFixture;$ledger=Read-033StrictJson $f.LedgerPath;foreach($item in $ledger.inventory){if($item.path -eq 'other-mod.dll'){$item.disposition='unknown'}};Write-033Json $f.LedgerPath $ledger
    $p=Plan $f;Check (-not $p.Data.Ready -and ($p.Data.Blockers -join ';') -like '*Unresolved loading-chain*') 'Unknown loader-chain file was silently ignored';ExactBefore $f
}
Case 'vault inside target, escaping relative target and physical hard link are rejected' {
    $f=NewFixture;$f.Vault=Join-Path $f.Game 'backup';MustFail {Plan $f} 'physically outside'
    $f=NewFixture;$f.Manifest.files[4].targetPath='../escape.dll';SaveManifest $f;MustFail {Package $f} 'pattern mismatch'
    $f=NewFixture;$link=Join-Path $f.Game 'linked-copy.bin';New-Item -ItemType HardLink -Path $link -Target (Join-Path $f.Game 'winmm.dll')|Out-Null
    MustFail {Plan $f} 'Hard-linked migration target'
}
Case 'existing common settings require explicit global reset plus excluded sessions and drained saves' {
    $f=NewFixture 'x64' $true;$p=Plan $f;Check (-not $p.Data.Ready) 'Single game install reset existing common settings';ExactBefore $f
    $f.Context.GlobalSessionsExcluded=$false;$p=Plan $f -Reset;Check (-not $p.Data.Ready) 'Running global session accepted'
    $f.Context.GlobalSessionsExcluded=$true;$f.Context.PendingSavesDrained=$false;$p=Plan $f -Reset;Check (-not $p.Data.Ready) 'Deferred old saves accepted'
    $f.Context.PendingSavesDrained=$true;$p=Plan $f -Reset;Check $p.Data.Ready ($p.Data.Blockers -join '; ')
    $result=Execute $f $p;Check (-not [IO.File]::ReadAllText((Join-Path $f.Settings 'settings.ini')).Contains('old mixed')) 'Old mixed configuration was imported'
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'production plan cannot write shared settings or execute even if fixture metadata claims ready' {
    $f=NewFixture 'x64' $true;$p=Plan $f -Reset -Production
    Check (-not $p.Data.Ready -and ($p.Data.Blockers -join ';') -like '*Production global session exclusion*') 'Production global barrier was bypassed'
    MustFail {Execute $f $p} 'ready offline fixture';ExactBefore $f
}
Case 'real byte-range settings lock excludes the transaction and its identity is preserved' {
    $f=NewFixture;$p=Plan $f;$lockPath=Join-Path $f.Settings 'settings.ini.033lock';$guard=[Installer033.SettingsLockV1]::new($lockPath)
    try{MustFail {Execute $f $p} 'lock is busy';ExactBefore $f}finally{$guard.Dispose()}
    $receipt=@(Get-ChildItem -LiteralPath $f.Vault -Filter receipt.json -Recurse)[0].FullName
    Restore $f $receipt|Out-Null;ExactBefore $f;Check (Test-Path -LiteralPath $lockPath) 'Persistent settings lock file was removed'
}
Case 'all snapshots and new payloads are durable outside target roots before first target write' {
    $f=NewFixture;$p=Plan $f;$script:archiveSeen=$false
    $script:stepHook={param($Stage,$Receipt,$Path)
        if($Stage -eq 'all-archives-durable'){
            ExactBefore $f;$state=Read-033StrictJson $Receipt;Check ($state.Status -eq 'prepared' -and $state.Files.Count -eq $p.Data.Actions.Count) 'Incomplete staging before writes'
            foreach($item in $state.Files){Check ($item.Phase -eq 'prepared') 'A target was touched before staging completed'}
            $script:archiveSeen=$true
        }
        if($Stage -eq 'before-write'){Check $script:archiveSeen 'Target mutation preceded full durable archive'}
    }
    $result=Execute $f $p;Check $script:archiveSeen 'Staging boundary not observed';$script:stepHook=$null;Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'failure after a runtime write restores migration-before files and preserves original ownership archives' {
    $f=NewFixture;$p=Plan $f
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-write' -and $Path -eq '033-runtime/033-runtime.dll'){throw 'INERT INJECTED WRITE FAILURE'}}
    MustFail {Execute $f $p} 'migration-before state restored';$script:stepHook=$null;ExactBefore $f
    Check ([IO.File]::ReadAllText((Join-Path (Split-Path -Parent $f.LedgerPath) 'legacy/original.bin')) -eq 'original unrelated Mod before any 033') 'Historical original backup changed'
}
Case 'external target change during write is not overwritten by automatic recovery' {
    $f=NewFixture;$p=Plan $f
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'before-write' -and $Path -eq 'old-owned.cfg'){Put (Join-Path $f.Game $Path) 'external edit at boundary'}}
    MustFail {Execute $f $p} 'Recovery blocked';$script:stepHook=$null
    Check ([IO.File]::ReadAllText((Join-Path $f.Game 'old-owned.cfg')) -eq 'external edit at boundary') 'External edit was overwritten'
}
Case 'restore preflights every target before changing any file and never substitutes original-pre-033 for undo' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;Put (Join-Path $f.Game '033-runtime/033-runtime.dll') 'external post-install edit'
    $entryHash=Hash (Join-Path $f.Game 'd3d11.dll');MustFail {Restore $f $result.Receipt} 'External modification blocks restore'
    Check ((Hash (Join-Path $f.Game 'd3d11.dll')) -eq $entryHash) 'Restore changed early targets before finding a later conflict'
}
Case 'plan and input hashes prevent stale or altered execution; roots cannot escape fixture scope' {
    $f=NewFixture;$p=Plan $f;[IO.File]::AppendAllText($p.Path,' ');MustFail {Execute $f $p} 'plan SHA256 mismatch';ExactBefore $f
    $f=NewFixture;$p=Plan $f;[IO.File]::AppendAllText($f.ManifestPath,' ');MustFail {Execute $f $p} 'Pinned plan inputs changed';ExactBefore $f
    $f=NewFixture;$p=Plan $f;$changed=Read-033StrictJson $p.Path;$changed.Roots.game=Split-Path -Parent $expected;Write-033Json $p.Path $changed;$p.SHA256=Hash $p.Path
    MustFail {Execute $f $p} 'escaped its physical workspace';ExactBefore $f
}
Case 'required binary bindings and unique references cannot be omitted or detached' {
    $f=NewFixture;$f.Manifest.bindings=@($f.Manifest.bindings|Select-Object -Skip 1);SaveManifest $f;MustFail {Package $f} 'lacks a pinned binding'
    $f=NewFixture;$f.Manifest.files[4].dependsOn=@();SaveManifest $f;MustFail {Package $f} 'not in the consumer closure'
    $f=NewFixture;$f.Manifest.bindings+=@($f.Manifest.bindings[0]);SaveManifest $f;MustFail {Package $f} 'Duplicate component binding'
}
Case 'reparse paths and directory collisions are rejected without mutating targets' {
    $f=NewFixture;$alias=Join-Path $f.Base 'vault-junction';New-Item -ItemType Junction -Path $alias -Target $f.Vault|Out-Null
    $f.Vault=$alias;MustFail {Plan $f} 'Reparse';ExactBefore $f
    $f=NewFixture;[void][IO.Directory]::CreateDirectory((Join-Path $f.Game 'd3d11.dll'));MustFail {Plan $f} 'directory occupies file path';ExactBefore $f
}
Case 'source drift after plan prevents execution before any target write' {
    $f=NewFixture;$p=Plan $f;[IO.File]::AppendAllText((Join-Path $f.Package 'parts/runtime.dll'),'changed fixture payload')
    MustFail {Execute $f $p} 'Package component changed';ExactBefore $f
}
Case 'read-only file attributes and precise timestamps survive migration undo' {
    $f=NewFixture;$path=Join-Path $f.Game 'old-owned.cfg'
    [IO.File]::SetLastWriteTimeUtc($path,[DateTime]::new(2024,2,3,4,5,6,[DateTimeKind]::Utc));[IO.File]::SetAttributes($path,[IO.FileAttributes]::ReadOnly);$f.Before['old-owned.cfg']=Get-033MigrationSnapshot $f.Game 'old-owned.cfg'
    $p=Plan $f;$result=Execute $f $p;Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'global exclusion loss during transaction blocks further writes and recovery until drained' {
    $f=NewFixture;$p=Plan $f
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-write' -and $Path -eq '033-runtime/033-runtime.dll'){$f.Context.PendingSavesDrained=$false}}
    MustFail {Execute $f $p} 'Recovery blocked';$script:stepHook=$null
    Check (-not(Test-Path -LiteralPath (Join-Path $f.Settings 'settings.ini'))) 'Shared settings written after drain evidence was lost'
    $f.Context.PendingSavesDrained=$true;$receipt=@(Get-ChildItem -LiteralPath $f.Vault -Filter receipt.json -Recurse)[0].FullName
    Restore $f $receipt|Out-Null;ExactBefore $f
}
Case 'interrupted restore journal can resume exact migration-before recovery' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-restore'){throw 'INERT INTERRUPTED RESTORE'}}
    MustFail {Restore $f $result.Receipt} 'INERT INTERRUPTED RESTORE';$script:stepHook=$null
    Check ((Read-033StrictJson $result.Receipt).Status -eq 'restoring') 'Interrupted restore phase was lost'
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
$cliReceipts=[Collections.Generic.List[object]]::new()
function InvokeFixtureIndependentCli([string[]]$Arguments){
    $shell=Join-Path $PSHOME $(if($PSVersionTable.PSVersion.Major -eq 5){'powershell.exe'}else{'pwsh.exe'})
    Check ([IO.File]::Exists($shell)) 'Current reviewed PowerShell executable missing'
    $id=[Guid]::NewGuid().ToString('N');$out=Get-033IndependentPath $work ('cli-'+$id+'.stdout');$err=Get-033IndependentPath $work ('cli-'+$id+'.stderr')
    $all=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $deploy 'independent_install.ps1'))+$Arguments
    # No command shell or script evaluation. Every argument is one quoted token;
    # this private call rejects quotes, line breaks, NUL and trailing backslashes.
    $quoted=@(foreach($arg in $all){Check ($arg -notmatch '["\x00\r\n]' -and -not $arg.EndsWith('\')) 'Invalid private CPU CLI argument';'"'+$arg+'"'})
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$shell;$start.Arguments=$quoted -join ' ';$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.WorkingDirectory=$work;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.EnvironmentVariables.Remove('PSModulePath')
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    $outStream=[IO.FileStream]::new($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $errStream=[IO.FileStream]::new($err,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{
        Check $process.Start() 'Reviewed PowerShell child did not start'
        # Copy raw pipes concurrently; PowerShell redirection would decode/reencode.
        $outTask=$process.StandardOutput.BaseStream.CopyToAsync($outStream);$errTask=$process.StandardError.BaseStream.CopyToAsync($errStream)
        $process.WaitForExit();[void]$outTask.GetAwaiter().GetResult();[void]$errTask.GetAwaiter().GetResult();$code=$process.ExitCode
        $outStream.Flush($true);$errStream.Flush($true)
    }finally{$outStream.Dispose();$errStream.Dispose();$process.Dispose()}
    $raw=[IO.File]::ReadAllBytes($out);$errorBytes=[IO.File]::ReadAllBytes($err)
    $text=[Text.UTF8Encoding]::new($false,$true).GetString($raw)
    $row=@{Arguments=$Arguments;ExitCode=$code;Stdout=$out;Stderr=$err;StdoutSHA256=(Hash $out);StderrSHA256=(Hash $err);StdoutBytes=$raw.Length;StderrBytes=$errorBytes.Length;PowerShellExecutable=$shell}
    $cliReceipts.Add($row)
    if($code -eq 0){
        Check ($raw.Length -gt 1 -and $raw[0] -eq 123 -and $raw[$raw.Length-1] -eq 10 -and $raw[$raw.Length-2] -ne 13 -and $errorBytes.Length -eq 0) 'Successful stdout is not BOM-free JSON with one LF and empty stderr'
        $row.Data=[Installer033.StrictJsonV1]::Parse($text)
        Check ($row.Data -is [Collections.IDictionary]) 'Expected one complete JSON object'
    }else{Check ($raw.Length -eq 0 -and $errorBytes.Length -gt 0) 'Failed CLI emitted a report or omitted the error'}
    return $row
}
function PublicArguments($Fixture,[string]$Action){
    $values=@('-Action',$Action,'-PackageRoot',$Fixture.Package,'-ManifestSHA256',(Hash $Fixture.ManifestPath),'-RequirementsSHA256',$Fixture.RequirementsSHA256)
    if($Action -eq 'Plan'){$values+=@('-GameExe',$Fixture.Exe,'-LedgerPath',$Fixture.LedgerPath,'-LedgerSHA256',(Hash $Fixture.LedgerPath),'-SettingsRoot',$Fixture.Settings,'-VaultRoot',$Fixture.Vault)}
    return $values
}
Case 'public Validate and Plan entries remain read-only and report execution unavailable' {
    $f=NewFixture
    $validation=InvokeFixtureIndependentCli (PublicArguments $f 'Validate')
    Check ($validation.ExitCode -eq 0 -and -not $validation.Data.ProductionExecutionAvailable) 'Validation report implies production execution'
    $plan=InvokeFixtureIndependentCli (PublicArguments $f 'Plan')
    Check ($plan.ExitCode -eq 0 -and -not $plan.Data.Ready -and $plan.Data.Mode -eq 'plan-only') 'Public plan became executable';ExactBefore $f
}
function MakeReadonlyBefore($Fixture){
    $path=Join-Path $Fixture.Game 'old-owned.cfg'
    [IO.File]::SetLastWriteTimeUtc($path,[DateTime]::new(2024,2,3,4,5,6,[DateTimeKind]::Utc))
    [IO.File]::SetAttributes($path,[IO.FileAttributes]::ReadOnly)
    $Fixture.Before['old-owned.cfg']=Get-033MigrationSnapshot $Fixture.Game 'old-owned.cfg'
}
function InterruptRestoreContent(){
    $f=NewFixture;MakeReadonlyBefore $f;$p=Plan $f;$result=Execute $f $p
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-restore-content' -and $Path -eq 'old-owned.cfg'){throw 'INERT RESTORE CONTENT GAP'}}
    try{MustFail {Restore $f $result.Receipt} 'INERT RESTORE CONTENT GAP'}finally{$script:stepHook=$null}
    $state=Read-033StrictJson $result.Receipt;$file=@($state.Files|Where-Object {$_.Path -eq 'old-owned.cfg'})[0]
    $snapshot=Get-033RestoreSnapshot $f.Game 'old-owned.cfg'
    Check (Test-033OwnRestoreGap $file $snapshot) 'Interrupted content has no exact staged identity proof'
    Check (-not (Test-033RestoreBefore $file $snapshot)) 'Test did not interrupt before final metadata'
    return @{Fixture=$f;Receipt=$result.Receipt;File=$file;Snapshot=$snapshot}
}
Case 'original frozen Restore reproduces the content-before-metadata interruption defect' {
    $f=NewFixture;MakeReadonlyBefore $f;$p=Plan $f;$result=Execute $f $p
    $script:beforeGapWriter=(Get-Command Set-033TransactionFile -CommandType Function).ScriptBlock
    function script:Set-033TransactionFile([string]$Target,[string]$Source,[string]$ExpectedHash){
        & $script:beforeGapWriter $Target $Source $ExpectedHash
        if([IO.Path]::GetFileName($Target) -eq 'old-owned.cfg' -and $Source -and $Source.EndsWith('.migration-before')){throw 'INERT LEGACY CONTENT GAP'}
    }
    try{MustFail {Restore-033IndependentBeforeGap $result.Receipt (Hash $result.Receipt) $f.Context} 'INERT LEGACY CONTENT GAP'}
    finally{Set-Item -LiteralPath 'Function:script:Set-033TransactionFile' -Value $script:beforeGapWriter}
    $state=Read-033StrictJson $result.Receipt;$file=@($state.Files|Where-Object {$_.Path -eq 'old-owned.cfg'})[0]
    $snapshot=Get-033RestoreSnapshot $f.Game 'old-owned.cfg'
    Check ($snapshot.SHA256 -ieq $file.MigrationBefore.SHA256 -and -not (Test-033RestoreBefore $file $snapshot)) 'Legacy interruption did not reproduce a real metadata gap'
    MustFail {Restore-033IndependentBeforeGap $result.Receipt (Hash $result.Receipt) $f.Context} 'metadata changed after planning'
    # Old receipts have no staged identity proof; the new implementation must
    # also refuse to guess whether their mismatched metadata was externally edited.
    MustFail {Restore $f $result.Receipt} 'metadata changed after planning'
}
Case 'content-restored journal resumes only its exact staged file and restores readonly metadata' {
    $gap=InterruptRestoreContent;$f=$gap.Fixture;$identity=$gap.Snapshot.Identity
    Restore $f $gap.Receipt|Out-Null;ExactBefore $f
    Check ([Installer033.FileIdentityV1]::Identity((Join-Path $f.Game 'old-owned.cfg')) -ceq $identity) 'Metadata completion replaced the proven restored file'
    Restore $f $gap.Receipt|Out-Null;ExactBefore $f
}
Case 'same before bytes and timestamp on a different file ID cannot impersonate an interrupted restore' {
    $gap=InterruptRestoreContent;$f=$gap.Fixture;$target=Join-Path $f.Game 'old-owned.cfg';$replacement=Join-Path $f.Game 'external-replacement.tmp'
    [IO.File]::Copy($target,$replacement,$false);[IO.File]::SetAttributes($replacement,[IO.FileAttributes]$gap.Snapshot.Attributes)
    [IO.File]::SetLastWriteTimeUtc($replacement,(ConvertTo-033UtcTime $gap.Snapshot.LastWriteUtc));[IO.File]::Replace($replacement,$target,[NullString]::Value)
    $external=Get-033RestoreSnapshot $f.Game 'old-owned.cfg';Check ($external.Identity -cne $gap.Snapshot.Identity) 'External replacement kept the staged identity'
    MustFail {Restore $f $gap.Receipt} 'metadata changed after planning'
    Check (Test-033RestoreSnapshot (Get-033RestoreSnapshot $f.Game 'old-owned.cfg') $external) 'External replacement was overwritten'
}
Case 'same-identity external timestamp or attribute edits are not repaired as a restore gap' {
    foreach($change in @('time','attributes')){
        $gap=InterruptRestoreContent;$f=$gap.Fixture;$target=Join-Path $f.Game 'old-owned.cfg'
        if($change -eq 'time'){[IO.File]::SetLastWriteTimeUtc($target,(ConvertTo-033UtcTime $gap.Snapshot.LastWriteUtc).AddSeconds(3))}
        else{[IO.File]::SetAttributes($target,([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Archive))}
        $external=Get-033RestoreSnapshot $f.Game 'old-owned.cfg';Check ($external.Identity -ceq $gap.Snapshot.Identity) 'Metadata edit unexpectedly changed file identity'
        MustFail {Restore $f $gap.Receipt} 'metadata changed after planning'
        Check (Test-033RestoreSnapshot (Get-033RestoreSnapshot $f.Game 'old-owned.cfg') $external) 'External metadata was overwritten'
    }
}
Case 'prepared restore resumes after its journaled readonly-target transition' {
    $f=NewFixture;$legacyPath=Join-Path (Split-Path -Parent $f.LedgerPath) 'legacy/receipt.json';$legacy=Read-033StrictJson $legacyPath
    foreach($file in $legacy.Files){if($file.Path -eq 'old-replaced.dll'){$file.BeforeAttributes=[int][IO.FileAttributes]::ReadOnly}}
    Write-033Json $legacyPath $legacy;$ledger=Read-033StrictJson $f.LedgerPath
    foreach($item in $ledger.ownedTargets){$item.receipt.sha256=Hash $legacyPath};Write-033Json $f.LedgerPath $ledger
    $p=Plan $f;$result=Execute $f $p
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-restore-target-writable' -and $Path -eq 'old-replaced.dll'){throw 'INERT RESTORE WRITABLE GAP'}}
    try{MustFail {Restore $f $result.Receipt} 'INERT RESTORE WRITABLE GAP'}finally{$script:stepHook=$null}
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
}
Case 'persistent settings lock keeps its volume and file ID through busy-lock failure migration and undo' {
    $f=NewFixture;$p=Plan $f;$path=Join-Path $f.Settings 'settings.ini.033lock';$identity=[Installer033.FileIdentityV1]::Identity($path)
    $guard=[Installer033.SettingsLockV1]::new($path)
    try{MustFail {Execute $f $p} 'lock is busy';Check ([Installer033.FileIdentityV1]::Identity($path) -ceq $identity) 'Busy lock identity changed'}finally{$guard.Dispose()}
    $result=Execute $f $p;Check ([Installer033.FileIdentityV1]::Identity($path) -ceq $identity) 'Migration replaced the persistent lock'
    Restore $f $result.Receipt|Out-Null;ExactBefore $f
    Check ([Installer033.FileIdentityV1]::Identity($path) -ceq $identity) 'Undo replaced the persistent lock'
}
Case 'same-content external metadata or identity change after restore preflight is preserved' {
    foreach($change in @('time','identity')){
        $f=NewFixture;$p=Plan $f;$result=Execute $f $p
        $changedPath=if($change -eq 'time'){'old-replaced.dll'}else{'d3d11.dll'}
        $script:externalAtRestore=$null
        $script:stepHook={param($Stage,$Receipt,$Path)
            if($Stage -eq 'before-restore' -and $Path -eq $changedPath){
                $target=Join-Path $f.Game $Path;$before=Get-033RestoreSnapshot $f.Game $Path
                if($change -eq 'time'){[IO.File]::SetLastWriteTimeUtc($target,(ConvertTo-033UtcTime $before.LastWriteUtc).AddSeconds(3))}
                else{
                    $replacement=Join-Path $f.Game 'external-after-preflight.tmp'
                    [IO.File]::Copy($target,$replacement,$false)
                    [IO.File]::SetAttributes($replacement,[IO.FileAttributes]$before.Attributes)
                    [IO.File]::SetLastWriteTimeUtc($replacement,(ConvertTo-033UtcTime $before.LastWriteUtc))
                    [IO.File]::Replace($replacement,$target,[NullString]::Value)
                }
                $script:externalAtRestore=Get-033RestoreSnapshot $f.Game $Path
                Check ($script:externalAtRestore.SHA256 -ieq $before.SHA256) 'External mutation changed the intended identical content'
                if($change -eq 'identity'){Check ($script:externalAtRestore.Identity -cne $before.Identity) 'External replacement kept the preflight file ID'}
                else{Check ($script:externalAtRestore.Identity -ceq $before.Identity) 'Timestamp change unexpectedly replaced the file'}
            }
        }
        try{MustFail {Restore $f $result.Receipt} 'External modification after restore preflight'}finally{$script:stepHook=$null}
        Check ($null -ne $script:externalAtRestore) 'Before-restore external change was not exercised'
        Check (Test-033RestoreSnapshot (Get-033RestoreSnapshot $f.Game $changedPath) $script:externalAtRestore) 'External file or metadata was overwritten after preflight'
    }
}
function CheckPrivatePath([string]$Path){
    $full=[IO.Path]::GetFullPath($Path)
    Check ($full.StartsWith($expected+'\',[StringComparison]::OrdinalIgnoreCase)) 'Binding fixture operation escaped the reviewed workspace'
    [void](Get-033Path $expected $full.Substring($expected.Length+1))
}
function MovePrivate([string]$Source,[string]$Destination){
    CheckPrivatePath $Source;CheckPrivatePath $Destination
    if([IO.Directory]::Exists($Source)){[IO.Directory]::Move($Source,$Destination)}else{[IO.File]::Move($Source,$Destination)}
}
function SwapTargetIdentity($Fixture,[string]$Kind){
    Assert-033FixtureContext $Fixture.Context @{game=$Fixture.Game;'shared-settings'=$Fixture.Settings} $Fixture.Vault
    $before=Get-033TargetObservation $Fixture.Exe
    if($Kind -eq 'file'){
        $replacement=Join-Path $Fixture.Game 'same-bytes-new-id.tmp';CheckPrivatePath $replacement;CheckPrivatePath $Fixture.Exe
        [IO.File]::Copy($Fixture.Exe,$replacement,$false)
        [IO.File]::SetAttributes($replacement,[IO.FileAttributes]$before.Attributes)
        [IO.File]::SetLastWriteTimeUtc($replacement,(ConvertTo-033UtcTime $before.LastWriteUtc))
        [IO.File]::Replace($replacement,$Fixture.Exe,[NullString]::Value)
    }else{
        $prior=Join-Path $Fixture.Base 'prior-game-directory';MovePrivate $Fixture.Game $prior
        CheckPrivatePath $Fixture.Game;[void][IO.Directory]::CreateDirectory($Fixture.Game)
        foreach($item in (Get-ChildItem -LiteralPath $prior -Force)){MovePrivate $item.FullName (Join-Path $Fixture.Game $item.Name)}
    }
    $after=Get-033TargetObservation $Fixture.Exe
    Check ($after.ExeSHA256 -ceq $before.ExeSHA256 -and $after.LastWriteUtc -ceq $before.LastWriteUtc -and $after.Attributes -eq $before.Attributes) 'Identity counterexample changed bytes or metadata'
    if($Kind -eq 'file'){Check ($after.FileIdentity -cne $before.FileIdentity -and $after.DirectoryIdentity -ceq $before.DirectoryIdentity) 'File replacement did not isolate file identity'}
    else{Check ($after.FileIdentity -ceq $before.FileIdentity -and $after.DirectoryIdentity -cne $before.DirectoryIdentity) 'Ordinary directory swap did not isolate directory identity'}
}
$script:leaseProbeEvidence=[Collections.Generic.List[object]]::new()
function SharingDenied([scriptblock]$Body,[int[]]$AllowedCodes=@(32,33),[string]$Kind='exe-write-denied',[string]$Target){
    $denied=$false
    try{& $Body}catch{
        $cause=$_.Exception.GetBaseException();$errorCode=$cause.HResult -band 0xffff
        Check (($cause -is [IO.IOException] -or $cause -is [UnauthorizedAccessException]) -and $errorCode -in $AllowedCodes) ('Unexpected probe failure '+$errorCode+': '+$_.Exception.Message)
        $script:leaseProbeEvidence.Add(@{Kind=$Kind;Target=$Target;NativeErrorCode=$errorCode;ExceptionType=$cause.GetType().FullName});$denied=$true
    }
    Check $denied 'Target lease did not block the competing operation'
}
function CheckTargetHeld($Fixture){
    SharingDenied -Target $Fixture.Exe -Body {$writer=[IO.FileStream]::new($Fixture.Exe,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite);$writer.Dispose()}
    $probe=Join-Path $Fixture.Base 'lease-rename-probe'
    # Directory.Move maps a retained directory handle to ACCESS_DENIED on this
    # host. Only this directory probe accepts 5; unlocked rename is controlled
    # before and after the transaction/restore. EXE writes still require 32/33.
    try{SharingDenied -AllowedCodes @(5,32,33) -Kind 'directory-rename-denied' -Target $Fixture.Game -Body {MovePrivate $Fixture.Game $probe}}finally{if([IO.Directory]::Exists($probe)){MovePrivate $probe $Fixture.Game}}
}
function CheckTargetReleased($Fixture){
    $writer=[IO.FileStream]::new($Fixture.Exe,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite);$writer.Dispose()
    $probe=Join-Path $Fixture.Base 'lease-release-probe';MovePrivate $Fixture.Game $probe;MovePrivate $probe $Fixture.Game
    $script:leaseProbeEvidence.Add(@{Kind='released-positive-control';Target=$Fixture.Game;FileWritable=$true;DirectoryRenameSucceeded=$true})
}
function FixtureBytes($Fixture){
    $rows=@(Get-ChildItem -LiteralPath $Fixture.Base -File -Recurse -Force|Sort-Object FullName|ForEach-Object {
        @{Path=$_.FullName;Identity=[Installer033.FileIdentityV1]::Identity($_.FullName);SHA256=(Hash $_.FullName);Attributes=[int]$_.Attributes;LastWriteUtc=$_.LastWriteTimeUtc.ToString('o')}
    })
    $directories=@(Get-ChildItem -LiteralPath $Fixture.Base -Directory -Recurse -Force|Sort-Object FullName|ForEach-Object {$_.FullName})
    return (@{Files=$rows;Directories=$directories}|ConvertTo-Json -Depth 5 -Compress)
}
Case 'selected same-byte replacement EXE is rejected before any vault or target write' {
    $f=NewFixture;$p=Plan $f;SwapTargetIdentity $f 'file';$before=FixtureBytes $f
    MustFail {Execute $f $p} 'Target observation changed: FileIdentity'
    Check ((FixtureBytes $f) -ceq $before -and @(Get-ChildItem -LiteralPath $f.Vault -Force).Count -eq 0) 'Identity rejection wrote files'
    CheckTargetReleased $f
}
Case 'ordinary directory swap with the same EXE identity is rejected before vault preparation' {
    $f=NewFixture;$p=Plan $f;SwapTargetIdentity $f 'directory';$before=FixtureBytes $f
    MustFail {Execute $f $p} 'Target observation changed: DirectoryIdentity'
    Check ((FixtureBytes $f) -ceq $before -and @(Get-ChildItem -LiteralPath $f.Vault -Force).Count -eq 0) 'Directory rejection wrote files'
    CheckTargetReleased $f
}
Case 'transaction pins EXE and directory through writes and standalone restore then releases both' {
    $f=NewFixture;$p=Plan $f;CheckTargetReleased $f;$script:heldStages=@{}
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -in @('all-archives-durable','before-write','before-restore')){CheckTargetHeld $f;$script:heldStages[$Stage]=$true}}
    try{$result=Execute $f $p;CheckTargetReleased $f;Restore $f $result.Receipt|Out-Null}finally{$script:stepHook=$null}
    foreach($stage in @('all-archives-durable','before-write','before-restore')){Check $script:heldStages.ContainsKey($stage) ('Missing lease boundary: '+$stage)}
    CheckTargetReleased $f;ExactBefore $f
    $state=Read-033StrictJson $result.Receipt
    Check ($state.TargetBindingVersion -eq 1 -and $state.PlanSHA256 -ceq $p.SHA256 -and $state.GameExeSHA256 -ceq $p.Data.GameExeSHA256) 'Receipt did not preserve the pinned plan binding'
    Assert-033TargetObservation $state.TargetObservation $p.Data.TargetEvidence.Observation
}
Case 'automatic failure recovery retains target lease and releases it after exact undo' {
    $f=NewFixture;$p=Plan $f;CheckTargetReleased $f;$script:restoreLeaseSeen=$false
    $script:stepHook={param($Stage,$Receipt,$Path)
        if($Stage -eq 'after-write' -and $Path -eq '033-runtime/033-runtime.dll'){CheckTargetHeld $f;throw 'INERT BOUND WRITE FAILURE'}
        if($Stage -eq 'before-restore'){CheckTargetHeld $f;$script:restoreLeaseSeen=$true}
    }
    try{MustFail {Execute $f $p} 'migration-before state restored'}finally{$script:stepHook=$null}
    Check $script:restoreLeaseSeen 'Automatic recovery did not exercise the held lease';CheckTargetReleased $f;ExactBefore $f
}
Case 'blocked automatic recovery releases target lease and later exact recovery remains possible' {
    $f=NewFixture;$p=Plan $f;CheckTargetReleased $f
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-write' -and $Path -eq '033-runtime/033-runtime.dll'){CheckTargetHeld $f;$f.Context.PendingSavesDrained=$false}}
    try{MustFail {Execute $f $p} 'Recovery blocked'}finally{$script:stepHook=$null}
    CheckTargetReleased $f;$f.Context.PendingSavesDrained=$true
    $receipt=@(Get-ChildItem -LiteralPath $f.Vault -Filter receipt.json -Recurse)[0].FullName
    Restore $f $receipt|Out-Null;CheckTargetReleased $f;ExactBefore $f
}
Case 'standalone restore rejects a post-install replacement EXE without touching receipt or targets' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;SwapTargetIdentity $f 'file';$before=FixtureBytes $f
    MustFail {Restore $f $result.Receipt} 'Target observation changed: FileIdentity'
    Check ((FixtureBytes $f) -ceq $before) 'Wrong-EXE restore wrote receipt or targets';CheckTargetReleased $f
}
Case 'standalone restore rejects a post-install ordinary directory swap with zero writes' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;SwapTargetIdentity $f 'directory';$before=FixtureBytes $f
    MustFail {Restore $f $result.Receipt} 'Target observation changed: DirectoryIdentity'
    Check ((FixtureBytes $f) -ceq $before) 'Wrong-directory restore wrote receipt or targets';CheckTargetReleased $f
}
Case 'interrupted standalone restore releases its lease and reacquires it for exact continuation' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;CheckTargetReleased $f
    $script:stepHook={param($Stage,$Receipt,$Path) if($Stage -eq 'after-restore'){CheckTargetHeld $f;throw 'INERT BOUND RESTORE INTERRUPTION'}}
    try{MustFail {Restore $f $result.Receipt} 'INERT BOUND RESTORE INTERRUPTION'}finally{$script:stepHook=$null}
    CheckTargetReleased $f;Restore $f $result.Receipt|Out-Null;CheckTargetReleased $f;ExactBefore $f
    $again=Restore $f $result.Receipt;Check ($again.TargetWrites -eq 0) 'Already-restored path did not use its no-target-write return';CheckTargetReleased $f
}
Case 'busy settings lock and no-change execution release target leases on early paths' {
    $f=NewFixture;$p=Plan $f;$guard=[Installer033.SettingsLockV1]::new((Join-Path $f.Settings 'settings.ini.033lock'))
    try{MustFail {Execute $f $p} 'lock is busy';CheckTargetReleased $f}finally{$guard.Dispose()}
    $receipt=@(Get-ChildItem -LiteralPath $f.Vault -Filter receipt.json -Recurse)[0].FullName
    Restore $f $receipt|Out-Null;CheckTargetReleased $f;ExactBefore $f
    $f=NewFixture;$p=Plan $f;$data=Read-033StrictJson $p.Path;$data.Actions=@();Write-033Json $p.Path $data;$p.SHA256=Hash $p.Path
    $result=Execute $f $p;Check ($result.Status -eq 'no-changes') 'Empty reviewed action set did not take the no-change path';CheckTargetReleased $f;ExactBefore $f
}
Case 'new restore path refuses historical unbound receipts without inventing an identity' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;$state=Read-033StrictJson $result.Receipt
    [void]$state.Remove('TargetBindingVersion');[void]$state.Remove('TargetObservation');[void]$state.Remove('GameExeSHA256');Write-033Json $result.Receipt $state
    $before=FixtureBytes $f;MustFail {Restore $f $result.Receipt} 'Unbound historical receipt'
    Check ((FixtureBytes $f) -ceq $before) 'Historical receipt was upgraded or changed';CheckTargetReleased $f
}
Case 'archived plan drift and detached receipt identity are rejected before restore writes' {
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;[IO.File]::AppendAllText((Join-Path (Split-Path -Parent $result.Receipt) 'plan.json'),' ')
    $before=FixtureBytes $f;MustFail {Restore $f $result.Receipt} 'Archived plan SHA256 mismatch';Check ((FixtureBytes $f) -ceq $before) 'Changed archived plan was consumed'
    $f=NewFixture;$p=Plan $f;$result=Execute $f $p;$state=Read-033StrictJson $result.Receipt;$state.TargetObservation.FileIdentity='detached';Write-033Json $result.Receipt $state
    $before=FixtureBytes $f;MustFail {Restore $f $result.Receipt} 'Target observation changed: FileIdentity';Check ((FixtureBytes $f) -ceq $before) 'Detached receipt identity was consumed'
}
Case 'plan game root and selected observation must refer to the same physical target' {
    $f=NewFixture;$p=Plan $f;$data=Read-033StrictJson $p.Path;$data.TargetEvidence.Observation.Directory=$f.Settings;Write-033Json $p.Path $data;$p.SHA256=Hash $p.Path
    $before=FixtureBytes $f;MustFail {Execute $f $p} 'not bound to the planned EXE and game root';Check ((FixtureBytes $f) -ceq $before) 'Detached plan wrote files'
}
Case 'public selected plan remains non-executable even when caller changes only ready flags' {
    $f=NewFixture;$p=Plan $f -Production;Check (-not $p.Data.Ready -and $p.Data.Mode -eq 'plan-only') 'Public selected plan gained fixture permission'
    $data=Read-033StrictJson $p.Path;$data.Ready=$true;$data.Blockers=@();Write-033Json $p.Path $data;$p.SHA256=Hash $p.Path
    $before=FixtureBytes $f;MustFail {Execute $f $p} 'ready offline fixture';Check ((FixtureBytes $f) -ceq $before) 'Public plan crossed the fixture mode gate'
}
Case 'public stdout preserves Chinese paths and a caller can save the exact native bytes' {
    # Build the existing fixture under a private Unicode/space parent before any
    # ownership records are produced, so all recorded absolute paths are real.
    $savedWork=$work;$unicodeLeaf=[string][char]0x4e2d+[char]0x6587+' '+[char]0x7a7a+[char]0x683c
    try{$work=Get-033IndependentPath $savedWork $unicodeLeaf;[void][IO.Directory]::CreateDirectory($work);$f=NewFixture}finally{$work=$savedWork}
    Check ((Split-Path -Leaf (Split-Path -Parent $f.Base)) -ceq $unicodeLeaf) 'Expected explicit Unicode fixture parent was not used'
    $before=FixtureBytes $f
    $validation=InvokeFixtureIndependentCli (PublicArguments $f 'Validate')
    $plan=InvokeFixtureIndependentCli (PublicArguments $f 'Plan')
    Check ($validation.ExitCode -eq 0 -and $plan.ExitCode -eq 0) 'Unicode public call failed'
    Check ($plan.Data.GameExe -ceq $f.Exe -and $plan.Data.Roots.game -ceq $f.Game -and $plan.Data.ManifestPath -ceq $f.ManifestPath) 'Native UTF8 stdout corrupted explicit Chinese paths'
    $saved=Get-033IndependentPath $work 'caller-saved-public-plan.json'
    [IO.File]::Copy($plan.Stdout,$saved)
    Check ((Hash $saved) -ceq $plan.StdoutSHA256 -and (Read-033StrictJson $saved).GameExe -ceq $f.Exe) 'Caller changed saved stdout bytes'
    Check ((FixtureBytes $f) -ceq $before) 'Public call wrote into an input root'
}
Case 'removed public OutputPath cannot overwrite inputs or create an external report' {
    $f=NewFixture;$external=Join-Path $f.Base 'existing-report.json';Put $external 'caller-owned original evidence'
    $before=FixtureBytes $f
    $outputs=@($external,$f.ManifestPath,$f.LedgerPath,(Join-Path $f.Game '_033-integrated.json'),(Join-Path $f.Settings 'settings.ini.033lock'),(Join-Path $f.Vault 'receipt.json'),(Join-Path $f.Base 'missing-report.json'))
    foreach($output in $outputs){
        $row=InvokeFixtureIndependentCli ((PublicArguments $f 'Validate')+@('-OutputPath',$output))
        Check ($row.ExitCode -ne 0) 'Removed output parameter silently accepted'
        Check ((FixtureBytes $f) -ceq $before) 'Removed output parameter changed an input or created a report'
    }
}
Case 'public validation and planning failures exit nonzero without a JSON report' {
    $f=NewFixture;$before=FixtureBytes $f
    $bad=PublicArguments $f 'Validate';$index=[Array]::IndexOf($bad,'-ManifestSHA256');$bad[$index+1]='0'*64
    $row=InvokeFixtureIndependentCli $bad;Check ($row.ExitCode -ne 0) 'Invalid manifest hash accepted'
    $missing=PublicArguments $f 'Validate';$missing[1]='Plan'
    $row=InvokeFixtureIndependentCli $missing;Check ($row.ExitCode -ne 0) 'Plan without explicit target inputs accepted'
    $bad=PublicArguments $f 'Plan';$index=[Array]::IndexOf($bad,'-LedgerSHA256');$bad[$index+1]='0'*64
    $row=InvokeFixtureIndependentCli $bad;Check ($row.ExitCode -ne 0) 'Invalid ownership hash accepted'
    Check ((FixtureBytes $f) -ceq $before) 'Rejected public call wrote into input roots'
}
Case 'public stdout interface does not expose an installation or fixture execution action' {
    $f=NewFixture;$before=FixtureBytes $f
    foreach($action in @('Install','Restore')){
        $arguments=PublicArguments $f 'Validate';$arguments[1]=$action
        $row=InvokeFixtureIndependentCli $arguments;Check ($row.ExitCode -ne 0) 'Unsupported execution action accepted'
    }
    $row=InvokeFixtureIndependentCli ((PublicArguments $f 'Plan')+@('-FixtureContext','forbidden'))
    Check ($row.ExitCode -ne 0 -and (FixtureBytes $f) -ceq $before) 'Public call exposed a fixture execution path'
}
Write-033Json (Join-Path $work 'public-cli-receipts.json') @{Calls=$cliReceipts.ToArray();ProductionExecutionAvailable=$false;NativeRuntimeExecuted=$false}
Write-033Json (Join-Path $work 'target-lease-probes.json') @{Probes=$script:leaseProbeEvidence.ToArray();NativeGameSdkGpuExecution=$false;ProductionDeployment=$false}
$summary=@{PowerShell=$PSVersionTable.PSVersion.ToString();Passed=@($results|Where-Object {$_.Passed}).Count;Failed=@($results|Where-Object {-not $_.Passed}).Count;Fixtures=$work;Results=$results.ToArray();NativeGameSdkGpuExecution=$false;ProductionDeployment=$false}
Write-033Json (Join-Path $OutputDirectory 'independent-installer-results.json') $summary
Write-Output ('Independent installer: '+$summary.Passed+' passed, '+$summary.Failed+' failed')
if($summary.Failed){throw 'Independent installer fixture failures; see independent-installer-results.json'}
