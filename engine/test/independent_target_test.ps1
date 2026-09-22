param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskEngine=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$expected=Join-Path $taskEngine 'build/parallel-InstallerTargetCpu'
if([IO.Path]::GetFullPath($OutputDirectory) -ine $expected){throw 'Use only the reviewed InstallerTargetCpu output'}
for($cursor=$expected;$cursor;$cursor=[IO.Path]::GetDirectoryName($cursor)){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Fixture output is indirect'}}}
$taskRepo=Split-Path -Parent $taskEngine
$taskInputPath=Join-Path $PSScriptRoot 'independent_target_inputs.json'
$taskInputs=Get-Content -LiteralPath $taskInputPath -Raw -Encoding UTF8|ConvertFrom-Json
foreach($row in $taskInputs.files){$source=[IO.Path]::GetFullPath((Join-Path $taskRepo $row.path));if(-not $source.StartsWith($taskEngine+'\',[StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ine $row.sha256){throw ('Pinned target source changed: '+$row.path)}}
if(@($taskInputs.files).Count -ne 13){throw 'Incomplete target input closure'}
$deploy=Join-Path $taskEngine 'deploy'
. (Join-Path $deploy 'independent_target.ps1')
. (Join-Path $PSScriptRoot 'fixtures/independent-target/builders.ps1')
$work=Join-Path $expected ('fixtures-'+[Guid]::NewGuid().ToString('N'));[void][IO.Directory]::CreateDirectory($work)
$reports=Join-Path $work 'reports';[void][IO.Directory]::CreateDirectory($reports)
$results=[Collections.Generic.List[object]]::new()
function Check($Value,[string]$Message){if(-not $Value){throw $Message}}
function Reject([scriptblock]$Body,[string]$Pattern='*'){
    $rejected=$false;try{& $Body|Out-Null}catch{$rejected=$true;Check ($_.Exception.Message -like $Pattern) ('Unexpected error: '+$_.Exception.Message)}
    Check $rejected ('Expected rejection: '+$Pattern)
}
function Case([string]$Name,[scriptblock]$Body){
    try{& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output ('PASS '+$Name)}
    catch{$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message;Stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$Name+': '+$_.Exception.Message)}
}
function FixturePath([string]$Path){
    $full=[IO.Path]::GetFullPath($Path);Check ($full.StartsWith($work+'\',[StringComparison]::OrdinalIgnoreCase)) 'File operation escaped this new private fixture'
    [void](Get-033IndependentPath $work $full.Substring($work.Length+1));return $full
}
function MoveFixtureFile([string]$From,[string]$To){[IO.File]::Move((FixturePath $From),(FixturePath $To))}
function MoveFixtureDirectory([string]$From,[string]$To){[IO.Directory]::Move((FixturePath $From),(FixturePath $To))}
function Single([string[]]$Imports=@('d3d11.dll')){
    $folder=Join-Path $work ('target-'+[Guid]::NewGuid().ToString('N'));$exe=Join-Path $folder 'game.exe';Pe $exe 'x64' $false $Imports
    return @{Game=$folder;Exe=$exe}
}
function Selection($Fixture){
    $report=Get-033TargetSelection @($Fixture.Exe) $Fixture.Exe 'CPU fixture caller explicitly selected this target; no game execution'
    $path=Join-Path $reports ([Guid]::NewGuid().ToString('N')+'.json');Write-033Json $path $report
    return @{Path=$path;SHA256=(Hash $path);Data=$report}
}
function Bound($Fixture,$Selected){
    New-033TargetBoundPlan -TargetReportPath $Selected.Path -TargetReportSHA256 $Selected.SHA256 -PackageRoot $Fixture.Package -ManifestSHA256 (Hash $Fixture.ManifestPath) -RequirementsSHA256 $Fixture.RequirementsSHA256 -LedgerPath $Fixture.LedgerPath -LedgerSHA256 (Hash $Fixture.LedgerPath) -SettingsRoot $Fixture.Settings -VaultRoot $Fixture.Vault
}
function ReplaceImports($Fixture,[string[]]$Imports){
    Pe $Fixture.Exe 'x64' $false $Imports
    $ledger=Read-033StrictJson $Fixture.LedgerPath;$ledger.gameExeSHA256=Hash $Fixture.Exe
    foreach($item in $ledger.inventory){if($item.path -eq [IO.Path]::GetFileName($Fixture.Exe)){$item.sha256=Hash $Fixture.Exe}}
    Write-033Json $Fixture.LedgerPath $ledger
}
$cliReceipts=[Collections.Generic.List[object]]::new()
function InvokeFixtureTargetCli([string[]]$Arguments){
    $shell=Join-Path $PSHOME $(if($PSVersionTable.PSVersion.Major -eq 5){'powershell.exe'}else{'pwsh.exe'})
    Check ([IO.File]::Exists($shell)) 'Current reviewed PowerShell executable missing'
    $id=[Guid]::NewGuid().ToString('N');$out=FixturePath (Join-Path $reports ('cli-'+$id+'.stdout'));$err=FixturePath (Join-Path $reports ('cli-'+$id+'.stderr'))
    $all=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $deploy 'independent_target_plan.ps1'))+$Arguments
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
    $row=@{ExitCode=$code;Stdout=$out;Stderr=$err;StdoutSHA256=(Hash $out);StderrSHA256=(Hash $err);StdoutBytes=$raw.Length;StderrBytes=$errorBytes.Length;PowerShellExecutable=$shell}
    $cliReceipts.Add($row)
    if($code -eq 0){
        Check ($raw.Length -gt 1 -and $raw[0] -eq 123 -and $raw[$raw.Length-1] -eq 10 -and $raw[$raw.Length-2] -ne 13 -and $errorBytes.Length -eq 0) 'Successful stdout is not BOM-free JSON with one LF and empty stderr'
        $row.Data=[Installer033.StrictJsonV1]::Parse($text)
    }else{Check ($raw.Length -eq 0 -and $errorBytes.Length -gt 0) 'Failed CLI emitted a report or omitted the error'}
    return $row
}
Case 'one or several candidates never silently choose an executable' {
    $a=Single;$b=Single
    foreach($candidates in @(@($a.Exe),@($a.Exe,$b.Exe))){$r=Get-033TargetSelection $candidates '' '';Check (-not $r.SelectionAccepted -and $r.Issues[0].Code -eq 'target-not-selected') 'A target was guessed'}
    $r=Get-033TargetSelection @($a.Exe) $b.Exe 'explicit';Check (-not $r.SelectionAccepted -and $r.Issues[0].Code -eq 'selection-not-in-candidates') 'Unlisted target accepted'
}
Case 'explicit case-insensitive choice preserves uncertainty and every candidate' {
    $a=Single @('d3d11.dll','vulkan-1.dll');$b=Single
    $r=Get-033TargetSelection @($a.Exe,$b.Exe) $a.Exe.ToUpperInvariant() 'explicit fixture choice'
    Check ($r.SelectionAccepted -and $r.Candidates.Count -eq 2 -and $r.ActualApi -eq 'unknown' -and -not $r.ActualLoadingDirectoryVerified) 'Static hints became actual capability'
    $path=Join-Path $reports 'case-choice.json';Write-033Json $path $r;$read=Read-033SelectedTarget $path (Hash $path)
    Check ($read.Observation.StaticApiHints.Count -eq 2) 'Multiple hints were collapsed'
}
Case 'read lease pins the actual EXE and directory during observation only' {
    $f=Single;$before=Get-033TargetObservation $f.Exe;$lease=[Installer033.TargetReadLeaseV1]::new($f.Exe)
    try{Reject {[IO.File]::WriteAllText((FixturePath $f.Exe),'forbidden')};Reject {MoveFixtureDirectory $f.Game ($f.Game+'-moved')}}finally{$lease.Dispose()}
    Assert-033TargetObservation $before (Get-033TargetObservation $f.Exe)
    MoveFixtureDirectory $f.Game ($f.Game+'-moved');MoveFixtureDirectory ($f.Game+'-moved') $f.Game
    Check ([IO.File]::Exists($f.Exe)) 'Lease was not released'
}
Case 'identical bytes with a different file identity invalidate an old report' {
    $f=Single;$s=Selection $f;$before=Get-033TargetObservation $f.Exe;$old=$f.Exe+'.before'
    MoveFixtureFile $f.Exe $old;[IO.File]::Copy((FixturePath $old),(FixturePath $f.Exe));[IO.File]::SetLastWriteTimeUtc($f.Exe,[DateTime]::Parse($before.LastWriteUtc));[IO.File]::SetAttributes($f.Exe,[IO.FileAttributes]$before.Attributes)
    Check ((Hash $f.Exe) -eq $before.ExeSHA256) 'Same-byte fixture changed content'
    Reject {Read-033SelectedTarget $s.Path $s.SHA256} '*FileIdentity*'
}
Case 'a rebuilt directory at the same path invalidates even the same EXE identity' {
    $f=Single;$s=Selection $f;$before=Get-033TargetObservation $f.Exe;$old=$f.Game+'-before'
    MoveFixtureDirectory $f.Game $old;[void][IO.Directory]::CreateDirectory((FixturePath $f.Game));MoveFixtureFile (Join-Path $old 'game.exe') $f.Exe
    $after=Get-033TargetObservation $f.Exe;Check ($before.FileIdentity -eq $after.FileIdentity -and $before.DirectoryIdentity -ne $after.DirectoryIdentity) 'Directory identity fixture was not isolated'
    Reject {Read-033SelectedTarget $s.Path $s.SHA256} '*DirectoryIdentity*'
}
Case 'content attributes and write-time changes invalidate observed facts' {
    foreach($kind in @('bytes','attributes','time')){
        $f=Single;$s=Selection $f
        if($kind -eq 'bytes'){Pe $f.Exe 'x64' $false @('d3d11.dll') 9}
        elseif($kind -eq 'attributes'){[IO.File]::SetAttributes($f.Exe,[IO.FileAttributes]::ReadOnly)}
        else{[IO.File]::SetLastWriteTimeUtc($f.Exe,[DateTime]::UtcNow.AddDays(-1))}
        Reject {Read-033SelectedTarget $s.Path $s.SHA256} '*observation changed*'
    }
}
Case 'invalid occupied and duplicate targets produce explicit refusal' {
    $f=Single;$busy=[IO.FileStream]::new($f.Exe,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try{$r=Get-033TargetSelection @($f.Exe) $f.Exe 'explicit';Check (-not $r.SelectionAccepted -and $r.Candidates[0].Status -eq 'unreadable-or-invalid') 'Busy target omitted or accepted'}finally{$busy.Dispose()}
    Pe $f.Exe 'x64' $true @('d3d11.dll');$r=Get-033TargetSelection @($f.Exe) $f.Exe 'explicit';Check (-not $r.SelectionAccepted) 'DLL disguised as EXE accepted'
    Reject {Get-033TargetSelection @($f.Exe,$f.Exe.ToUpperInvariant()) '' ''} '*Duplicate*'
    Reject {Get-033TargetSelection @('relative.exe') '' ''} '*absolute*'
    $missing=Join-Path $f.Game 'missing.exe';$r=Get-033TargetSelection @($f.Exe,$missing) $missing 'explicit';Check ($r.Candidates.Count -eq 2 -and -not $r.SelectionAccepted) 'Missing target disappeared'
}
Case 'report byte hashes and claimed runtime truth are distinct gates' {
    $f=Single;$s=Selection $f
    Reject {Read-033SelectedTarget $s.Path ('0'*64)} '*hash mismatch*'
    $s.Data.ActualLoadingDirectoryVerified=$true;Write-033Json $s.Path $s.Data
    Reject {Read-033SelectedTarget $s.Path (Hash $s.Path)} '*cannot assert*'
    $s.Data.ActualLoadingDirectoryVerified=$false;$s.Data.Candidates+=@($s.Data.Candidates[0]);Write-033Json $s.Path $s.Data
    Reject {Read-033SelectedTarget $s.Path (Hash $s.Path)} '*duplicate candidate*'
}
Case 'real x64 and x86 package planning remains bound to one selected target and readonly' {
    foreach($arch in @('x64','x86')){
        $f=NewFixture $arch;$s=Selection $f;$p=Bound $f $s
        Check ($p.GameExe -eq $f.Exe -and $p.RouteId -eq $arch -and $p.Mode -eq 'plan-only' -and -not $p.Ready) 'Target-bound plan selected the wrong root or enabled execution'
        Check ($p.TargetEvidence.Observation.Architecture -eq $arch -and $p.RuntimeAcceptance.Loaded -eq 'unknown' -and $p.RuntimeAcceptance.FG -eq 'unknown') 'PE facts became loading evidence'
        foreach($name in $f.Before.Keys){
            $observed=Get-033MigrationSnapshot $f.Game $name
            foreach($field in @('Exists','SHA256','Attributes','LastWriteUtc')){Check ($observed[$field] -ceq $f.Before[$name][$field]) ('Read-only plan changed '+$name+' '+$field)}
        }
    }
}
Case 'unknown and multiple static API hints are not unselected or unsupported games' {
    foreach($imports in @(@('vulkan-1.dll'),@('d3d11.dll','vulkan-1.dll'))){
        $f=NewFixture;ReplaceImports $f $imports;$s=Selection $f;$p=Bound $f $s
        Check ($s.Data.SelectionAccepted -and $p.TargetEvidence.Observation.Path -eq $f.Exe) 'API uncertainty was confused with a missing target choice'
        Check ($p.RuntimeAcceptance.Compatibility -eq 'awaiting-loader-contract' -and $p.RuntimeAcceptance.ActualApi -eq 'unknown' -and -not $p.Ready) 'Unsupported/enabled capability was invented'
        Check ($p.RequiredNextSteps.Count -eq 3) 'Uncertainty has no actionable next step'
    }
}
Case 'Chinese and spaced paths round trip through raw UTF8 stdout and exact saved report SHA' {
    $unicodeFolder=-join ([char[]]@(0x4e2d,0x6587,0x20,0x7a7a,0x683c,0x8def,0x5f84));$reason=$unicodeFolder+' caller choice'
    $outerWork=$work;$work=Join-Path $outerWork $unicodeFolder;[void][IO.Directory]::CreateDirectory($work)
    try{$f=NewFixture}finally{$work=$outerWork}
    $selected=Join-Path $reports 'public-selected.json';$plan=Join-Path $reports 'public-plan.json'
    $before=@{};foreach($path in [IO.Directory]::GetFiles($f.Base,'*',[IO.SearchOption]::AllDirectories)){$before[$path]=Hash $path}
    $inspection=InvokeFixtureTargetCli @('-Action','Inspect','-CandidatePaths',$f.Exe,'-SelectedGameExe',$f.Exe,'-SelectionReason',$reason)
    Check ($inspection.ExitCode -eq 0 -and $inspection.Data['SelectionAccepted'] -and $inspection.Data['SelectedGameExe'] -ceq $f.Exe -and $inspection.Data['SelectionReason'] -ceq $reason) 'CLI damaged Unicode paths or selection reason'
    # Caller persists exact raw bytes, not a decoded/ToJson re-encoding.
    [IO.File]::Copy((FixturePath $inspection.Stdout),(FixturePath $selected));Check ((Hash $selected) -eq $inspection.StdoutSHA256) 'Saved report SHA differs from actual stdout bytes'
    $planning=InvokeFixtureTargetCli @('-Action','Plan','-TargetReportPath',$selected,'-TargetReportSHA256',(Hash $selected),'-PackageRoot',$f.Package,'-ManifestSHA256',(Hash $f.ManifestPath),'-RequirementsSHA256',$f.RequirementsSHA256,'-LedgerPath',$f.LedgerPath,'-LedgerSHA256',(Hash $f.LedgerPath),'-SettingsRoot',$f.Settings,'-VaultRoot',$f.Vault)
    Check ($planning.ExitCode -eq 0 -and -not $planning.Data['Ready'] -and $planning.Data['RuntimeAcceptance']['SR'] -eq 'unknown' -and $planning.Data['GameExe'] -ceq $f.Exe) 'Plan rejected exact saved bytes or invented actual acceptance'
    [IO.File]::Copy((FixturePath $planning.Stdout),(FixturePath $plan))
    $reencoded=Join-Path $reports 'same-json-different-bytes.json';$value=[IO.File]::ReadAllText($selected)
    [IO.File]::WriteAllText((FixturePath $reencoded),$value+' ',[Text.UTF8Encoding]::new($false))
    Check ((Hash $reencoded) -ne (Hash $selected)) 'Whitespace variant failed to change actual bytes'
    Reject {Read-033SelectedTarget $reencoded (Hash $selected)} '*hash mismatch*'
    [void](Read-033SelectedTarget $reencoded (Hash $reencoded))
    $after=@([IO.Directory]::GetFiles($f.Base,'*',[IO.SearchOption]::AllDirectories));Check ($after.Count -eq $before.Count) 'Read-only entry added or removed fixture files'
    foreach($path in $before.Keys){Check ((Hash $path) -eq $before[$path]) 'Read-only entry changed protected bytes'}
}
Case 'package-contained target evidence cannot supply its own target authority' {
    $f=NewFixture;$s=Selection $f;$inside=Join-Path $f.Package 'target.json';[IO.File]::Copy((FixturePath $s.Path),(FixturePath $inside));$s.Path=$inside;$s.SHA256=Hash $inside
    Reject {Bound $f $s} '*outside the package*'
}
Case 'removed OutputPath parameter cannot overwrite or create any evidence file' {
    $f=NewFixture;$other=Single;$s=Selection $f;$entry=Join-Path $deploy 'independent_target_plan.ps1';$kept=Join-Path $reports 'existing-evidence.json';Put $kept '{"keep":"original"}'
    foreach($output in @($f.LedgerPath,$other.Exe,$s.Path,$kept,(Join-Path $f.Game 'new.json'),(Join-Path $f.Settings 'new.json'),(Join-Path $f.Vault 'new.json'),(Join-Path $f.Package 'new.json'))){
        $before=Hash $output;Reject {& $entry -Action Inspect -CandidatePaths @($f.Exe) -OutputPath $output} '*OutputPath*';Check ((Hash $output) -eq $before) 'Unknown output argument modified an input or created a report'
    }
}
Case 'actual shell CLI has pure JSON success and nonzero empty-stdout errors' {
    $f=Single
    $ok=InvokeFixtureTargetCli @('-Action','Inspect','-CandidatePaths',$f.Exe,'-SelectedGameExe',$f.Exe,'-SelectionReason','CPU fixture explicit target')
    Check ($ok.ExitCode -eq 0 -and $ok.Data['SelectionAccepted']) 'Valid CLI failed'
    $missing=InvokeFixtureTargetCli @('-Action','Plan');Check ($missing.ExitCode -ne 0) 'Missing inputs returned success'
    $removed=InvokeFixtureTargetCli @('-Action','Inspect','-CandidatePaths',$f.Exe,'-OutputPath',(Join-Path $f.Game 'forbidden.json'));Check ($removed.ExitCode -ne 0) 'Removed parameter was silently accepted'
    Check (-not [IO.File]::Exists((Join-Path $f.Game 'forbidden.json'))) 'Removed parameter still caused public I/O'
}
Case 'launcher nested folders and equal basenames remain explicit distinct choices' {
    $base=Join-Path $work 'launcher-and-nested';$launcher=Join-Path $base 'launcher.exe';$game64=Join-Path $base 'bin/x64/game.exe';$game32=Join-Path $base 'bin/x86/game.exe'
    Pe $launcher 'x64' $false @();Pe $game64 'x64' $false @('d3d11.dll');Pe $game32 'x86' $false @('d3d9.dll')
    $candidates=@($launcher,$game64,$game32);$unselected=Get-033TargetSelection $candidates '' '';Check (-not $unselected.SelectionAccepted) 'Launcher or basename selected a game implicitly'
    foreach($path in @($game64,$game32)){
        $r=Get-033TargetSelection $candidates $path 'Caller explicitly chose this nested CPU fixture EXE'
        Check ($r.SelectionAccepted -and $r.SelectedGameExe -ceq $path -and $r.Candidates.Count -eq 3 -and -not $r.ActualLoadingDirectoryVerified) 'Nested choices collapsed or became actual loading proof'
    }
}
Case 'ordinary directory swaps invalidate a prior report without granting a write permit' {
    $a=Single;$b=Single;$selected=Selection $a;$before=Get-033TargetObservation $a.Exe;$old=$a.Game+'-original'
    MoveFixtureDirectory $a.Game $old;MoveFixtureDirectory $b.Game $a.Game
    Reject {Read-033SelectedTarget $selected.Path $selected.SHA256} '*observation changed*'
    $current=Get-033TargetObservation $a.Exe;Check ($current.DirectoryIdentity -ne $before.DirectoryIdentity) 'Directory-swap fixture did not change the real identity'
    Check ((Hash $selected.Path) -eq $selected.SHA256) 'Stale target rejection changed source report bytes'
}
Case 'drive-root report exclusion uses normalized separators without drive-root writes' {
    $f=NewFixture;$s=Selection $f;$drive=[IO.Path]::GetPathRoot($work)
    Reject {New-033TargetBoundPlan -TargetReportPath $s.Path -TargetReportSHA256 $s.SHA256 -PackageRoot $drive -ManifestSHA256 ('0'*64) -RequirementsSHA256 ('0'*64) -LedgerPath $f.LedgerPath -LedgerSHA256 (Hash $f.LedgerPath) -SettingsRoot $f.Settings -VaultRoot $f.Vault} '*outside the package*'
    Check ((Hash $s.Path) -eq $s.SHA256) 'Drive-root exclusion modified the source report'
}
foreach($row in $taskInputs.files){Check ((Get-FileHash -LiteralPath (Join-Path $taskRepo $row.path) -Algorithm SHA256).Hash -ieq $row.sha256) ('Source changed during execution: '+$row.path)}
Write-033Json (Join-Path $reports 'cli-results.json') $cliReceipts.ToArray()
$passed=@($results|Where-Object {$_.Passed}).Count;$failed=$results.Count-$passed
$result=@{PowerShell=$PSVersionTable.PSVersion.ToString();Passed=$passed;Failed=$failed;Results=$results.ToArray();Fixtures=$work;SourceInputs=$taskInputs.files;InputManifestSHA256=(Hash $taskInputPath);ProductionExecutionAvailable=$false;NativeRuntimeExecuted=$false;GameAccepted=$false}
Write-033Json (Join-Path $work 'results.json') $result;Write-033Json (Join-Path $expected 'independent-target-results.json') $result
Write-Output ('033 target CPU: '+$passed+' passed, '+$failed+' failed; '+$work)
if($results.Count -ne 17 -or $failed){throw 'Target CPU cases failed or incomplete'}
