param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'deploy/deployment_transaction.ps1')
. (Join-Path $root 'deploy/package_tools.ps1')
$work=Join-Path $OutputDirectory ('native-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$game=Join-Path $work 'game';$package=Join-Path $work 'package'
New-Item -ItemType Directory -Path $game,$package | Out-Null
$checks=0
function Check($Ok,[string]$Message){$script:checks++;if(-not $Ok){throw "FAIL: $Message"}}
function Pe([string]$Path,[byte]$Tag){
    # Minimal PE32+ byte fixture for read-only import parsing. Never executable
    # test code; there is no entry point, imported function or code section.
    $b=[byte[]]::new(1024);$b[0]=0x4d;$b[1]=0x5a
    [BitConverter]::GetBytes([int]128).CopyTo($b,60)
    [BitConverter]::GetBytes([int]0x4550).CopyTo($b,128)
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($b,132)
    [BitConverter]::GetBytes([uint16]240).CopyTo($b,148)
    [BitConverter]::GetBytes([uint16]0x20b).CopyTo($b,152)
    # Valid bounded section metadata for the stricter read-only PE inspector.
    [BitConverter]::GetBytes([uint16]1).CopyTo($b,134)
    foreach($pair in @(@(212,512),@(260,16),@(400,512),@(404,4096),@(408,512),@(412,512))){[BitConverter]::GetBytes([uint32]$pair[1]).CopyTo($b,$pair[0])}
    $b[1000]=$Tag;[IO.File]::WriteAllBytes($Path,$b)
}
Pe (Join-Path $game 're4.exe') 1;Pe (Join-Path $game 'version.dll') 2
Pe (Join-Path $game 'dinput8.dll') 3;Pe (Join-Path $package 'core.dll') 4;Pe (Join-Path $package 'scene.dll') 5
[IO.File]::WriteAllText((Join-Path $package 'notice.txt'),'license-preserved')
$coreBefore=(Get-FileHash -LiteralPath (Join-Path $game 'version.dll')).Hash
$sceneBefore=(Get-FileHash -LiteralPath (Join-Path $game 'dinput8.dll')).Hash
$catalogue=Get-Content -LiteralPath (Join-Path $root 'deploy/native_input_catalog.json') -Raw -Encoding UTF8|ConvertFrom-Json
$catalogue.Adapters[0].AllowedCoreBefore=@($coreBefore);$catalogue.Adapters[0].AllowedAdapterBefore=@($sceneBefore)
Write-033Json (Join-Path $package 'catalogue.json') $catalogue
function Entry([string]$Name){return @{Path=$Name;SHA256=(Get-FileHash -LiteralPath (Join-Path $package $Name)).Hash}}
$manifest=@{Schema=1;Kind='033-native-input-upgrade';PackageId='cpu-fixture';Core=(Entry 'core.dll');SceneAdapter=(Entry 'scene.dll');Notice=(Entry 'notice.txt');Catalogue=(Entry 'catalogue.json');AllowedNoticeBefore=@();Files=@()}
$manifest.Files=@($manifest.Core,$manifest.SceneAdapter,$manifest.Notice,$manifest.Catalogue)
Write-033Json (Join-Path $package 'native-input-package.json') $manifest
$exe=Join-Path $game 're4.exe'
Write-033Json (Join-Path $game '_033-integrated.json') @{Version=1;PackageId='before';CoreMount='version.dll';CoreHash=$coreBefore;GameExe=$exe;ComponentHashes=@(@{Path='version.dll';After=$coreBefore},@{Path='dinput8.dll';After=$sceneBefore})}
$settings=@{'dlss5-033.cfg'="inject=1`r`npasses=3`r`nimagefg=1`r`nimagefgmult=3`r`nhotkey=122`r`n";'dlss5-033.state'='2';'ReShade.ini'="[RenoDX.MFGUnlock]`nForceMultiplier=6`n";'OptiScaler.ini'='user choices';'local_config.ini'='game graphics'}
foreach($name in $settings.Keys){[IO.File]::WriteAllText((Join-Path $game $name),$settings[$name])}
$before=@{};Get-ChildItem -LiteralPath $game -File|ForEach-Object {$before[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
$installer=Join-Path $root 'deploy/integrated_install.ps1'
$p1=& $installer -Action Plan -GameExe $exe -PackageRoot $package|ConvertFrom-Json
$p2=& $installer -Action Plan -GameExe $exe -PackageRoot $package|ConvertFrom-Json
Check ($p1.Route -eq 'semantic-scene-auto' -and $p1.CoreMount -eq 'version.dll' -and $p1.AutomaticSelection) 'automatically selects scene adapter and preserves non-winmm mount'
Check (($p1|ConvertTo-Json -Depth 8) -eq ($p2|ConvertTo-Json -Depth 8)) 'repeated plan is deterministic'
foreach($name in $before.Keys){Check ((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -eq $before[$name]) 'plan has no side effects'}
$install=& $installer -Action Install -GameExe $exe -PackageRoot $package|ConvertFrom-Json
Check ($install.Status -eq 'installed-awaiting-manual-validation' -and -not $install.GameAccepted) 'installation is distinct from game acceptance'
foreach($name in $settings.Keys){Check ((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -eq $before[$name]) 'settings, F11, sixfold and crash count preserved byte-for-byte'}
Check (-not(Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'no heuristic remount or extra engine'
$repeat=& $installer -Action Install -GameExe $exe -PackageRoot $package|ConvertFrom-Json
Check ($repeat.Status -eq 'already-installed') 'repeat upgrade is idempotent'
& $installer -Action Restore -Receipt $install.Receipt | Out-Null
foreach($name in $before.Keys){Check ((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -eq $before[$name]) 'exact rollback restores original bytes'}
Check (-not(Test-Path -LiteralPath (Join-Path $game '033-native-input-NOTICE.txt'))) 'rollback restores original absence'

# Unknown games retain actual common-interface handling; a copied library or a
# filename does not establish runtime native inputs and does not enable NR/FG.
$unknown=Join-Path $game 'unknown.exe';Pe $unknown 6
$rec=Get-Content -LiteralPath (Join-Path $game '_033-integrated.json') -Raw|ConvertFrom-Json
$rec.GameExe=$unknown;Write-033Json (Join-Path $game '_033-integrated.json') $rec
$result=& $installer -Action Install -GameExe $unknown -PackageRoot $package|ConvertFrom-Json
Check ($result.Status -eq 'preserve-existing-interface' -and $result.Targets.Count -eq 10 -and $result.EntryAssets -eq 'installed' -and -not $result.GameAccepted) 'unknown game receives only common entries, not a guessed scene adapter'
Check ('version.dll' -notin $result.Targets -and 'dinput8.dll' -notin $result.Targets -and '_033-integrated.json' -notin $result.Targets) 'entry-only update excludes native payload and receipt'
foreach($name in $settings.Keys){Check ((Get-FileHash -LiteralPath (Join-Path $game $name)).Hash -eq $before[$name]) 'unknown-game settings preserved'}
$rec.GameExe=$exe;Write-033Json (Join-Path $game '_033-integrated.json') $rec
Pe (Join-Path $game 'dinput8.dll') 88
$blocked=$false;try{& $installer -Action Install -GameExe $exe -PackageRoot $package|Out-Null}catch{$blocked=$true}
Check $blocked 'unreviewed REFramework bytes rejected before mutation'
Pe (Join-Path $game 'dinput8.dll') 3
$catalogue.Adapters=@($catalogue.Adapters[0],$catalogue.Adapters[0]);Write-033Json (Join-Path $package 'catalogue.json') $catalogue
$manifest.Catalogue=Entry 'catalogue.json';$manifest.Files=@($manifest.Core,$manifest.SceneAdapter,$manifest.Notice,$manifest.Catalogue)
Write-033Json (Join-Path $package 'native-input-package.json') $manifest
$blocked=$false;try{& $installer -Action Plan -GameExe $exe -PackageRoot $package|Out-Null}catch{$blocked=$true}
Check $blocked 'conflicting catalogue rules rejected'
[IO.File]::AppendAllText((Join-Path $package 'core.dll'),'corrupt')
$blocked=$false;try{& $installer -Action Plan -GameExe $exe -PackageRoot $package|Out-Null}catch{$blocked=$true}
Check $blocked 'changed package bytes rejected'
"Native input installer CPU: $checks checks, 0 failures; mock PE files only, no DLL/game execution"
Write-033Json (Join-Path $OutputDirectory 'native-input-results.json') @{PowerShell=$PSVersionTable.PSVersion.ToString();Checks=$checks;Failed=0;Fixtures=$work;GameOrGPUExecution=$false}
