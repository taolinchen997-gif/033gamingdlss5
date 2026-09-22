param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# File fixtures and console subprocesses only. Never run game/legacy installer
# code: the copied public launcher targets an inert receipt probe, with no UI.
$ErrorActionPreference='Stop'
$deploy=Join-Path (Split-Path -Parent $PSScriptRoot) 'deploy'
. (Join-Path $deploy 'deployment_transaction.ps1')
$results=[Collections.Generic.List[object]]::new()
$root=Join-Path $OutputDirectory ('中文测试-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $root|Out-Null
$runIndex=0
$childOutputCodePage=65001 # The first probe writes only its UTF-8 JSON receipt.
function Check($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Case([string]$Name,[scriptblock]$Body){
    try{& $Body;$results.Add(@{Name=$Name;Passed=$true});Write-Output "PASS $Name"}
    catch{$results.Add(@{Name=$Name;Passed=$false;Error=$_.Exception.Message;Stack=$_.ScriptStackTrace});Write-Output "FAIL $Name : $($_.Exception.Message)"}
}
function Put([string]$Path,[string]$Text){[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($true))}
function Folder([string]$Name){$path=Join-Path $root ($Name+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6));New-Item -ItemType Directory -Path $path|Out-Null;return $path}
function RunCmd([string]$Path,[string[]]$Arguments=@()){
    # Test-generated paths are individually quoted. No CALL or shell-built
    # deletion/move; delayed expansion stays off for literal exclamation marks.
    foreach($arg in @($Path)+$Arguments){if($arg.Contains('"') -or $arg.Contains([char]13) -or $arg.Contains([char]10)){throw 'Unsupported test argument'}}
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=Join-Path $env:SystemRoot 'System32\cmd.exe'
    $info.Arguments='/d /v:off /s /c ""'+$Path+'"'+$(if($Arguments.Count){' '+(($Arguments|ForEach-Object {'"'+$_+'"'}) -join ' ')}else{''})+'"'
    $info.WorkingDirectory=$OutputDirectory
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.RedirectStandardInput=$true
    # ConvertFrom-Json can return Int64; force the code-page overload in PS7.
    $info.StandardOutputEncoding=[Text.Encoding]::GetEncoding([int]$script:childOutputCodePage)
    $info.StandardErrorEncoding=[Text.Encoding]::GetEncoding([int]$script:childOutputCodePage)
    # The original bare powershell resolves only to the real system shell.
    $info.EnvironmentVariables['PATH']=(Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0')+';'+(Join-Path $env:SystemRoot 'System32')
    $process=[Diagnostics.Process]::new();$process.StartInfo=$info
    try{
        [void]$process.Start();$process.StandardInput.Close()
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(20000)){throw 'Fixture console subprocess timed out; no game was started'}
        $text=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult();$code=$process.ExitCode
        $script:runIndex++
        Write-033Json (Join-Path $root ('cmd-'+$script:runIndex+'.json')) @{Cmd=$Path;Arguments=$Arguments;WorkingDirectory=$info.WorkingDirectory;ExitCode=$code;OutputCodePage=$script:childOutputCodePage;Stdout=$text;Stderr=$errorText}
        return [pscustomobject]@{ExitCode=$code;Stdout=$text;Stderr=$errorText}
    }finally{$process.Dispose()}
}
function NewFixture([string]$InstallerOverride) {
    $base=Folder '安装夹具';$game=Join-Path $base '游戏 (中文) ! &';$package=Join-Path $base '安装包 中文'
    $tools=Join-Path $package 'tools';New-Item -ItemType Directory -Path $game,$tools,(Join-Path $package 'payload')|Out-Null
    Get-ChildItem -LiteralPath $deploy -File|Copy-Item -Destination $tools
    if($InstallerOverride){Copy-Item -LiteralPath $InstallerOverride -Destination (Join-Path $tools 'integrated_install.ps1') -Force}
    # Inert AMD64 PE import name. No code or entry point.
    $bytes=[byte[]]::new(1024)
    function U16($At,$Value){[BitConverter]::GetBytes([uint16]$Value).CopyTo($bytes,$At)}
    function U32($At,$Value){[BitConverter]::GetBytes([uint32]$Value).CopyTo($bytes,$At)}
    U16 0 0x5a4d;U32 60 128;U32 128 0x4550;U16 132 0x8664;U16 134 1;U16 148 240
    U16 152 0x20b;U32 260 16;U32 272 4096;U32 276 40;U32 400 512;U32 404 4096;U32 408 512;U32 412 512
    U32 524 4160;[Text.Encoding]::ASCII.GetBytes('winmm.dll'+[char]0).CopyTo($bytes,576)
    $exe=Join-Path $game '关卡 (测试) ! &.exe';[IO.File]::WriteAllBytes($exe,$bytes)
    foreach($name in @('winmm.dll','dlss5-033.addon64')){Put (Join-Path $game $name) ('old '+$name)}
    Put (Join-Path $game 'dlss5-033.cfg') ("# 用户设置"+[Environment]::NewLine+"engine=0"+[Environment]::NewLine+"inject=0"+[Environment]::NewLine+"passes=3"+[Environment]::NewLine+"work=80"+[Environment]::NewLine)
    Put (Join-Path $game 'dlss5-033.state') '7';Put (Join-Path $game 'ReShade.ini') "[User]";Put (Join-Path $game 'OptiScaler.ini') "[User]"
    Write-033Json (Join-Path $game '_033-integrated.json') @{Version=1;GameExe=$exe;CoreMount='winmm.dll';CoreHash=(Get-033FileHash (Join-Path $game 'winmm.dll'));PackageId='old-fixture'}
    Put (Join-Path $game '_安装记录.txt') ('proxy=winmm.dll'+[Environment]::NewLine+'proxycands=winmm.dll,version.dll'+[Environment]::NewLine+'proxyall=winmm.dll'+[Environment]::NewLine)
    foreach($name in @('dlss5_check.ps1','dlss5_remount.ps1')){Put (Join-Path $game $name) "throw 'OLD ENTRY MUST NOT RUN'"}
    $sources=Join-Path $PSScriptRoot 'fixtures\installer_cmd'
    foreach($name in @('检查有没有生效.cmd','按Home没反应就点我.cmd')){Copy-Item -LiteralPath (Join-Path $sources $name) -Destination $game}
    Put (Join-Path $package 'payload\033-engine.dll') 'new inert core';Put (Join-Path $package 'payload\adapter.dll') 'new inert adapter'
    $files=@(@{Path='payload/033-engine.dll';SHA256=(Get-033FileHash (Join-Path $package 'payload\033-engine.dll'))},@{Path='payload/adapter.dll';SHA256=(Get-033FileHash (Join-Path $package 'payload\adapter.dll'))})
    Write-033Json (Join-Path $package 'manifest.json') @{Version=1;PackageId='cmd-fixture';EngineLayout='single-033';Files=$files;Payload=@(@{Path=$files[0].Path;SHA256=$files[0].SHA256;Target='@CORE@'},@{Path=$files[1].Path;SHA256=$files[1].SHA256;Target='dlss5-033.addon64'})}
    $before=@{};foreach($file in (Get-ChildItem -LiteralPath $game -File)){$before[$file.Name]=Get-033FileHash $file.FullName}
    $installed=& (Join-Path $tools 'integrated_install.ps1') -Action Install -GameExe $exe -PackageRoot $package
    @{Game=$game;Exe=$exe;Package=$package;Receipt=$installed.Receipt;Before=$before;Installer=(Join-Path $tools 'integrated_install.ps1')}
}
Case 'original public CMD launches Windows PowerShell 5.1 through a Chinese package path' {
    $package=Folder '原始包 (中文) ! &'
    $source=Join-Path $PSScriptRoot 'fixtures\installer_cmd\DLSS5一键包.cmd'
    $cmd=Join-Path $package 'DLSS5一键包.cmd';Copy-Item -LiteralPath $source -Destination $cmd
    Check ((Get-033FileHash $source) -eq (Get-033FileHash $cmd)) 'Original launcher bytes changed'
    # Stub only the PS target. Never run the original installer or its GUI.
    Put (Join-Path $package 'dlss5_install.ps1') @'
param([string]$GameExe)
$ErrorActionPreference='Stop'
$receipt=@{GameExe=$GameExe;ScriptRoot=$PSScriptRoot;WorkingDirectory=[Environment]::CurrentDirectory;PSEdition=$PSVersionTable.PSEdition;Version=$PSVersionTable.PSVersion.ToString();OutputCodePage=[Console]::OutputEncoding.CodePage;Text='中文路径已正确传入';ExecutedGame=$false}
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'probe.json'),($receipt|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
'@
    $argument=Join-Path $package '游戏 (中文) ! &.exe'
    $run=RunCmd $cmd @($argument);Check ($run.ExitCode -eq 0) ('Public launcher failed: '+$run.Stderr)
    $receipt=Get-Content -LiteralPath (Join-Path $package 'probe.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $script:childOutputCodePage=$receipt.OutputCodePage
    Check ($receipt.PSEdition -eq 'Desktop' -and $receipt.Version.StartsWith('5.1.') -and $receipt.ScriptRoot -eq $package -and $receipt.GameExe -eq $argument -and $receipt.Text -eq '中文路径已正确传入') 'Default CMD encoding/path/version mismatch'
    $run=RunCmd $cmd;Check ($run.ExitCode -eq 0) 'No-argument launcher path failed'
    $receipt=Get-Content -LiteralPath (Join-Path $package 'probe.json') -Raw -Encoding UTF8|ConvertFrom-Json
    Check (-not $receipt.GameExe) 'No-argument launch invented a target'
}
Case 'before-candidate CMD failures are reproduced, then both entries work after the narrow update' {
    $beforeInstaller=Join-Path $PSScriptRoot 'fixtures\installer_cmd\before_integrated_install.ps1'
    $f=NewFixture $beforeInstaller
    $check=RunCmd (Join-Path $f.Game '检查有没有生效.cmd')
    Check ($check.ExitCode -ne 0 -and $check.Stderr.Contains('OLD ENTRY MUST NOT RUN')) 'Baseline did not reach the retained legacy alias'
    $homeRun=RunCmd (Join-Path $f.Game '按Home没反应就点我.cmd')
    Check ($homeRun.ExitCode -ne 0 -and $homeRun.Stderr.Contains('Specify an available different mount')) 'Baseline Home/no-argument failure not reproduced'
    & (Join-Path $deploy 'integrated_install.ps1') -Action Install -GameExe $f.Exe -PackageRoot $f.Package | Out-Null
    foreach($name in @('检查有没有生效.cmd','按Home没反应就点我.cmd')){Check ((RunCmd (Join-Path $f.Game $name)).ExitCode -eq 0) 'Updated entry still fails'}
}
Case 'installed default check and Home CMD both produce Chinese read-only reports from another cwd' {
    $f=NewFixture;$before=@{};Get-ChildItem -LiteralPath $f.Game -File|ForEach-Object {$before[$_.Name]=Get-033FileHash $_.FullName}
    foreach($name in @('检查有没有生效.cmd','按Home没反应就点我.cmd')){
        $run=RunCmd (Join-Path $f.Game $name);Check ($run.ExitCode -eq 0) ('Default entry failed: '+$run.Stderr)
        Check ($run.Stdout.Contains('诊断报告已保存：') -and $run.Stdout.Contains($f.Game)) 'Redirected Chinese message/path did not round-trip'
        $report=Get-Content -LiteralPath (Join-Path $f.Game '033-diagnostic.json') -Raw -Encoding UTF8|ConvertFrom-Json
        Check ($report.GameExe -eq $f.Exe -and $report.Actions.Count -eq 0 -and $report.Installation.Status -eq 'core-hash-matched' -and $report.Route.Note.Contains('原生补帧')) 'Report/Chinese text/ownership differs'
    }
    foreach($name in $before.Keys){Check ((Get-033FileHash (Join-Path $f.Game $name)) -eq $before[$name]) "Default diagnosis changed $name"}
}
Case 'explicit Chinese EXE argument and legacy Auto flag remain diagnostics only' {
    $f=NewFixture;$run=RunCmd (Join-Path $f.Game '检查有没有生效.cmd') @('-GameExe',$f.Exe,'-Auto')
    Check ($run.ExitCode -eq 0) ('Argument forwarding failed: '+$run.Stderr)
    $report=Get-Content -LiteralPath (Join-Path $f.Game '033-diagnostic.json') -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($report.GameExe -eq $f.Exe -and $report.Actions.Count -eq 0 -and $report.Route.AbnormalExitCounter -eq '7') 'Arguments changed behavior or counter'
}
Case 'missing ownership record returns nonzero and replaces a stale success report' {
    $f=NewFixture;$cmd=Join-Path $f.Game '检查有没有生效.cmd';Check ((RunCmd $cmd).ExitCode -eq 0) 'Initial diagnostic failed'
    Remove-Item -LiteralPath (Join-Path $f.Game '_033-integrated.json')
    $run=RunCmd $cmd;Check ($run.ExitCode -eq 1) 'CMD lost the PowerShell failure exit code'
    Check ($run.Stderr.Contains('诊断未完成：')) 'Redirected Chinese error did not round-trip'
    $report=Get-Content -LiteralPath (Join-Path $f.Game '033-diagnostic.json') -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($report.Verdict -eq 'error' -and $report.Actions.Count -eq 0) 'Stale success report survived failure'
}
Case 'changed core is reported without automatic remount or repair' {
    $f=NewFixture;Put (Join-Path $f.Game 'winmm.dll') 'external edited core';$hash=Get-033FileHash (Join-Path $f.Game 'winmm.dll')
    Check ((RunCmd (Join-Path $f.Game '按Home没反应就点我.cmd')).ExitCode -eq 0) 'Read-only report failed'
    $report=Get-Content -LiteralPath (Join-Path $f.Game '033-diagnostic.json') -Raw -Encoding UTF8|ConvertFrom-Json
    Check ($report.Installation.Status -eq 'core-hash-mismatch' -and (Get-033FileHash (Join-Path $f.Game 'winmm.dll')) -eq $hash -and -not(Test-Path (Join-Path $f.Game 'version.dll'))) 'Default Home entry mutated a mount'
}
Case 'new entry files and overwritten legacy aliases have exact rollback bytes' {
    $f=NewFixture
    & $f.Installer -Action Restore -Receipt $f.Receipt | Out-Null
    foreach($name in $f.Before.Keys){Check ((Get-033FileHash (Join-Path $f.Game $name)) -eq $f.Before[$name]) "Exact rollback differs: $name"}
    Check (-not(Test-Path (Join-Path $f.Game 'integrated_supervisor.ps1'))) 'Rollback retained a newly introduced dependency'
}
foreach($failure in @('missing-transaction','broken-transaction','broken-path-helper','missing-supervisor','broken-supervisor-json')){
    Case ('early dependency failure replaces stale report: '+$failure) {
        $f=NewFixture;$cmd=Join-Path $f.Game '检查有没有生效.cmd'
        $reportPath=Join-Path $f.Game '033-diagnostic.json'
        Write-033Json $reportPath @{Verdict='old-success';GeneratedUtc='2001-01-01T00:00:00.0000000Z'}
        switch($failure){
            'missing-transaction' {Remove-Item -LiteralPath (Join-Path $f.Game 'deployment_transaction.ps1')}
            'broken-transaction' {Put (Join-Path $f.Game 'deployment_transaction.ps1') 'function Broken {'}
            'broken-path-helper' {Put (Join-Path $f.Game 'deployment_transaction.ps1') "function Get-033Path {throw 'BROKEN PATH HELPER'}"}
            'missing-supervisor' {Remove-Item -LiteralPath (Join-Path $f.Game 'integrated_supervisor.ps1')}
            'broken-supervisor-json' {Put (Join-Path $f.Game 'integrated_supervisor.ps1') "Write-Output 'invalid-json'"}
        }
        $attempt=[DateTime]::UtcNow;$run=RunCmd $cmd
        $report=Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8|ConvertFrom-Json
        Check ($run.ExitCode -eq 1 -and $run.Stderr.Contains('本次失败报告已保存')) 'Early dependency failure bypassed current failure report'
        # Preserve UTC kind when PS7 deserializes ISO timestamps as DateTime.
        $reportedUtc=ConvertTo-033UtcTime $report.GeneratedUtc
        Check ($report.Verdict -eq 'error' -and $report.Actions.Count -eq 0 -and $reportedUtc -ge $attempt -and $run.Stderr.Contains($reportedUtc.ToString('o'))) 'Stale success or missing attempt boundary'
    }
}
Case 'locked report returns nonzero with current attempt time and explicit stale-report warning' {
    $f=NewFixture;$reportPath=Join-Path $f.Game '033-diagnostic.json'
    Write-033Json $reportPath @{Verdict='old-success';GeneratedUtc='2001-01-01T00:00:00.0000000Z'}
    Remove-Item -LiteralPath (Join-Path $f.Game 'deployment_transaction.ps1')
    $before=Get-033FileHash $reportPath
    $lock=[IO.FileStream]::new($reportPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{$run=RunCmd (Join-Path $f.Game '检查有没有生效.cmd')}finally{$lock.Dispose()}
    Check ($run.ExitCode -eq 1 -and $run.Stderr.Contains('报告未更新，旧文件不能作为本次结果') -and $run.Stderr -match 'UTC：\d{4}-\d{2}-\d{2}T') 'Locked output lost failure/time boundary'
    Check ((Get-033FileHash $reportPath) -eq $before) 'Locked old report was overwritten'
}
Case 'unwritable fixture directory returns nonzero and restores only its exact ACL' {
    $f=NewFixture;$reportPath=Join-Path $f.Game '033-diagnostic.json'
    Write-033Json $reportPath @{Verdict='old-success';GeneratedUtc='2001-01-01T00:00:00.0000000Z'}
    Remove-Item -LiteralPath (Join-Path $f.Game 'deployment_transaction.ps1')
    $before=Get-033FileHash $reportPath
    $target=[IO.Path]::GetFullPath($f.Game)
    Check ($target.StartsWith([IO.Path]::GetFullPath($root)+'\',[StringComparison]::OrdinalIgnoreCase)) 'ACL test escaped its inert fixture'
    $savedAcl=Get-Acl -LiteralPath $target;$savedSddl=$savedAcl.Sddl
    $denyAcl=Get-Acl -LiteralPath $target
    $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
    $rule=[Security.AccessControl.FileSystemAccessRule]::new($sid,[Security.AccessControl.FileSystemRights]::CreateFiles,[Security.AccessControl.InheritanceFlags]::None,[Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Deny)
    [void]$denyAcl.AddAccessRule($rule)
    try{
        Set-Acl -LiteralPath $target -AclObject $denyAcl
        $blocked=$false
        try{$probe=[IO.File]::Open((Join-Path $target 'denied-probe.tmp'),[IO.FileMode]::CreateNew);$probe.Dispose()}catch{$blocked=$true}
        Check $blocked 'Fixture directory was not actually write denied'
        $run=RunCmd (Join-Path $f.Game '检查有没有生效.cmd')
    }finally{Set-Acl -LiteralPath $target -AclObject $savedAcl}
    $restoredSddl=(Get-Acl -LiteralPath $target).Sddl
    Write-033Json (Join-Path $root 'fixture-acl-restored.json') @{Path=$target;Before=$savedSddl;After=$restoredSddl;OnlyInertFixture=$true}
    Check ($savedSddl -eq $restoredSddl) 'Fixture ACL not restored exactly'
    Check ($run.ExitCode -eq 1 -and $run.Stderr.Contains('报告未更新，旧文件不能作为本次结果') -and $run.Stderr -match 'UTC：\d{4}-\d{2}-\d{2}T') 'Unwritable report directory lost failure/time boundary'
    Check ((Get-033FileHash $reportPath) -eq $before) 'Unwritable directory changed the stale report'
}
$summary=[ordered]@{CallerPowerShell=$PSVersionTable.PSVersion.ToString();DefaultChildPowerShell='Windows PowerShell 5.1';Passed=@($results|Where-Object {$_.Passed}).Count;Failed=@($results|Where-Object {-not $_.Passed}).Count;Fixtures=$root;Results=$results.ToArray();GameExecution=$false;LegacyInstallerExecution=$false;DesktopUI=$false}
Write-033Json (Join-Path $OutputDirectory 'cmd-results.json') $summary
Write-Output "CMD entry: $($summary.Passed) passed, $($summary.Failed) failed"
if($summary.Failed){throw 'Default CMD regressions; see cmd-results.json'}
