param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# Reviewed offline fixtures only. These PE files have no code/entry point and
# must never be executed or installed in a game. All output stays under build.
$ErrorActionPreference='Stop'
$deploy=Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy'
foreach($file in (Get-ChildItem -LiteralPath $deploy -Filter '*.ps1')){
    $tokens=$null;$errors=$null;[void][Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw "Parse failed: $($file.Name): $errors"}
}
. (Join-Path $deploy 'deployment_transaction.ps1')
. (Join-Path $deploy 'package_tools.ps1')
. (Join-Path $deploy 'route_manager.ps1')
$results=[Collections.Generic.List[object]]::new()
$fixtureRoot=Join-Path $OutputDirectory ('f-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
function Verify($Ok,[string]$Message){if(-not $Ok){throw $Message}}
function Case([string]$Name,[scriptblock]$Body){
    try{& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output "PASS $Name"}
    catch{$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message;Position=$_.ScriptStackTrace});Write-Output "FAIL $Name : $($_.Exception.Message)"}
}
function MustFail([scriptblock]$Body,[string]$Pattern){$message=$null;try{& $Body|Out-Null}catch{$message=$_.Exception.Message};Verify ($message -and $message -match $Pattern) "Expected failure /$Pattern/, got: $message"}
function Folder { $path=Join-Path $fixtureRoot ([Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $path|Out-Null;return $path }
function Put([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Pe([string]$Path,[string]$Arch='x64',[string[]]$Imports=@(),[string[]]$Delay=@(),[switch]$Dll,[switch]$DelayVA,[byte]$Tag=0){
    $b=[byte[]]::new(4096)
    function U16($At,$Value){[BitConverter]::GetBytes([uint16]$Value).CopyTo($b,$At)}
    function U32($At,$Value){[BitConverter]::GetBytes([uint32]$Value).CopyTo($b,$At)}
    U16 0 0x5a4d;U32 60 128;U32 128 0x4550;U16 134 1;U16 150 $(if($Dll){0x2002}else{2})
    $opt=152
    if($Arch -eq 'x86'){U16 132 0x14c;U16 148 224;U16 $opt 0x10b;U32 ($opt+28) 4194304;$dir=$opt+96;$section=$opt+224}
    else{U16 132 $(if($Arch -eq 'arm64'){0xaa64}else{0x8664});U16 148 240;U16 $opt 0x20b;[BitConverter]::GetBytes([uint64]0x140000000).CopyTo($b,$opt+24);$dir=$opt+112;$section=$opt+240}
    U32 ($opt+60) 512;U32 ($dir-4) 16;U32 ($section+12) 4096;U32 ($section+16) 3584;U32 ($section+20) 512
    $nameAt=1536
    if($Imports.Count){U32 ($dir+8) 4096;U32 ($dir+12) (($Imports.Count+1)*20)}
    for($i=0;$i -lt $Imports.Count;$i++){
        U32 (512+$i*20+12) (4096+$nameAt-512);$s=[Text.Encoding]::ASCII.GetBytes($Imports[$i]+[char]0);$s.CopyTo($b,$nameAt);$nameAt+=$s.Length
    }
    if($Delay.Count){U32 ($dir+13*8) 4608;U32 ($dir+13*8+4) (($Delay.Count+1)*32)}
    for($i=0;$i -lt $Delay.Count;$i++){
        U32 (1024+$i*32) $(if($DelayVA){0}else{1})
        U32 (1024+$i*32+4) (4096+$nameAt-512+$(if($DelayVA){4194304}else{0}))
        $s=[Text.Encoding]::ASCII.GetBytes($Delay[$i]+[char]0);$s.CopyTo($b,$nameAt);$nameAt+=$s.Length
    }
    $b[4000]=$Tag;[IO.File]::WriteAllBytes($Path,$b)
}
function RouteFixture {
    $root=Folder;$game=Join-Path $root 'game';$cache=Join-Path $root 'cache'
    New-Item -ItemType Directory -Path $game,$cache|Out-Null
    $exe=Join-Path $game 'fixture.exe';Pe $exe -Delay @('d3d12.dll')
    foreach($name in @('dlss5-033.cfg','dlss5-033.state')){Put (Join-Path $game $name) "user=unchanged; F11; native6; universal2/3; layers3; counter7"}
    Pe (Join-Path $cache 'client.dll') -Dll -Tag 1;Pe (Join-Path $cache 'worker.exe') -Tag 2
    Pe (Join-Path $cache 'opti.dll') -Dll -Tag 3;Put (Join-Path $cache 'default.ini') "[User]`r`nValue=default`r`n";Put (Join-Path $cache 'LICENSE.txt') 'Offline fixture license only'
    $components=@()
    foreach($id in @('client','worker','opti')){
        $name=if($id -eq 'worker'){'worker.exe'}else{$id+'.dll'}
        $components+=@(@{Id=$id;Version='fixture-1';Source='inert CPU fixture';Protocol='fixture-abi-1';License=@{Path='LICENSE.txt';SHA256=(Get-033FileHash (Join-Path $cache 'LICENSE.txt'))};Files=@(@{Path=$name;SHA256=(Get-033FileHash (Join-Path $cache $name));Kind=$(if($id -eq 'worker'){'exe'}else{'dll'});Architecture='x64'},@{Path='default.ini';SHA256=(Get-033FileHash (Join-Path $cache 'default.ini'));Kind='data';Architecture=$null})})
    }
    $combos=@(@{Id='N';Route='native';ExeArchitecture='x64';Apis=@('dx12');Ready=$true;ControlContract='033-managed-v1';Components=@();ProtocolLinks=@();Payload=@();Configs=@()})
    foreach($pair in @(@('A','feeder','client','client.dll'),@('B','optiscaler','opti','opti.dll'))){
        $combos+=@(@{Id=$pair[0];Route=$pair[1];ExeArchitecture='x64';Apis=@('dx12');Ready=$true;ControlContract='033-managed-v1';Components=@($pair[2],'worker');ProtocolLinks=@(@{Client=$pair[2];Worker='worker';Protocol='fixture-abi-1'});Payload=@(@{Component=$pair[2];Path=$pair[3];Target='winmm.dll';AllowedBeforeHashes=@()},@{Component='worker';Path='worker.exe';Target='033-worker64.exe';AllowedBeforeHashes=@()});Configs=@(@{Component=$pair[2];Path='default.ini';Target='ReShade.ini'})})
    }
    $cat=Join-Path $root 'catalog.json';Write-033Json $cat @{Version=1;Components=$components;Combinations=$combos}
    @{Game=$game;Exe=$exe;Cache=$cache;Catalog=$cat;Pin=(Get-033FileHash $cat)}
}
function SwitchRoute($F,[string]$Route,[switch]$Plan){Invoke-033RouteSwitch -GameExe $F.Exe -Catalog $F.Catalog -CatalogSHA256 $F.Pin -CacheRoot $F.Cache -Combination $Route -PlanOnly:$Plan}
Case 'PE32/PE32+ normal imports and RVA/VA delay imports' {
    $root=Folder
    foreach($arch in @('x86','x64')){
        $path=Join-Path $root ($arch+'.exe');Pe $path $arch -Imports @('winmm.dll') -Delay @('D3D11.dll')
        $info=Get-033PeInfo $path;Verify ($info.Status -eq 'valid' -and $info.Architecture -eq $arch -and $info.DelayImports -contains 'd3d11.dll' -and $info.Imports -contains 'winmm.dll') 'Import parsing failed'
    }
    $path=Join-Path $root 'va.exe';Pe $path 'x86' -Delay @('d3d9.dll') -DelayVA
    Verify ((Get-033PeInfo $path).DelayImports -contains 'd3d9.dll') 'Delay VA parsing failed'
    MustFail {Get-033Imports $path} 'AMD64'
}
Case 'malformed/truncated PE cannot select an executable' {
    $root=Folder;$path=Join-Path $root 'bad.exe';Pe $path -Delay @('d3d12.dll')
    $b=[IO.File]::ReadAllBytes($path);$b[369]=255;[IO.File]::WriteAllBytes($path,$b)
    Verify ((Get-033PeInfo $path).Status -eq 'unknown') 'Corrupt directory accepted'
    [IO.File]::WriteAllBytes($path,$b[0..39]);Verify ((Get-033PeInfo $path).Status -eq 'unknown') 'Truncated DOS header accepted'
    Verify (-not (Get-033GameInventory $root).SelectedExe) 'Invalid executable selected'
}
Case 'launcher, local renderer DLL and nested SR versions stay distinct from FG' {
    $root=Folder;Pe (Join-Path $root 'launcher.exe');Pe (Join-Path $root 'game.exe') -Imports @('render.dll')
    Pe (Join-Path $root 'render.dll') -Dll -Delay @('vulkan-1.dll')
    foreach($sub in @('a','b')){New-Item -ItemType Directory -Path (Join-Path $root $sub)|Out-Null;Pe (Join-Path $root ($sub+'\nvngx_dlss.dll')) -Dll -Tag ([byte][char]$sub)}
    $scan=Get-033GameInventory $root
    Verify ($scan.SelectedExe -eq (Join-Path $root 'game.exe') -and $scan.Candidates[0].Apis -contains 'vulkan') 'Local renderer evidence not found'
    Verify ($scan.SRFiles.Count -eq 2 -and $scan.FGFiles.Count -eq 0 -and $scan.NativeFG -eq 'unverified') 'SR files falsely prove FG or newest loaded DLL'
    Pe (Join-Path $root 'render.dll') 'x86' -Dll -Imports @('d3d9.dll')
    Verify (-not (Get-033GameInventory $root).SelectedExe) 'Wrong-bitness renderer selected'
}
Case 'ambiguous executables, unknown API and scan bounds do not auto-select' {
    $root=Folder;Pe (Join-Path $root 'one.exe') -Imports @('d3d11.dll');Pe (Join-Path $root 'two.exe') -Delay @('d3d12.dll')
    Verify (-not (Get-033GameInventory $root).SelectedExe) 'Ambiguous game selection'
    $scan=Get-033GameInventory $root -MaxEntries 1;Verify (-not $scan.Complete -and -not $scan.SelectedExe) 'Truncated scan auto-selected'
    $root2=Folder;Pe (Join-Path $root2 'unknown.exe');Verify (-not (Get-033GameInventory $root2).SelectedExe) 'Unknown API selected'
}
Case 'query failure remains unknown while every nested DLL lock is checked' {
    $root=Folder;New-Item -ItemType Directory -Path (Join-Path $root 'nested')|Out-Null
    Pe (Join-Path $root 'game.exe');$dll=Join-Path $root 'nested\render.dll';Pe $dll -Dll
    function Get-033ProcessSnapshot {throw 'fixture query failure'}
    $handle=[IO.File]::Open($dll,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try{$state=Get-033Occupancy $root;Verify ($state.Status -eq 'unknown' -and $state.Running.Count -eq 0 -and $state.Files.Count -eq 2 -and @($state.Files|Where-Object {$_.State -eq 'in-use'}).Count -eq 1) 'Query failure bypassed nested DLL or reported running'}finally{$handle.Dispose()}
}
Case 'known running process differs from unknown worker ownership' {
    $root=Folder;Pe (Join-Path $root 'game.exe')
    function Get-033ProcessSnapshot {@{ProcessId=0;Name='033-worker64.exe';ExecutablePath='C:\elsewhere\033-worker64.exe'}}
    Verify ((Get-033Occupancy $root).Status -eq 'unknown') 'Foreign helper misreported as this game running'
    function Get-033ProcessSnapshot {@{ProcessId=0;Name='game.exe';ExecutablePath=(Join-Path $root 'game.exe')}}
    Verify ((Get-033Occupancy $root).Status -eq 'running') 'Known process not detected'
}
Case 'fixed catalogue rejects bad pin, corrupt component, architecture and protocol' {
    $f=RouteFixture;[void](Test-033ComponentCatalog $f.Catalog $f.Pin $f.Cache 'A')
    MustFail {Test-033ComponentCatalog $f.Catalog ('0'*64) $f.Cache 'A'} 'SHA256'
    $data=Get-Content $f.Catalog -Raw|ConvertFrom-Json;$data.Components[0].Files[0].Architecture='x86';Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    MustFail {Test-033ComponentCatalog $f.Catalog $f.Pin $f.Cache 'A'} 'architecture'
    $data.Components[0].Files[0].Architecture='x64';$data.Components[1].Protocol='wrong';Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    MustFail {Test-033ComponentCatalog $f.Catalog $f.Pin $f.Cache 'A'} 'protocol'
    Put (Join-Path $f.Cache 'client.dll') 'same name wrong DLL'
    MustFail {Test-033ComponentCatalog $f.Catalog $f.Pin $f.Cache 'A'} 'corrupt'
}
Case 'missing license and unreviewed combination are rejected offline' {
    $f=RouteFixture;$data=Get-Content $f.Catalog -Raw|ConvertFrom-Json;$data.Combinations[1].Ready=$false;Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    MustFail {SwitchRoute $f 'A'} 'review'
    MustFail {SwitchRoute $f 'unknown'} 'listed'
    $f=RouteFixture;Put (Join-Path $f.Cache 'LICENSE.txt') 'changed license'
    MustFail {SwitchRoute $f 'A'} 'License'
}
Case 'route plan is read-only and SR alone cannot satisfy an API/FG gate' {
    $f=RouteFixture;$plan=SwitchRoute $f 'A' -Plan
    Verify ($plan.Status -eq 'planned' -and -not (Test-Path (Join-Path $f.Game '_033routes'))) 'Planning modified target'
    Pe $f.Exe -Imports @('dxgi.dll');Pe (Join-Path $f.Game 'nvngx_dlss.dll') -Dll
    MustFail {SwitchRoute $f 'A'} 'API'
}
Case 'A-B-A, repeated installs and native restore retain per-route and shared settings' {
    $f=RouteFixture;function Get-033ProcessSnapshot {@()}
    $cfg=Get-033FileHash (Join-Path $f.Game 'dlss5-033.cfg');$counter=Get-033FileHash (Join-Path $f.Game 'dlss5-033.state')
    $first=SwitchRoute $f 'A';Put (Join-Path $f.Game 'ReShade.ini') "[User]`r`nValue=A user edit`r`n";$a=Get-033FileHash (Join-Path $f.Game 'ReShade.ini')
    [void](SwitchRoute $f 'B');Put (Join-Path $f.Game 'ReShade.ini') "[User]`r`nValue=B user edit`r`n";$b=Get-033FileHash (Join-Path $f.Game 'ReShade.ini')
    [void](SwitchRoute $f 'A');Verify ((Get-033FileHash (Join-Path $f.Game 'ReShade.ini')) -eq $a) 'A config not restored'
    [void](SwitchRoute $f 'A');Verify ((Get-033FileHash (Join-Path $f.Game 'ReShade.ini')) -eq $a) 'Repeat reset config'
    [void](SwitchRoute $f 'B');Verify ((Get-033FileHash (Join-Path $f.Game 'ReShade.ini')) -eq $b) 'B config not restored'
    [void](SwitchRoute $f 'N');Verify (-not(Test-Path (Join-Path $f.Game 'winmm.dll')) -and -not(Test-Path (Join-Path $f.Game '033-worker64.exe'))) 'Original absence not restored'
    [void](SwitchRoute $f 'A');Verify ((Get-033FileHash (Join-Path $f.Game 'ReShade.ini')) -eq $a) 'Profile lost across native route'
    Verify ((Get-033FileHash (Join-Path $f.Game 'dlss5-033.cfg')) -eq $cfg -and (Get-033FileHash (Join-Path $f.Game 'dlss5-033.state')) -eq $counter) 'Shared settings/counter reset'
}
Case 'external binary and saved-profile modification block route changes' {
    $f=RouteFixture;function Get-033ProcessSnapshot {@()};[void](SwitchRoute $f 'A');$state=Get-033FileHash (Join-Path $f.Game '_033routes\active.json')
    Put (Join-Path $f.Game 'winmm.dll') 'external'
    MustFail {SwitchRoute $f 'B'} 'External binary';Verify ((Get-033FileHash (Join-Path $f.Game '_033routes\active.json')) -eq $state) 'State advanced on conflict'
    $f=RouteFixture;[void](SwitchRoute $f 'A');[void](SwitchRoute $f 'B')
    $saved=Get-Content (Join-Path $f.Game '_033routes\active.json') -Raw|ConvertFrom-Json
    Put (Get-033Path $f.Game $saved.Profiles[0].Blob) 'damaged saved profile'
    MustFail {SwitchRoute $f 'A'} 'Saved route bytes'
}
Case 'all replace/delete/create write boundaries restore exact bytes and attributes' {
    foreach($phase in @('before-write','after-write','after-record')){foreach($failPath in @('old.dll','gone.dll','new.dll')){
        $root=Folder;$source=Join-Path $root 'payload.bin';Put $source 'after';Put (Join-Path $root 'old.dll') 'before';Put (Join-Path $root 'gone.dll') 'delete me'
        $stamp=[DateTime]::SpecifyKind([DateTime]'2020-01-02T03:04:05',[DateTimeKind]::Utc);[IO.File]::SetLastWriteTimeUtc((Join-Path $root 'old.dll'),$stamp);[IO.File]::SetAttributes((Join-Path $root 'old.dll'),[IO.FileAttributes]::ReadOnly)
        function Invoke-033TransactionStep($Stage,$Receipt,$Path){if($Stage -eq $phase -and $Path -eq $failPath){throw 'injected write failure'}}
        MustFail {Invoke-033Transaction $root @(@{Path='old.dll';Source=$source},@{Path='gone.dll';Source=$null},@{Path='new.dll';Source=$source})} 'exact files restored'
        Verify ([IO.File]::ReadAllText((Join-Path $root 'old.dll')) -eq 'before' -and [IO.File]::ReadAllText((Join-Path $root 'gone.dll')) -eq 'delete me' -and -not(Test-Path (Join-Path $root 'new.dll'))) 'Write-boundary rollback differs'
        Verify ([IO.File]::GetLastWriteTimeUtc((Join-Path $root 'old.dll')) -eq $stamp -and ([IO.File]::GetAttributes((Join-Path $root 'old.dll')) -band [IO.FileAttributes]::ReadOnly)) 'Original metadata lost'
    }}
}
Case 'interrupted rollback resumes without forcing external changes' {
    $root=Folder;$source=Join-Path $root 'payload.bin';Put $source 'after';foreach($name in @('a.dll','b.dll')){Put (Join-Path $root $name) $name}
    $receipt=Invoke-033Transaction $root @(@{Path='a.dll';Source=$source},@{Path='b.dll';Source=$source})
    function Invoke-033TransactionStep($Stage,$Receipt,$Path){if($Stage -eq 'after-restore' -and $Path -eq 'a.dll'){throw 'restore interrupted'}}
    MustFail {Restore-033Transaction $receipt} 'interrupted'
    MustFail {Invoke-033Transaction $root @(@{Path='c.dll';Source=$source})} 'Unfinished'
    Put (Join-Path $root 'b.dll') 'external after interruption';MustFail {Restore-033Transaction $receipt -Recover} 'External modification'
    Put (Join-Path $root 'b.dll') 'after';function Invoke-033TransactionStep($Stage,$Receipt,$Path){}
    [void](Restore-033Transaction $receipt -Recover);[void](Restore-033Transaction $receipt -Recover)
    Verify ([IO.File]::ReadAllText((Join-Path $root 'a.dll')) -eq 'a.dll' -and [IO.File]::ReadAllText((Join-Path $root 'b.dll')) -eq 'b.dll') 'Retry did not restore originals'
}
Case 'legacy recovery keeps conflict protection and unsafe paths are rejected' {
    $root=Folder;$source=Join-Path $root 'payload.bin';Put $source 'after';Put (Join-Path $root 'a.dll') 'before'
    $receipt=Invoke-033Transaction $root @(@{Path='a.dll';Source=$source});$state=Get-Content $receipt -Raw|ConvertFrom-Json;$state.Version=1;$state.Status='prepared';Write-033Json $receipt $state
    Put (Join-Path $root 'a.dll') 'external';MustFail {Restore-033Transaction $receipt -Recover} 'External modification'
    Put (Join-Path $root 'a.dll') 'after';[void](Restore-033Transaction $receipt -Recover)
    foreach($path in @('..\x','x:stream','x.','CON.dll','.\x','a\\b')){MustFail {Get-033Path $root $path} 'Invalid'}
}
Case 'x86 client plus x64 worker is accepted only as a coherent reviewed pair' {
    $f=RouteFixture;Pe $f.Exe 'x86' -Imports @('d3d9.dll');Pe (Join-Path $f.Cache 'client.dll') 'x86' -Dll
    $data=Get-Content $f.Catalog -Raw|ConvertFrom-Json
    $data.Components[0].Files[0].Architecture='x86';$data.Components[0].Files[0].SHA256=Get-033FileHash (Join-Path $f.Cache 'client.dll')
    $data.Combinations[1].ExeArchitecture='x86';$data.Combinations[1].Apis=@('dx9');Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    Verify ((SwitchRoute $f 'A' -Plan).Status -eq 'planned') 'Reviewed x86/64 bridge pair rejected'
    $data.Combinations[1].ExeArchitecture='x64';Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    MustFail {SwitchRoute $f 'A' -Plan} 'architecture'
}
Case 'route switch failure restores both selected backend and edited configuration' {
    $f=RouteFixture;function Get-033ProcessSnapshot {@()};[void](SwitchRoute $f 'A')
    Put (Join-Path $f.Game 'ReShade.ini') "[User]`r`nValue=keep edited A`r`n"
    $before=@{};foreach($p in @('winmm.dll','ReShade.ini','_033routes\active.json')){$before[$p]=Get-033FileHash (Get-033Path $f.Game $p)}
    function Invoke-033TransactionStep($Stage,$Receipt,$Path){if($Stage -eq 'after-write' -and $Path -eq 'winmm.dll'){throw 'switch interrupted'}}
    MustFail {SwitchRoute $f 'B'} 'exact files restored'
    foreach($p in $before.Keys){Verify ((Get-033FileHash (Get-033Path $f.Game $p)) -eq $before[$p]) "Failed switch changed $p"}
}
Case 'external state, unowned proxy and game compiler cannot be overwritten' {
    $f=RouteFixture;Put (Join-Path $f.Game 'winmm.dll') 'unowned proxy';MustFail {SwitchRoute $f 'A'} 'Unowned'
    $f=RouteFixture;function Get-033ProcessSnapshot {@()};[void](SwitchRoute $f 'A')
    Put (Join-Path $f.Game '_033routes\active.json') '{}';MustFail {SwitchRoute $f 'B'} 'transaction hash'
    $f=RouteFixture;$data=Get-Content $f.Catalog -Raw|ConvertFrom-Json;$data.Combinations[1].Payload[0].Target='d3dcompiler_47.dll';Write-033Json $f.Catalog $data;$f.Pin=Get-033FileHash $f.Catalog
    MustFail {SwitchRoute $f 'A'} 'Protected'
}
Case 'common entry retains native-input package dispatch and DLL validation compatibility' {
    $f=RouteFixture;function Get-033ProcessSnapshot {@()}
    $record=@{Version=1;GameExe=$f.Exe;CoreMount='winmm.dll';CoreHash=$null;PackageId='fixture-old'}
    Put (Join-Path $f.Game 'winmm.dll') 'owned core';$record.CoreHash=Get-033FileHash (Join-Path $f.Game 'winmm.dll');Write-033Json (Join-Path $f.Game '_033-integrated.json') $record
    Put (Join-Path $f.Cache 'notice.txt') 'inert native input fixture';Write-033Json (Join-Path $f.Cache 'input.json') @{Schema=1;DefaultRoute='existing-interface-auto';Adapters=@()}
    $files=@();foreach($name in @('client.dll','opti.dll','notice.txt','input.json')){$files+=@(@{Path=$name;SHA256=(Get-033FileHash (Join-Path $f.Cache $name))})}
    Write-033Json (Join-Path $f.Cache 'native-input-package.json') @{Schema=1;Kind='033-native-input-upgrade';PackageId='fixture';Files=$files;Core=$files[0];SceneAdapter=$files[1];Notice=$files[2];Catalogue=$files[3]}
    $plan=& (Join-Path $deploy 'integrated_install.ps1') -Action Plan -GameExe $f.Exe -PackageRoot $f.Cache|ConvertFrom-Json
    Verify ($plan.Status -eq 'preserve-existing-interface' -and -not $plan.SettingsChanged) 'Native-input dispatch lost'
    Pe $f.Exe -Dll;MustFail {& (Join-Path $deploy 'integrated_install.ps1') -Action Plan -GameExe $f.Exe -PackageRoot $f.Cache} 'game executable'
}
Case 'source and target changes after a pinned plan cause no target writes' {
    $root=Folder;$source=Join-Path $root 'payload.bin';Put $source 'after';$target=Join-Path $root 'target.dll';Put $target 'original'
    MustFail {Invoke-033Transaction $root @(@{Path='target.dll';Source=$source;ExpectedBeforeHash=(Get-033FileHash $target);ExpectedAfterHash=('0'*64)})} 'pinned plan hash'
    MustFail {Invoke-033Transaction $root @(@{Path='target.dll';Source=$source;ExpectedBeforeHash=$null;ExpectedAfterHash=(Get-033FileHash $source)})} 'changed after planning'
    Verify ([IO.File]::ReadAllText($target) -eq 'original') 'Pinned-plan rejection changed target'
}
Case 'offline cache is hash-addressed, idempotent and exactly recoverable' {
    $f=RouteFixture;$cache=Folder
    $saved=Import-033ComponentCache $f.Catalog $f.Pin $f.Cache $cache 'A'
    Verify ($saved.Status -eq 'verified' -and $saved.Root.EndsWith($f.Pin.ToLowerInvariant())) 'Cache key is not the reviewed catalogue hash'
    $repeat=Import-033ComponentCache $f.Catalog $f.Pin $f.Cache $cache 'A';Verify (-not $repeat.Receipt) 'Repeat import rewrote identical cache'
    Put (Join-Path $saved.Root 'client.dll') 'corrupt offline cache'
    MustFail {Import-033ComponentCache $f.Catalog $f.Pin $f.Cache $cache 'A'} 'corrupt'
    [IO.File]::Copy((Join-Path $f.Cache 'client.dll'),(Join-Path $saved.Root 'client.dll'),$true)
    [void](Restore-033Transaction $saved.Receipt)
    Verify (-not(Test-Path (Join-Path $saved.Root 'client.dll')) -and -not(Test-Path (Join-Path $saved.Root '033-components.json'))) 'Cache exact restore failed'
}
$summary=[ordered]@{Shell=$PSVersionTable.PSVersion.ToString();Passed=@($results|Where-Object {$_.Passed}).Count;Failed=@($results|Where-Object {-not $_.Passed}).Count;Fixtures=$fixtureRoot;Results=$results.ToArray();GPUExecution=$false;GameExecution=$false}
Write-033Json (Join-Path $OutputDirectory 'swapper-results.json') $summary
Write-Output "Swapper installer: $($summary.Passed) passed, $($summary.Failed) failed"
if($summary.Failed){throw 'Swapper installer regressions; see swapper-results.json'}
