# 033 managed install/update/uninstall. All original bytes stay in the external
# vault. Newer PE, JSON, file identity and executable exclusion helpers are reused.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'deployment_transaction.ps1')
. (Join-Path $PSScriptRoot 'package_layout.ps1')
. (Join-Path $PSScriptRoot 'pe_inspection.ps1')
. (Join-Path $PSScriptRoot 'cleanup_footprint.ps1')
. (Join-Path $PSScriptRoot 'game_clean.ps1')
. (Join-Path $PSScriptRoot 'yanyun_only.ps1')
. (Join-Path $PSScriptRoot 'game_survey.ps1')
. (Join-Path $PSScriptRoot 'yanyun_target.ps1')
. (Join-Path $PSScriptRoot 'yanyun_discovery.ps1')
. (Join-Path $PSScriptRoot 'mount_evidence.ps1')
. (Join-Path $PSScriptRoot 'proxy_compat.ps1')
. (Join-Path $PSScriptRoot 'entry_bootstrap.ps1')
. (Join-Path $PSScriptRoot 'release_contract.ps1')
. (Join-Path $PSScriptRoot 'mount_history.ps1')
. (Join-Path $PSScriptRoot 'checkup.ps1')
. (Join-Path $PSScriptRoot 'diagnostics.ps1')
. (Join-Path $PSScriptRoot 'route_seed_merge.ps1')
. (Join-Path $PSScriptRoot 'vault_storage.ps1')
# 2026-09-12 评论区（谭雪：「5.0 正常能用，6.0 点安装器就跳出来这个」）：安装器还没碰任何游戏文件
#   就报「"LIB 环境变量"中新定的搜索路径 "G:\Program Files (x86)\...\MSVC.43.34808\libd" 无效
#   —— 系统找不到指定的路径」，而且是【视为错误的警告】。他说 G 盘压根没那个文件 —— 没错，那不是他的问题：
#   Windows PowerShell 5.1 的 Add-Type 会真的调一次 C# 编译器，编译器继承本进程的环境变量，而他机器上
#   装过又删过 Visual Studio 生成工具，LIB 还指着已经不存在的目录。这三个变量跟我们要编的这三个小 .cs
#   一点关系都没有，编之前清掉即可。只改【本进程】，不动机器和用户的设置；本进程马上就结束，不用恢复。
foreach($stale in @('LIB','INCLUDE','LIBPATH')){
    if([Environment]::GetEnvironmentVariable($stale)){[Environment]::SetEnvironmentVariable($stale,$null)}
}
try{
    foreach($type in @(@('Installer033.StrictJsonV1','independent_json.cs'),@('Installer033.FileIdentityV1','independent_windows.cs'),@('Installer033.ExecutionTargetLeaseV1','independent_execution_windows.cs'),@('Installer033.VaultLinksV1','vault_storage.cs'))){
        if(-not ($type[0] -as [type])){Add-Type -LiteralPath (Join-Path $PSScriptRoot $type[1])}
    }
}catch{
    throw ('安装器启动时要先编译几个很小的 C# 辅助文件，这一步失败了：'+$_.Exception.Message+
        '。这跟你的游戏无关，是本机的 .NET / Visual Studio 环境有问题 —— 最常见的是装过又删过 Visual Studio 生成工具，'+
        'LIB / INCLUDE 这类环境变量还指着已经不存在的目录（安装器已经在自己这个进程里清过一遍了）。重启一次电脑通常就好；'+
        '还不行就在「系统属性 → 高级 → 环境变量」里把指向不存在目录的 LIB / INCLUDE 删掉。')
}
function Read-033ManagedJson([string]$Path){
    $bytes=[IO.File]::ReadAllBytes($Path);if($bytes.Length -gt 8MB){throw '安装记录过大'}
    $data=[Installer033.StrictJsonV1]::Parse([Text.UTF8Encoding]::new($false,$true).GetString($bytes).TrimStart([char]0xfeff))
    if($data -isnot [Collections.IDictionary]){throw '安装记录不是有效对象'}
    return ,$data
}
function Copy-033ManagedObject($Value){return ,[Installer033.StrictJsonV1]::Parse(($Value|ConvertTo-Json -Depth 40))}
function Get-033ManagedRoot([string]$Root){
    $full=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if($full -notmatch '^[A-Za-z]:\\' -or $full.Length -le 3){throw '必须使用准确的本机目录'}
    [void](Get-033Path $full '.033-path-check')
    if(Test-Path -LiteralPath $full){
        if([Installer033.FileIdentityV1]::DirectoryPath($full) -ine $full){throw '目录身份与实际位置不一致'}
    }
    return $full
}
function Assert-033ManagedVaultCapacity([string]$Vault){
    # This installer also runs under Windows PowerShell 5.1/.NET Framework.
    # Reserve the longest path used by a FUTURE reinstall/restore before even
    # the initial Plan is accepted. GUIDs and decimal Int32 blob indices are
    # bounded; previous-journal.json and Write-033Json's temp name are 20 chars.
    # Do not rely on host-specific long-path support or shorten existing ledgers.
    $group=$Vault+'\groups\'+('0'*32)
    $operation=$group+'\operations\'+('0'*32)
    $directories=@($Vault,($Vault+'\index'),($Vault+'\groups'),$group,($group+'\operations'),$operation,($group+'\originals'),($group+'\history'))
    $files=@(($Vault+'\index\'+('0'*64)+'.json'),($group+'\state.json'),($group+'\pending.json'),($operation+'\receipt.json'),($operation+'\previous-journal.json'),($operation+'\2147483647.before'),($operation+'\2147483647.after'),($group+'\originals\2147483647.bin'),($group+'\history\2147483647.json'))
    # Atomic JSON replacement uses a 16-hex + .tmp name in the same parent.
    $directories+=@($Vault+'\objects')
    $files+=@(($Vault+'\objects\'+('0'*64)+'.bin'),($operation+'\.033-link-'+('0'*32)+'.tmp'))
    $files+=@($directories|ForEach-Object {$_+'\'+('0'*16)+'.tmp'})
    $longest=@($files|Sort-Object Length -Descending)[0]
    if($longest.Length -gt 259 -or @($directories|Where-Object {$_.Length -gt 247}).Count){
        throw [IO.PathTooLongException]::new("033_VAULT_PATH_CAPACITY: 备份目录过长，后续重装或卸载的账本路径将达到 $($longest.Length) 个字符（最多259）。请改用较短的游戏目录外备份路径；尚未开始本次安装。")
    }
}
function Get-033ManagedKey([string]$Path){
    $h=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($h.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($Path).ToLowerInvariant())))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}
}
function Get-033ManagedSnapshot([string]$Root,[string]$Path){
    $p=Get-033Path $Root $Path
    $hash=Get-033FileHash $p
    if(-not $hash){return @{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}}
    if([Installer033.FileIdentityV1]::LinkCount($p) -ne 1){throw "不支持硬链接目标: $p"}
    $item=Get-Item -LiteralPath $p -Force
    return @{Exists=$true;Hash=$hash;Identity=[Installer033.FileIdentityV1]::Identity($p);Attributes=[int](Get-033SettableAttributes $item.Attributes);Time=$item.LastWriteTimeUtc.ToString('o')}
}
function Test-033ManagedSnapshot($A,$B,[switch]$ContentOnly,[switch]$HashOnly){
    # -HashOnly：跨次运行判断「是不是 033 装的那份」只看内容（业主 2026-09-11 定）。杀软扫描、拷贝游戏目录、游戏自己的
    # 镜像机制都会改掉文件编号和时间而一个字节不动；黎之轨迹的更新、鬼武者 5070 的收拾残局就是这样被拦的。
    # 同一次运行里写文件那几秒的核对仍然严格。
    if($null -eq $A -or $null -eq $B -or $A.Exists -ne $B.Exists -or $A.Hash -ine $B.Hash){return $false}
    if(-not $A.Exists -or $HashOnly){return $true}
    return ($ContentOnly -or $A.Identity -ceq $B.Identity) -and (Get-033SettableAttributes $A.Attributes) -eq (Get-033SettableAttributes $B.Attributes) -and (ConvertTo-033UtcTime $A.Time).Ticks -eq (ConvertTo-033UtcTime $B.Time).Ticks
}
function Copy-033ManagedBlob([string]$Source,[string]$Target,[string]$Hash){
    # Keep the exact validated ledger path, including recovery from an older
    # install whose previous-journal path exceeds MAX_PATH. Extended local
    # paths are only an I/O spelling; never rewrite stored paths or guess a vault.
    $sourceFull=[IO.Path]::GetFullPath($Source);$targetFull=[IO.Path]::GetFullPath($Target)
    if($sourceFull -notmatch '^[A-Za-z]:\\' -or $targetFull -notmatch '^[A-Za-z]:\\'){throw '备份必须使用已验证的本机绝对路径'}
    $sourceIo='\\?\'+$sourceFull;$targetIo='\\?\'+$targetFull
    [IO.File]::Copy($sourceIo,$targetIo,$false)
    [IO.File]::SetAttributes($targetIo,[IO.FileAttributes]::Normal)
    $s=[IO.FileStream]::new($targetIo,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $hasher=[Security.Cryptography.SHA256]::Create()
    try{
        $actual=([BitConverter]::ToString($hasher.ComputeHash($s))).Replace('-','')
        if($actual -ine $Hash){throw "备份或组件校验失败: $Target"}
        $s.Flush($true)
    }finally{$hasher.Dispose();$s.Dispose()}
}
function Invoke-033ManagedStep([string]$Stage,$Journal,$File) {} # offline fault seam
function Get-033ManagedIndex([string]$Vault,[string]$Exe){Get-033Path $Vault ('index/'+(Get-033ManagedKey $Exe)+'.json')}
function Read-033ManagedGroup([string]$Vault,[string]$Exe){
    $index=Get-033ManagedIndex $Vault $Exe
    if(-not(Test-Path -LiteralPath $index)){return $null}
    $ptr=Read-033ManagedJson $index
    if($ptr.Schema -ne 1 -or $ptr.Exe -ine $Exe -or $ptr.Group -notmatch '^[a-f0-9]{32}$'){throw '安装索引损坏，不能猜测恢复目录'}
    $folder=Get-033Path $Vault ('groups/'+$ptr.Group)
    $statePath=Get-033Path $folder 'state.json';$pendingPath=Get-033Path $folder 'pending.json'
    $state=if(Test-Path -LiteralPath $statePath){Read-033ManagedJson $statePath}else{$null}
    $pending=if(Test-Path -LiteralPath $pendingPath){Read-033ManagedJson $pendingPath}else{$null}
    $scope=if($pending -and ($pending.Status -notin @('committed','recovered') -or -not $state)){$pending.Group}else{$state}
    if(-not $scope -or $scope.Schema -ne 1 -or $scope.Id -cne $ptr.Group -or $scope.Vault -ine $Vault -or @($scope.Targets|Where-Object {$_.Exe -ieq $Exe}).Count -ne 1){throw '安装索引与目标/回执不一致'}
    return @{Folder=$folder;State=$state;Pending=$pending;Group=$scope}
}
# Resolve the WHOLE requested set before planning and again under the vault
# mutex. A settled restored record is history, not a restriction on a new scope.
# Keep every historical group unchanged; normal new-install journaling publishes
# new index pointers only after package/target/backup preflight has succeeded.
function Resolve-033RequestedGroup([string]$Vault,[string[]]$Executables,[string]$Action){
    if($Action -notin @('Plan','Install')){return Read-033ManagedGroup $Vault $Executables[0]}
    $selected=$null
    foreach($exe in $Executables){
        $candidate=Read-033ManagedGroup $Vault $exe
        if(-not $candidate){continue}
        $state=$candidate.State;$pending=$candidate.Pending
        $settled=$state -and $state.Status -eq 'restored'
        if($settled -and $pending){
            $settled=$pending.Status -eq 'recovered' -or
                ($pending.Status -eq 'committed' -and $pending.Action -eq 'Restore' -and
                 $pending.Group.Id -ceq $state.Id -and $pending.Group.Status -eq 'restored')
        }
        if($settled){continue}
        if($selected -and $selected.Group.Id -cne $candidate.Group.Id){
            throw '所选入口仍有不同的现装或未完成事务；须保留各自原件并完成还原，不能直接合并账本'
        }
        $selected=$candidate
    }
    if($selected){
        foreach($exe in $Executables){
            if(@($selected.Group.Targets|Where-Object Exe -IEQ $exe).Count -ne 1){
                throw '所选入口超出现装或未完成事务的范围；需先完成该安装组的还原，再安装完整双入口'
            }
        }
    }
    return $selected
}
function Assert-033ManagedEntry($Group,$Entry){
    if($Entry.Root -lt 0 -or $Entry.Root -ge $Group.Targets.Count){throw '无效目标编号'}
    $root=$Group.Targets[$Entry.Root].Root;$target=Get-033Path $root $Entry.Path
    if($Entry.Path -match '[*?]' -or $target -ieq $Group.Targets[$Entry.Root].Exe -or $Entry.Path -match '(?i)(^|[\\/])(?:_033transactions|_DLSS5_备份|\.033-directory\.guard)([\\/]|$)'){throw '禁止改写游戏程序、历史恢复证据或猜测路径'}
    if($Entry.Path -match '(?i)(^|[\\/])(?:saves?|savegames?)([\\/]|$)' -or $Entry.Path -match '(?i)\.(sav|pak|vpk|pck)$'){throw '禁止改写存档或游戏资源包'}
    return $target
}
# 2026-09-11 业主定「回退老安装器，保留备份和原样卸载」：5.0 从不独占主程序。V3 的独占租约每写一个文件就把整个
# 主程序重算一遍哈希（审判之眼 394 MB，玩家以为卡死关窗口），游戏一更新主程序哈希对不上就再也卸载不了，燕云启动器
# 开着或主程序是硬链接时整个装不上。现在只核目标根没变，开写前查游戏有没有在运行；写入的原子性和撤销仍靠日志与备份库。
function Get-033RunningProcessPaths{
    # 测试替身入口。受保护进程（反作弊）读不到路径，读不到的就不算。
    $rows=@()
    foreach($p in @(Get-Process -ErrorAction SilentlyContinue)){
        $path=$null;try{$path=$p.Path}catch{}
        if($path){$rows+=@([pscustomobject]@{Id=$p.Id;Name=$p.ProcessName;Path=$path})}
    }
    return $rows
}
function Assert-033GameNotRunning($Group){
    $procs=@();try{$procs=@(Get-033RunningProcessPaths)}catch{}
    foreach($t in @($Group.Targets)){
        $hit=@($procs|Where-Object {$_.Path -ieq $t.Exe})
        if($hit.Count){throw ('033_GAME_RUNNING: 游戏还开着：'+(Split-Path -Leaf $t.Exe)+'（进程 '+(@($hit|ForEach-Object {$_.Id}) -join '、')+'）。先把游戏完全退出（任务管理器里也没有了），再运行安装器。')}
    }
}
function Open-033ManagedGates($Group){
    foreach($t in @($Group.Targets)){
        # 根目录是主程序目录，或主程序目录下面的子目录（逐游戏名单的 InstallTarget：Source 引擎的 bin、泰坦陨落2 的 bin\x64_retail）。
        # 写入范围仍锁在游戏文件夹里；以后名单改了，也不会卡住已经装上的更新和卸载。
        $exeRoot=Get-033ManagedRoot (Split-Path -Parent ([string]$t.Exe))
        if((Get-033ManagedRoot $t.Root) -ine $t.Root -or (Get-033Path $exeRoot ([IO.Path]::GetFileName($t.Exe))) -ine $t.Exe -or (($t.Root -ine $exeRoot) -and -not ([string]$t.Root).StartsWith($exeRoot+'\',[StringComparison]::OrdinalIgnoreCase))){throw '目标根发生变化'}
    }
    Assert-033GameNotRunning $Group
    return @{All=[Collections.Generic.List[object]]::new();Directories=@{};Images=@{}}
}
function Close-033ManagedGates($Gates){if($Gates){for($i=$Gates.All.Count-1;$i -ge 0;$i--){$Gates.All[$i].Dispose()}}}
function Assert-033ManagedGates($Gates){}
function Open-033ManagedDirectory($Gates,[string]$Path){
    $full=Get-033ManagedRoot $Path
    if(-not(Test-Path -LiteralPath $full -PathType Container)){[void][IO.Directory]::CreateDirectory($full)}
}
# 2026-09-12 评论区两例：鸣潮「一直提示这个」，以及另一位「下午用的好好，晚上装什么都变成这样了」——
#   都是 Replace 抛 "Unable to remove the file to be replaced."。那句话的意思只有一个：目标文件
#   【这一刻】删不掉，因为别的进程正拿着它。三种最常见：游戏或它的启动器/反作弊还在后台跑；
#   杀毒软件正在扫我们刚写出来的那个文件；资源管理器开着预览。前两种几乎都是一过性的，
#   过一秒再试就成了 —— 以前一次失败就整趟回滚，玩家只看到一句生的英文，完全不知道该做什么。
#   现在退避重试几次；仍然不行才报错，而且把话说成人能照着做的。
function Invoke-033LockedFileOp([string]$Target,[scriptblock]$Operation){
    $delays=@(120,250,500,900,1500)
    for($i=0;;$i++){
        try{& $Operation;return}
        catch [IO.IOException],[UnauthorizedAccessException]{
            if($i -ge $delays.Count){
                throw ('这个文件现在被别的程序占着，换不了：'+$Target+
                    '。按这三样查：①游戏或它的启动器、反作弊还在后台跑 —— 任务管理器里结束掉；'+
                    '②杀毒软件正在扫描它 —— 把游戏目录加进白名单；③这个文件夹开着预览窗口 —— 关掉。'+
                    '处理完重新运行安装器就行：本次没有留下任何文件，原件和记录都还在。原始报错：'+$_.Exception.Message)
            }
            Start-Sleep -Milliseconds $delays[$i]
        }
    }
}
function Set-033ManagedFile($Journal,$Entry,[string]$JournalPath,[string]$Folder,$Gates,[switch]$Undo){
    $target=Assert-033ManagedEntry $Journal.Group $Entry;$root=$Journal.Group.Targets[$Entry.Root].Root
    $expected=if($Undo){$Entry.Applied}else{$Entry.Before}
    $desired=if($Undo){$Entry.Before}else{$Entry.After}
    $blob=if($Undo){$Entry.BeforeBlob}else{$Entry.AfterBlob}
    $slot=if($Undo){'UndoIntent'}else{'Intent'}
    $resuming=[bool]$Entry[$slot] # 接着上一次运行被打断的写入：上一次记下的快照只按内容比
    Assert-033ManagedGates $Gates
    $now=Get-033ManagedSnapshot $root $Entry.Path
    if(-not $now.Exists -and -not $desired.Exists){return $now}
    $contentDone=$false
    if($Entry[$slot]){
        $intent=$Entry[$slot]
        if($intent['Final'] -and (Test-033ManagedSnapshot $now $intent.Final)){return $now}
        if(Test-033ManagedSnapshot $now $intent.Result -HashOnly){$contentDone=$true}
        if(-not $contentDone -and -not(Test-033ManagedSnapshot $now $intent.Before -HashOnly) -and -not(Test-033ManagedSnapshot $now $intent.Writable -HashOnly)){throw "中断后文件已被外部修改，保留备份: $target"}
    }else{
        if(-not(Test-033ManagedSnapshot $now $expected)){throw "执行前文件发生变化，保留备份: $target"}
        $parent=Split-Path -Parent $target
        $missing=@();$cursor=$parent
        while(-not(Test-Path -LiteralPath $cursor)){$missing+=@($cursor);$cursor=Split-Path -Parent $cursor}
        foreach($dir in $missing){if($Journal.CreatedDirectories -notcontains $dir){$Journal.CreatedDirectories+=@($dir)}}
        Write-033Json $JournalPath $Journal
        Open-033ManagedDirectory $Gates $parent
        $writable=Copy-033ManagedObject $now
        if($writable.Exists){$writable.Attributes=([int]$writable.Attributes -band (-bnot [int][IO.FileAttributes]::ReadOnly));if(-not $writable.Attributes){$writable.Attributes=[int][IO.FileAttributes]::Normal}}
        $intent=@{Before=$now;Writable=$writable;Stage=$null;Result=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}}
        if($desired.Exists){
            $relativeParent=[IO.Path]::GetDirectoryName($Entry.Path)
            $stageName='.033-stage-'+[Guid]::NewGuid().ToString('N')+'.tmp'
            $stageRel=if($relativeParent){$relativeParent+'/'+$stageName}else{$stageName}
            $stage=Get-033Path $root $stageRel
            # Record exact staging path BEFORE creation; interrupted preparation
            # retains the original and never gives an arbitrary file ownership.
            $intent.Stage=$stageRel;$Entry[$slot]=$intent;Write-033Json $JournalPath $Journal
            Copy-033ManagedBlob (Get-033Path $Folder $blob) $stage $desired.Hash
            [IO.File]::SetLastWriteTimeUtc($stage,(ConvertTo-033UtcTime $desired.Time))
            # Use explicit Archive during the rename; Windows does not preserve Normal alone.
            $attrs=([int]$desired.Attributes -band (-bnot ([int][IO.FileAttributes]::ReadOnly -bor [int][IO.FileAttributes]::Normal))) -bor [int][IO.FileAttributes]::Archive
            [IO.File]::SetAttributes($stage,(ConvertTo-033FileAttributes $attrs))
            $intent.Result=Get-033ManagedSnapshot $root $stageRel
        }
        $Entry[$slot]=$intent;Write-033Json $JournalPath $Journal
    }
    if(-not $contentDone){
    Invoke-033ManagedStep $(if($Undo){'before-undo'}else{'before-write'}) $Journal $Entry
    Assert-033ManagedGates $Gates
    $now=Get-033ManagedSnapshot $root $Entry.Path
    if(-not(Test-033ManagedSnapshot $now $intent.Before -HashOnly:$resuming) -and -not(Test-033ManagedSnapshot $now $intent.Writable -HashOnly:$resuming)){throw "写入前目标有外部改动: $target"}
    if($desired.Exists){
        $stage=Get-033Path $root $intent.Stage
        if(-not(Test-033ManagedSnapshot (Get-033ManagedSnapshot $root $intent.Stage) $intent.Result -HashOnly:$resuming)){throw '暂存内容/身份发生变化'}
        if($now.Exists){[IO.File]::SetAttributes($target,(ConvertTo-033FileAttributes $intent.Writable.Attributes));Invoke-033LockedFileOp $target {[IO.File]::Replace($stage,$target,[NullString]::Value)}}else{Invoke-033LockedFileOp $target {[IO.File]::Move($stage,$target)}}
    }elseif($now.Exists){[IO.File]::SetAttributes($target,(ConvertTo-033FileAttributes $intent.Writable.Attributes));Invoke-033LockedFileOp $target {[IO.File]::Delete($target)}}
    $now=Get-033ManagedSnapshot $root $Entry.Path
    # 只比内容：exFAT/FAT 上改名会换文件编号、时间精度也不同，按编号和时间严核会把自己刚写的文件当成别人的（2026-09-11 HoneySelect2 H 盘）。
    if(-not(Test-033ManagedSnapshot $now $intent.Result -HashOnly)){throw "实际写入未匹配暂存内容: $target"}
    Invoke-033ManagedStep $(if($Undo){'after-undo-content'}else{'after-content'}) $Journal $Entry
    }
    if($desired.Exists){
        # Journal both sides of the metadata-only window, retaining exact identity.
        $final=Copy-033ManagedObject $now;$final.Attributes=[int](Get-033SettableAttributes $desired.Attributes)
        $intent['Final']=$final;Write-033Json $JournalPath $Journal
        [IO.File]::SetAttributes($target,(ConvertTo-033FileAttributes $desired.Attributes))
        $now=Get-033ManagedSnapshot $root $Entry.Path
        if(-not(Test-033ManagedSnapshot $now $final -HashOnly) -or (Get-033SettableAttributes $now.Attributes) -ne (Get-033SettableAttributes $final.Attributes)){throw '恢复属性核验失败'}
    }
    return $now
}
function Get-033ManagedOwnedCurrent($Entry,$Now){
    foreach($s in @($Entry.Applied,$Entry.Before)) {if($s -and (Test-033ManagedSnapshot $Now $s -HashOnly)){return $true}}
    foreach($name in @('Intent','UndoIntent')){
        if($Entry[$name]){foreach($s in @($Entry[$name].Before,$Entry[$name].Writable,$Entry[$name].Result,$Entry[$name]['Final'])){if($s -and (Test-033ManagedSnapshot $Now $s -HashOnly)){return $true}}}
    }
    return $false
}
# 2026-09-11 公开包实测（生化危机4，笔记本）：写暂存文件时被打断（杀软占住、窗口被关），暂存身份还没来得及记，
# 撤销就一直拒绝清理它，之后每次重试都卡在「暂存文件身份未完整记录或发生变化」。暂存名是写入前就记进日志的随机名；
# 它若仍是普通的单链接文件，且字节正是本次要写的那份组件的完整内容或开头一段，就是本进程写的，可以清掉。别的一律留作证据。
function Get-033PrefixHash([string]$Path,[long]$Length){
    $sha=[Security.Cryptography.SHA256]::Create();$stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        $buf=[byte[]]::new(4MB);$left=$Length
        while($left -gt 0){$n=$stream.Read($buf,0,[int][Math]::Min([long]$buf.Length,$left));if($n -le 0){throw '文件比预期短'};[void]$sha.TransformBlock($buf,0,$n,$null,0);$left-=$n}
        [void]$sha.TransformFinalBlock([byte[]]::new(0),0,0)
        return ([BitConverter]::ToString($sha.Hash)).Replace('-','')
    }finally{$stream.Dispose();$sha.Dispose()}
}
function Test-033StageIsOwnBlobPrefix([string]$StagePath,[string]$BlobPath){
    try{
        if(-not $BlobPath -or -not(Test-Path -LiteralPath $BlobPath -PathType Leaf)){return $false}
        $stage=Get-Item -LiteralPath $StagePath -Force
        if($stage.PSIsContainer -or ($stage.Attributes -band [IO.FileAttributes]::ReparsePoint)){return $false}
        if([Installer033.FileIdentityV1]::LinkCount($StagePath) -ne 1){return $false}
        $length=[long]$stage.Length
        if($length -gt (Get-Item -LiteralPath $BlobPath -Force).Length){return $false}
        return (Get-033PrefixHash $StagePath $length) -ceq (Get-033PrefixHash $BlobPath $length)
    }catch{return $false}
}
function Restore-033ManagedPending([string]$Folder,$Journal,$Gates){
    $jp=Get-033Path $Folder 'pending.json'
    if($Journal.Status -in @('committed','recovered')){return}
    # Inspect EVERY entry/backup before any reversal. Never use -Force to ignore conflicts.
    foreach($e in $Journal.Files){
        [void](Assert-033ManagedEntry $Journal.Group $e)
        $now=Get-033ManagedSnapshot $Journal.Group.Targets[$e.Root].Root $e.Path
        # 2026-09-11 回退到 5.0：既不是原件也不是 033 那份的文件原地保留，其余照常撤销，不再整个停下。
        if(-not(Get-033ManagedOwnedCurrent $e $now)){$e['LeftAsFound']=$now;continue}
        if($e.Before.Exists -and (Get-033FileHash (Get-033Path $Folder $e.BeforeBlob)) -ine $e.Before.Hash){throw '恢复备份缺失或损坏'}
    }
    Assert-033VulkanJournal $Journal
    $Journal.Status='recovering';Write-033Json $jp $Journal
    if($Journal.Action -eq 'Install'){Invoke-033VulkanRegistryActions $Journal $jp -Undo}
    for($i=$Journal.Files.Count-1;$i -ge 0;$i--){
        $e=$Journal.Files[$i];$now=Get-033ManagedSnapshot $Journal.Group.Targets[$e.Root].Root $e.Path
        if($e.ContainsKey('LeftAsFound') -and $e.LeftAsFound){continue}
        if(Test-033ManagedSnapshot $now $e.Before -HashOnly){continue}
        if($e.UndoIntent -and $e.UndoIntent['Final'] -and (Test-033ManagedSnapshot $now $e.UndoIntent.Final)){$e.Applied=$now;continue}
        $e.Applied=$now
        $restored=Set-033ManagedFile $Journal $e $jp $Folder $Gates -Undo
        if(-not(Test-033ManagedSnapshot $restored $e.Before -HashOnly)){throw '撤销后未回到本次操作前'}
        $e['Recovered']=$restored;Write-033Json $jp $Journal
    }
    if($Journal.Action -eq 'Restore'){Invoke-033VulkanRegistryActions $Journal $jp -Undo}
    $old=$Journal.OldState
    if($old){
        foreach($item in $old.Entries){
            $match=@($Journal.Files|Where-Object {$_.Root -eq $item.Root -and $_.Path -ieq $item.Path})
            if($match.Count){$item.Current=Get-033ManagedSnapshot $old.Targets[$item.Root].Root $item.Path}
        }
        Write-033Json (Get-033Path $Folder 'state.json') $old
    }elseif(Test-Path -LiteralPath (Get-033Path $Folder 'state.json')){
        # A crash can occur after state.json is written but before the commit
        # marker. Keep evidence, but never retain a fictitious installed state.
        $aborted=Copy-033ManagedObject $Journal.Group
        $aborted.Status='restored';$aborted.Entries=@();$aborted.CreatedDirectories=@()
        Write-033Json (Get-033Path $Folder 'state.json') $aborted
    }
    foreach($e in $Journal.Files){
        $root=$Journal.Group.Targets[$e.Root].Root
        foreach($name in @('Intent','UndoIntent')){
            $intent=$e[$name]
            if($intent -and $intent.Stage){
                $path=Get-033Path $root $intent.Stage
                if(Test-Path -LiteralPath $path){
                    $owned=$intent.Result.Exists -and (Test-033ManagedSnapshot (Get-033ManagedSnapshot $root $intent.Stage) $intent.Result)
                    if(-not $owned){
                        $blobRel=$(if($name -eq 'UndoIntent'){$e['BeforeBlob']}else{$e['AfterBlob']})
                        $owned=[bool]$blobRel -and (Test-033StageIsOwnBlobPrefix $path (Get-033Path $Folder $blobRel))
                    }
                    if(-not $owned){
                        # 认不出的暂存文件：复制进备份库证据目录，再从游戏目录挪走（5.0 不会为它停下）。
                        $evid=Get-033Path $Folder ('evidence/'+[Guid]::NewGuid().ToString('N')+'.stage');[void][IO.Directory]::CreateDirectory((Split-Path -Parent $evid))
                        Copy-033ManagedBlob $path $evid (Get-033FileHash $path)
                    }
                    [IO.File]::SetAttributes($path,[IO.FileAttributes]::Normal);[IO.File]::Delete($path)
                }
            }
        }
    }
    $Journal.Status='recovered';Write-033Json $jp $Journal
}
function Read-033ManagedPackage([string]$PackageRoot){
    $root=Get-033PackageDataRoot $PackageRoot;$path=Get-033Path $root '033-package.json'
    if(-not(Test-Path -LiteralPath $path)){throw '缺少新版033-package.json组件清单；不能调用旧脚本覆盖安装。请使用完整新版安装包。'}
    $p=Read-033ManagedJson $path
    if($p.Schema -ne 1 -or $p.Kind -cne '033-managed-package' -or -not $p.Version -or -not $p.Profiles.Count -or $p.Profiles.Count -gt 32){throw '不支持的安装包清单'}
    Assert-033ReleaseContract $root $p
    if(-not $p.ContainsKey('Notices') -or -not $p.Notices.Count){throw '完整组件包必须包含来源和许可证清单'}
    foreach($notice in $p.Notices){if($notice.Hash -notmatch '^[A-Fa-f0-9]{64}$' -or (Get-033FileHash (Get-033Path $root $notice.Source)) -ine $notice.Hash){throw '来源/许可证缺失或损坏'}}
    $seen=@{}
    # Shared model/core files occur in many profiles. Hash each exact resolved
    # source once PER READ, comparing that actual hash against EVERY declaration.
    # No cross-call cache: a later Plan/Install reads the package again. Staging
    # still copies and hashes bytes under an exclusive handle before any commit.
    $payloadHashes=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    # 2026-09-17 Fable（优化流程）：同一进程里 Survey 和 Install 各读一遍包，600 MB 的 payload 被算两次 SHA。按「路径|大小|修改时间」记住本进程算过的值；
    #   写入阶段仍在独占句柄下逐字节复制并校验，缓存只省重复读盘，不省校验。
    if(-not (Get-Variable -Name K033PayloadHashCache -Scope Script -ErrorAction SilentlyContinue)){$script:K033PayloadHashCache=@{}}
    foreach($profile in $p.Profiles){
        if([string]::IsNullOrWhiteSpace($profile.Id) -or -not $profile.Files.Count -or $profile.Files.Count -gt 10000 -or $profile.Mode -notin @('native','feeder') -or $profile.Architecture -notin @('x86','x64') -or $seen.ContainsKey($profile.Id)){throw '组件路线无效、为空或重复'}
        $seen[$profile.Id]=$true;$targets=@{}
        foreach($name in @('RuntimeFiles','RuntimeDirectories')){
            # 循环变量不能叫 $path: 它会盖掉上面 033-package.json 的路径, 函数末尾的包哈希就算错了。
            if($profile.ContainsKey($name)){foreach($runtimePath in $profile[$name]){
                if($runtimePath -isnot [string] -or $runtimePath -match '[*?]'){throw '运行产物必须给出准确相对路径'}
                [void](Get-033Path $root $runtimePath)
            }}
        }
        # 游戏自己的加载镜像目录（RE Engine 的 _storage_）：单层目录名，安装器只用已经存在的那个。
        if($profile.ContainsKey('MirrorDirectories')){foreach($mirrorName in $profile.MirrorDirectories){
            if($mirrorName -isnot [string] -or [string]::IsNullOrWhiteSpace($mirrorName) -or $mirrorName -match '[\\/*?]'){throw '镜像目录必须是游戏根目录下的单层目录名'}
            [void](Get-033Path $root $mirrorName)
        }}
        if($profile.ContainsKey('Suitability')){
            $suit=$profile.Suitability
            if($suit -isnot [Collections.IDictionary]){throw '路线适用性描述必须是对象'}
            if($suit.ContainsKey('Apis')){foreach($a in $suit.Apis){if($a -notin @('ddraw','glide','dx8','dx9','dx10','dx11','dx12','dxgi','opengl','vulkan')){throw '路线适用性 Apis 含未知接口'}}}
            if($suit.ContainsKey('NativeUpscaler') -and $suit.NativeUpscaler -notin @('absent','any','present')){throw '路线适用性 NativeUpscaler 只能是 absent/any/present'}
            if($suit.ContainsKey('Dx12Runtime') -and $suit.Dx12Runtime -notin @('absent','any','present')){throw '路线适用性 Dx12Runtime 只能是 absent/any/present'}
            if($suit.ContainsKey('EngineMarkers')){foreach($m in @($suit.EngineMarkers)){if($m -isnot [string] -or [string]::IsNullOrWhiteSpace($m) -or $m -match '[\\/:*?]'){throw '路线适用性 EngineMarkers 必须是根目录文件名'}}}
        }
        Assert-033VulkanActivation $profile
        $componentNames=@()
        if($profile.ContainsKey('Components')){
            # v1.4 optional per-machine components. A recipe may declare RTX 20/30 only (2026-09-13, see the
            # note on GpuGenerations below); whether the bridge is actually placed on this machine is still
            # decided by the GPU gate further down, never by the recipe alone.
            if($profile.Components -isnot [Collections.IDictionary] -or -not $profile.Components.Count){throw '路线组件描述必须是非空对象'}
            foreach($name in @($profile.Components.Keys)){
                $spec=$profile.Components[$name]
                if($name -cnotmatch '^[a-z0-9][a-z0-9-]{0,31}$' -or $spec -isnot [Collections.IDictionary]){throw '路线组件名或描述无效'}
                if($spec.ContainsKey('Kind') -and $spec.Kind -cne 'prepare'){throw "组件 $name 的 Kind 只能是 prepare"}
                if($spec.ContainsKey('Kind')){
                    # 2026-09-19 准备类组件（033 帧生成准备）：没有加载器，文件在 033-runtime\ 下面，和验证过它的那一个核心绑在一起。
                    $prepareGens=@($spec.GpuGenerations)
                    if(-not $prepareGens.Count -or @($prepareGens|Where-Object {$_ -notin @(20,30,40,50)}).Count){throw "准备类组件 $name 的显卡代数只能在 20/30/40/50 里"}
                    if(-not $spec.ContainsKey('GameFrameGen') -or $spec.GameFrameGen -cne 'absent'){throw "准备类组件 $name 的 GameFrameGen 必须是 absent"}
                    if([string]::IsNullOrWhiteSpace($spec.Label)){throw "组件 $name 缺少名称"}
                    foreach($forbidden in @('LoaderRole','LoaderNames','RequiredDlls','RuntimeFiles')){if($spec.ContainsKey($forbidden)){throw "准备类组件 $name 不能有 $forbidden"}}
                    if(-not $spec.ContainsKey('CoreHash') -or [string]$spec.CoreHash -notmatch '^[a-fA-F0-9]{64}$'){throw "准备类组件 $name 缺少它验证过的核心哈希"}
                    $prepareCore=@($profile.Files|Where-Object {-not $_.ContainsKey('Component') -and [string]$_.Target -ieq '033-runtime/033-engine.dll'})
                    if($prepareCore.Count -ne 1 -or [string]$prepareCore[0].Hash -ine [string]$spec.CoreHash){throw "准备类组件 $name 只能和它验证过的核心一起发：这条路线的核心不是那一个"}
                    $prepareFiles=@($profile.Files|Where-Object {$_.ContainsKey('Component') -and $_.Component -ceq $name})
                    if(-not $prepareFiles.Count){throw "准备类组件 $name 没有文件"}
                    foreach($pf in $prepareFiles){if([string]$pf.Target -cnotmatch '^033-runtime/(OptiScaler\.ini|033-fruc/[A-Za-z0-9_.-]+)$' -or $pf.Policy -cne 'replace'){throw "准备类组件 $name 的文件只能是 033-runtime/OptiScaler.ini 和 033-runtime/033-fruc/ 下的文件，策略 replace"}}
                    if(@($prepareFiles|Where-Object {[string]$_.Target -ceq '033-runtime/OptiScaler.ini'}).Count -ne 1){throw "准备类组件 $name 必须正好有一份声明 033-runtime/OptiScaler.ini"}
                    $componentNames+=@($name);continue
                }
                $gens=@($spec.GpuGenerations)
                # 2026-09-13 改回只许 20/30。09-12 放开到 40/50 的依据「V5.0 对所有显卡都装」查证是错的：
                # V5.0 只给 20/30 自动装（dlss5_install.ps1:2784-2848），40 系走编在核心里的 ImDreamt 多帧解锁，
                # 50 系原厂就有；转接件说明书也只写支持 20/30。放开后 40/50 上它用 30 系移植版顶掉原生 DLSS-G，
                # 核心的解锁见转接件又整套让开（mfg_body.inl g_stood_down），玩家多帧和原来的 2 倍一起没了。
                # 真包在整包套件里经 Read-033ManagedPackage 读取，生成器再写 40/50 会直接读包失败。
                if(-not $gens.Count -or @($gens|Where-Object {$_ -notin @(20,30)}).Count){throw "组件 $name 只能限定 RTX 20/30 显卡（40/50 用核心里的多帧解锁或原厂帧生成）"}
                if($spec.ContainsKey('GameFrameGen') -and $spec.GameFrameGen -notin @('present','any')){throw "组件 $name 的 GameFrameGen 只能是 present/any"}
                if([string]::IsNullOrWhiteSpace($spec.Label) -or [string]::IsNullOrWhiteSpace($spec.LoaderRole)){throw "组件 $name 缺少名称或加载器角色"}
                $loaderNames=@($spec.LoaderNames)
                if(-not $loaderNames.Count){throw "组件 $name 缺少加载器候选名"}
                foreach($n in $loaderNames){if($n -isnot [string] -or $n -ine [IO.Path]::GetFileName($n) -or $n -notmatch '(?i)^[a-z0-9_]+\.dll$' -or $K033ProxyNames -icontains $n){throw "组件 $name 的加载器候选名无效: $n"}}
                if($spec.ContainsKey('RequiredDlls')){foreach($n in @($spec.RequiredDlls)){if($n -isnot [string] -or $n -ine [IO.Path]::GetFileName($n) -or $n -notmatch '(?i)^[a-z0-9_]+\.dll$'){throw "组件 $name 的依赖运行库名无效: $n"}}}
                # $p holds the parsed package in this function: never reuse it as a loop variable.
                foreach($runtimeFile in @($spec.RuntimeFiles|Where-Object {$null -ne $_})){if($runtimeFile -isnot [string] -or $runtimeFile -match '[*?]' -or $runtimeFile -ine [IO.Path]::GetFileName($runtimeFile)){throw "组件 $name 的运行产物必须是根目录文件名"}}
                $loaders=@($profile.Files|Where-Object {$_.ContainsKey('Component') -and $_.Component -ceq $name -and $_.ContainsKey('Role') -and $_.Role -ceq $spec.LoaderRole})
                if($loaders.Count -ne 1 -or $loaderNames -inotcontains $loaders[0].Target){throw "组件 $name 必须有且只有一个加载器文件，目标名在候选名里"}
                $componentNames+=@($name)
            }
        }
        # 路线级必需运行库：名字必须是干净的裸文件名，跟组件那套同样的校验。
        if($profile.ContainsKey('Suitability') -and $profile.Suitability -and $profile.Suitability.ContainsKey('RequiredDlls')){
            foreach($n in @($profile.Suitability.RequiredDlls)){if($n -isnot [string] -or $n -ine [IO.Path]::GetFileName($n) -or $n -notmatch '(?i)^[a-z0-9_]+\.dll$'){throw "路线 $($profile.Id) 的必需运行库名无效: $n"}}
        }
        foreach($f in @($profile.Files|Where-Object {$_.ContainsKey('Component')})){
            if($componentNames -cnotcontains $f.Component){throw "文件声明了未定义的组件: $($f.Target)"}
            if($f.ContainsKey('Role') -and $f.Role -eq 'entry'){throw '组件文件不能是入口'}
            if($f.Target -match '[\\/]' -and -not $profile.Components[$f.Component].ContainsKey('Kind')){throw '组件文件只能放在游戏主程序目录'}
        }
        if($profile.Mode -eq 'feeder'){
            if(-not $profile.ContainsKey('Protocol') -or [string]::IsNullOrWhiteSpace($profile.Protocol)){throw 'Feeder完整路线缺少固定协议说明'}
            foreach($role in @('engine','feeder-client','model')){if(@($profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq $role}).Count -lt 1){throw "Feeder完整路线缺少组件: $role"}}
            if($profile.Architecture -eq 'x86' -and @($profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'feeder-worker' -and $_.Architecture -eq 'x64'}).Count -ne 1){throw '32位Feeder缺少准确64位worker'}
        }
        foreach($f in $profile.Files){
            if($f.Policy -notin @('replace','seed') -or $f.Hash -notmatch '^[a-fA-F0-9]{64}$'){throw '组件缺少固定哈希或写入策略'}
            if($f.ContainsKey('SeedMerge')){
                $baseName=([string]$f.Target).Replace('\','/').Split('/')[-1]
                if($f.Policy -ne 'seed' -or
                   ($f.SeedMerge -ceq '033-host-v1' -and $baseName -ine 'ReShade.ini') -or
                   ($f.SeedMerge -ceq '033-preset-v1' -and $baseName -ine '033-dlss5-preset.ini') -or
                   ($f.SeedMerge -ceq '033-mfg-v1' -and ($baseName -ine 'dlssg_to_fsr3.ini' -or $f.Role -cne 'fg-bridge-config' -or $f.Component -cne 'mfg2030')) -or
                   ($f.SeedMerge -ceq '033-opti-v1' -and $baseName -ine 'OptiScaler.ini') -or
                   $f.SeedMerge -cnotin @('033-host-v1','033-preset-v1','033-mfg-v1','033-opti-v1')){throw 'Invalid owned route seed merge declaration'}
            }
            $src=Get-033Path $root $f.Source
            if(-not $payloadHashes.ContainsKey($src)){
                if(Test-Path -LiteralPath $src -PathType Leaf){
                    $srcItem=Get-Item -LiteralPath $src -Force;$cacheKey=$src+'|'+$srcItem.Length+'|'+$srcItem.LastWriteTimeUtc.Ticks
                    if($script:K033PayloadHashCache.ContainsKey($cacheKey)){$payloadHashes[$src]=$script:K033PayloadHashCache[$cacheKey]}
                    else{$payloadHashes[$src]=Get-033FileHash $src;$script:K033PayloadHashCache[$cacheKey]=$payloadHashes[$src]}
                }else{$payloadHashes[$src]=$null}
            }
            if($payloadHashes[$src] -ine $f.Hash){throw "组件缺失/已变化，未改游戏: $($f.Source)"}
            Assert-033EntryBootstrap $src $f
            [void](Get-033Path $root $f.Target)
            if($targets.ContainsKey($f.Target)){throw '同一路线重复写入目标'};$targets[$f.Target]=$true
            if($f.Target -match '(?i)\.(dll|addon32|addon64|exe)$'){
                $pe=Get-033PeInfo $src
                if($pe.Status -ne 'valid' -or $pe.Architecture -ne $f.Architecture -or $pe.IsDll -ne ($f.Target -notmatch '(?i)\.exe$')){throw "组件位数/类型不匹配: $($f.Source)"}
            }
        }
        Assert-033FeederBootstrap $root $profile
    }
    return @{Root=$root;Data=$p;Hash=Get-033FileHash $path}
}
function Select-033ManagedProfile($Package,[string]$Exe,[switch]$ForceFeeder,[string]$Profile,$NativeDlss=$null,$Dx12Runtime=$null,$EffectiveApis=$null,[switch]$Manual,$Notes=$null){
    $pe=Get-033PeInfo $Exe
    # V3.4 manual route (owner 2026-09-11: 装不上的弄个手动模式): the person names the route, so API, engine-marker,
    # native-DLSS and D3D12-runtime matching are skipped. Architecture and a route's executable restrictions stay binding;
    # an executable whose headers only parse as far as the machine type (packed exes: 绝地潜兵 2, 逆转裁判 调查合集) is accepted.
    if($Manual){
        if([string]::IsNullOrWhiteSpace($Profile)){throw '手动模式必须指定一条路线（-Profile）'}
        if($pe.IsDll -or $pe.Architecture -notin @('x86','x64') -or $pe.Status -notin @('valid','unknown')){throw '所选文件不是有效的游戏主程序'}
        $named=@($Package.Data.Profiles|Where-Object Id -CEQ $Profile)
        if(-not $named.Count){throw ('包里没有这条路线：'+$Profile)}
        if($named[0].Architecture -ne $pe.Architecture){throw ('路线 '+$Profile+' 是 '+$(if($named[0].Architecture -eq 'x86'){'32'}else{'64'})+' 位的，这个游戏是 '+$(if($pe.Architecture -eq 'x86'){'32'}else{'64'})+' 位，手动模式也不能装')}
        $manualHash=Get-033FileHash $Exe
        if(($named[0].ContainsKey('ExecutableHashes') -and $named[0].ExecutableHashes.Count -and $named[0].ExecutableHashes -inotcontains $manualHash) -or ($named[0].ContainsKey('ExecutableNames') -and $named[0].ExecutableNames.Count -and $named[0].ExecutableNames -inotcontains [IO.Path]::GetFileName($Exe))){throw ('路线 '+$Profile+' 只给它指定的游戏主程序用')}
        return $named[0]
    }
    # 5.0 只读机器类型：加壳主程序（文件头读不全）按位数照样选路。
    if($pe.IsDll -or $pe.Architecture -notin @('x86','x64') -or $pe.Status -notin @('valid','unknown')){throw '所选文件不是有效的游戏主程序'}
    $all=@($Package.Data.Profiles|Where-Object {$_.Architecture -eq $pe.Architecture})
    $exeHash=Get-033FileHash $Exe
    $all=@($all|Where-Object {(-not $_.ContainsKey('ExecutableHashes') -or -not $_.ExecutableHashes.Count -or $_.ExecutableHashes -icontains $exeHash) -and (-not $_.ContainsKey('ExecutableNames') -or -not $_.ExecutableNames.Count -or $_.ExecutableNames -icontains [IO.Path]::GetFileName($Exe))})
    # 点名这个主程序的专用路线（燕云 yysls.exe、龙之信条2 DD2.exe）先于一切接口 / DLSS 判断：业主「识别到燕云就走这个」。
    if(-not $Profile -and -not $ForceFeeder){
        $namedFirst=@($all|Where-Object {$_.ContainsKey('ExecutableNames') -and @($_.ExecutableNames).Count})
        if($namedFirst.Count){return $namedFirst[0]}
        # 5.0 的逐游戏兼容（它的「-Route GL」出路）：FTL 实际跑 OpenGL，接口判据会按 DX11 算。点名走对应接口的路线。
        $exeApiRule=@{'ftlgame.exe'='opengl'}
        $leafName=[IO.Path]::GetFileName($Exe).ToLowerInvariant()
        if($exeApiRule.ContainsKey($leafName)){
            $ruled=@($all|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis') -and (@($_.Suitability.Apis) -contains $exeApiRule[$leafName])})
            if($ruled.Count){
                if($Notes -is [ref]){$Notes.Value=@($Notes.Value)+@('逐游戏兼容表（5.0）：'+[IO.Path]::GetFileName($Exe)+' 实际跑 '+$exeApiRule[$leafName]+'，按这条路线装。')}
                return $ruled[0]
            }
        }
    }
    # x64 legacy routes mount inside the game process, so their Mode is 'native';
    # filtering on Mode left zero candidates and -ForceFeeder could never work
    # there (2026-09-11: 巫师3 / 古剑奇谭三 both hit this). The product line is
    # what "Feeder" actually means: 033 brings its own DLSS instead of riding the
    # game's. Older recipes without ProductLine keep the previous behaviour.
    if($ForceFeeder){
        $byLine=@($all|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('ProductLine') -and $_.Suitability.ProductLine -eq 'legacy'})
        $all=@(if($byLine.Count){$byLine}else{@($all|Where-Object Mode -eq 'feeder')})
    }
    if($Profile){$all=@($all|Where-Object Id -CEQ $Profile)}
    # API facts come from the newer bounded PE parser. No giant binary string
    # search and no substituting a launcher or guessing a different EXE.
    $imports=@($pe.Imports)+@($pe.DelayImports)
    # Engines that LoadLibrary their graphics DLL have no graphics import at all;
    # matching on the import table alone refuses them (2026-09-11: 古剑奇谭三 loads
    # d3d11 dynamically, 巫师3 的 DX12 入口经 Streamline 到 d3d12). When the caller
    # surveyed effective APIs and the import table shows no graphics module, match
    # on those instead. Static imports still win when present.
    $matchNames=$imports
    $staticGraphics=@(Get-033ApiNames $imports|Where-Object {$_ -ne 'dxgi'})
    # 2026-09-17 Fable（5.0 天命奇御教训，Ori / Kingdom New Lands 实测）：Unity 主程序静态导入的 opengl32 只是可选后端，
    #   实际跑 D3D11。原来这里按导入表把它们送进 legacy-opengl-x86、挂 opengl32.dll，ReShade 进去了却拿不到设备。
    if($staticGraphics.Count -and -not @($staticGraphics|Where-Object {$_ -ne 'opengl'}).Count -and (Test-033UnityGame (Split-Path -Parent $Exe))){
        $imports=@($imports|Where-Object {$_ -ine 'opengl32.dll'});$matchNames=$imports;$staticGraphics=@()
        if($Notes -is [ref]){$Notes.Value=@($Notes.Value)+@('Unity 游戏（5.0）：主程序静态导入的 opengl32 只是可选后端，Windows 上实际跑 D3D11，按 D3D11 选路。')}
    }
    # Compute it here when the caller did not survey it, so that the plan builder
    # and the survey can never disagree about which routes match (same Get-033GraphicsEvidence ladder).
    if($null -eq $EffectiveApis -and -not $staticGraphics.Count){
        $EffectiveApis=@((Get-033GraphicsEvidence $Exe).Apis)
    }
    if($null -ne $EffectiveApis){
        if(-not $staticGraphics.Count){
            $apiToModule=@{'ddraw'='ddraw.dll';'dx8'='d3d8.dll';'dx9'='d3d9.dll';'dx10'='d3d10.dll';'dx11'='d3d11.dll';'dx12'='d3d12.dll';'dxgi'='dxgi.dll';'opengl'='opengl32.dll';'vulkan'='vulkan-1.dll'}
            $extra=@($EffectiveApis|ForEach-Object {if($apiToModule.ContainsKey($_)){$apiToModule[$_]}})
            if($extra.Count){$matchNames=@(@($imports)+@($extra)|Sort-Object -Unique)}
        }
    }
    # v5.0 的规则（2026-09-11 仁王1 评论区找回）：同时导入 DX10/11/12 和 DX9/DX8/DirectDraw 时按新接口算，旧接口多半是
    # 视频、覆盖层或兼容件在用；不去掉就是 DX11、DX9 两条路线打平，自动安装直接拒绝。
    $matchNames=@(Select-033ModernGraphicsModules $matchNames)
    $beforeImports=@($all)
    $all=@($all|Where-Object {(-not $_.Imports.Count) -or @($_.Imports|Where-Object {$matchNames -icontains $_}).Count})
    $apis=@(Get-033ApiNames $matchNames)
    if(-not $all.Count -and $beforeImports.Count){
        # 5.0 不因为接口对不上路线就不装：DX12 / DX11 两条路线文件一模一样，OpenGL 和老接口也按 D3D11 那条走（64 位没有 DDraw/D8 包装时同样如此）。
        # 只有纯 Vulkan 5.0 也拒绝。
        $fallback=@()
        if(@($apis|Where-Object {$_ -in @('dx12')}).Count){$fallback+=@('d3d11.dll')}
        if(@($apis|Where-Object {$_ -in @('dx10','dx11')}).Count){$fallback+=@('d3d12.dll')}
        if(@($apis|Where-Object {$_ -in @('opengl','dx9','dx8','ddraw','glide')}).Count -or -not @($apis|Where-Object {$_ -ne 'dxgi'}).Count){$fallback+=@('d3d11.dll')}
        if($fallback.Count){
            $all=@($beforeImports|Where-Object {(-not $_.Imports.Count) -or @($_.Imports|Where-Object {$fallback -icontains $_}).Count})
            if($Notes -ne $null -and $all.Count){$Notes.Value=@($Notes.Value)+@('包里没有和检测到的接口（'+(@($apis) -join '、')+'）完全对应的路线，按 5.0 的办法改走 '+(@($fallback|Select-Object -Unique) -join '/')+' 那一类。')}
        }
    }
    # 2026-09-17 Fable（5.0 规则）：DirectDraw 常被 D3D8/D3D9 游戏顺带引用（播片、枚举显示模式），3D 走的是 D3D8/9；
    #   两类路线同时匹配时只留 D3D8/D3D9 那条，硬塞 DDraw 包装反而出问题。
    if($all.Count -gt 1 -and @($apis|Where-Object {$_ -in @('dx8','dx9','dx10','dx11','dx12')}).Count){
        $notDdrawOnly=@($all|Where-Object {-not($_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis') -and @($_.Suitability.Apis).Count -and -not @(@($_.Suitability.Apis)|Where-Object {$_ -notin @('ddraw','glide')}).Count)})
        if($notDdrawOnly.Count){$all=$notDdrawOnly}
    }
    # 2026-09-12 引擎标记（re_chunk_000.pak）不在的路线先拿掉，再做 DLSS / D3D12 运行时的筛选。原来放在最后：纯 DX11、自带 DLSS 的游戏
    # 按 D3D12 运行时筛掉了 native-dx11，只剩 RE 路线（Dx12Runtime=any），RE 路线又因标记不在被删，落成「没有路线」被拒装。标记在的只留这些路线。
    $dirEarly=Split-Path -Parent $Exe
    $markerEarly={param($p) if(-not($p.ContainsKey('Suitability') -and $p.Suitability.ContainsKey('EngineMarkers') -and @($p.Suitability.EngineMarkers).Count)){return 'none'};if(@(@($p.Suitability.EngineMarkers)|Where-Object {-not (Test-Path -LiteralPath (Join-Path $dirEarly $_) -PathType Leaf)}).Count){return 'missing'};return 'present'}
    $markedEarly=@($all|Where-Object {(& $markerEarly $_) -eq 'present'})
    if($markedEarly.Count){$all=$markedEarly}else{$all=@($all|Where-Object {(& $markerEarly $_) -ne 'missing'})}
    # A static d3d12.dll import is DX12 evidence; dxgi.dll alone is not. Prefer a d3d12 route, but never refuse when none exists (V5).
    if(($apis -contains 'dx12') -and -not @($apis|Where-Object {$_ -in @('dx8','dx9','dx10','dx11')}).Count){
        $dx12Only=@($all|Where-Object {(-not $_.Imports.Count) -or ($_.Imports -icontains 'd3d12.dll')})
        if($dx12Only.Count){$all=$dx12Only}
    }
    # V3.1: a route that only rides the game's own DLSS (mainline v5 direct mount,
    # NativeUpscaler=present) is chosen when the folder shows DLSS/Streamline; a
    # route that builds its own DLSS (absent) is chosen when it does not. The
    # evidence comes from the survey when the caller already has it.
    $bySuit=@{}
    foreach($candidate in $all){$k='any';if($candidate.ContainsKey('Suitability') -and $candidate.Suitability.ContainsKey('NativeUpscaler')){$k=[string]$candidate.Suitability.NativeUpscaler};$bySuit[$k]=$true}
    if($bySuit.ContainsKey('present')){
        # EffectivePresent 会把"目录里有 DLSS 文件、但游戏里没有任何二进制引用 NGX"
        # 这种情形判为"没有自带 DLSS"(见 Get-033NativeDlssEvidence 的说明)。
        $present=$(if($null -ne $NativeDlss){
            if($NativeDlss.PSObject.Properties['EffectivePresent']){[bool]$NativeDlss.EffectivePresent}else{[bool]$NativeDlss.Present}
        }else{Test-033NativeDlssPresence $Exe})
        if($bySuit.ContainsKey('absent') -or $bySuit.ContainsKey('any')){
            $all=@($all|Where-Object {$k='any';if($_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('NativeUpscaler')){$k=[string]$_.Suitability.NativeUpscaler};($k -eq 'any') -or (($k -eq 'present') -eq $present)})
        }elseif(-not $present){
            # 5.0：没有自带 DLSS 的游戏走 033 自带 DLSS 的那一类（路线 A）。接口对上的只剩「只挂游戏 DLSS」的路线时，改从同位数的这一类里挑。
            $absent=@($beforeImports|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('NativeUpscaler') -and [string]$_.Suitability.NativeUpscaler -eq 'absent' -and -not($_.Suitability.ContainsKey('EngineMarkers') -and @($_.Suitability.EngineMarkers).Count)})
            $absentApi=@($absent|Where-Object {(-not $_.Imports.Count) -or @($_.Imports|Where-Object {@('d3d11.dll','d3d12.dll') -icontains $_}).Count})
            if($absentApi.Count){
                $all=$absentApi
                if($Notes -is [ref]){$Notes.Value=@($Notes.Value)+@('游戏没有自带 DLSS，接口对上的路线只挂游戏自带的 DLSS；按 5.0 的办法改走 033 自带 DLSS 的路线。')}
            }else{
                $what=$(if($apis -contains 'dx12'){'主程序静态导入 d3d12.dll，是 DX12 游戏；本包的 DX12 路线只挂游戏自带的 DLSS，'}else{'匹配到的路线只挂游戏自带的 DLSS，'})
                throw ($what+'但目录里没找到 DLSS/Streamline 文件（nvngx_dlss / sl.dlss / sl.interposer 等），程序里也没有 NGX 字样。没有自带 DLSS 的游戏不能走这条路线。')
            }
        }
    }
    # V3.2: among routes that agree on the game's DLSS, D3D12-at-runtime evidence
    # decides bridge (Dx12Runtime=absent) vs no-bridge (present): a bridge on a game
    # that really runs D3D12 fights it for NGX feature 18 (v5 lesson), so a
    # bridge-only match on such a game is refused. Engine markers (re_chunk_000.pak)
    # pick the most specific route when several remain.
    $byRt=@{}
    foreach($candidate in $all){$k='any';if($candidate.ContainsKey('Suitability') -and $candidate.Suitability.ContainsKey('Dx12Runtime')){$k=[string]$candidate.Suitability.Dx12Runtime};$byRt[$k]=$true}
    if($byRt.ContainsKey('absent') -or $byRt.ContainsKey('present')){
        $rt=$(if($null -ne $Dx12Runtime){[bool]$Dx12Runtime.Present}else{[bool](Get-033Dx12RuntimeEvidence (Split-Path -Parent $Exe) $Exe).Present})
        $keep=@($all|Where-Object {$k='any';if($_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Dx12Runtime')){$k=[string]$_.Suitability.Dx12Runtime};($k -eq 'any') -or (($k -eq 'present') -eq $rt)})
        if($keep.Count){$all=$keep}
        elseif($rt -and $byRt.ContainsKey('absent') -and -not $byRt.ContainsKey('present')){throw '匹配到的路线带 DX11 桥，但这个游戏实际以 D3D12 运行（目录里有 Agility SDK/DXC 文件，或程序里有 d3d12.dll 字样）；桥会和游戏抢同一个 NGX 特征，不能装。'}
    }
    $dir=Split-Path -Parent $Exe
    $markerState={param($p) if(-not($p.ContainsKey('Suitability') -and $p.Suitability.ContainsKey('EngineMarkers') -and @($p.Suitability.EngineMarkers).Count)){return 'none'};if(@(@($p.Suitability.EngineMarkers)|Where-Object {-not (Test-Path -LiteralPath (Join-Path $dir $_) -PathType Leaf)}).Count){return 'missing'};return 'present'}
    if($all.Count -gt 1){
        $marked=@($all|Where-Object {(& $markerState $_) -eq 'present'})
        if($marked.Count){$all=$marked}else{$all=@($all|Where-Object {(& $markerState $_) -eq 'none'})}
    }elseif($all.Count -eq 1 -and (& $markerState $all[0]) -eq 'missing'){$all=@()}
    if(-not $ForceFeeder -and -not $Profile){
        $native=@($all|Where-Object Mode -eq 'native')
        if($native.Count){$all=$native}
    }
    # 游戏能走 DX12 时优先 DX12 路线。同时报 dx11 和 dx12 的游戏(不少引擎两套渲染器
    # 都在)会同时匹配上"带 D3D11→D3D12 桥"和"直接用游戏 D3D12 设备"两条; 桥要和游戏
    # 抢同一个 NGX 特征, 直挂不用抢 —— 巫师3 上正是 DX12 那条通了、DX11 那条没通。
    # 用户指定了 -Profile 就以用户为准, 这里不插手。
    if($all.Count -gt 1 -and -not $Profile -and ($apis -contains 'dx12')){
        $dx12=@($all|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis') -and (@($_.Suitability.Apis) -contains 'dx12')})
        if($dx12.Count){$all=$dx12}
    }
    # 2026-09-13 Vulkan（对称的两条，缺一不可 —— 只写"优先"那条的话，两边都匹配时就成了
    # 「按包里顺序取第一条」，等于拿路线在配方里的位置决定玩家装到什么）：
    #   ① 游戏有真正的 D3D / OpenGL 画面接口 → 纯 Vulkan 的路线直接出局（它服务不了 D3D 启动的游戏）。
    if($all.Count -gt 1 -and -not $Profile -and @($apis|Where-Object {$_ -in @('dx10','dx11','dx12','dx9','dx8','ddraw','opengl')}).Count){
        $notVkOnly=@($all|Where-Object {
            $a=@();if($_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis')){$a=@($_.Suitability.Apis|ForEach-Object {[string]$_})}
            -not ($a.Count -and -not @($a|Where-Object {$_ -ne 'vulkan'}).Count)
        })
        if($notVkOnly.Count){$all=$notVkOnly}
    }
    #   ② 只在【纯 Vulkan】时优先 Vulkan 路线 —— 也就是除了 dxgi 之外没有任何
    # D3D 画面接口。很多 Vulkan 游戏顺手也导入 dxgi.dll（枚举显卡 / HDR），光看 dxgi 会和
    # native-dx11 打平。而真的两套渲染器都有的游戏（玩家可能按 D3D 启动），保持原来的判断不动 ——
    # 这种情况下改判 Vulkan 反而可能把本来装得好的游戏弄坏。
    if($all.Count -gt 1 -and -not $Profile -and ($apis -contains 'vulkan') -and -not @($apis|Where-Object {$_ -in @('dx10','dx11','dx12','dx9','dx8','ddraw','opengl')}).Count){
        $vk=@($all|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis') -and (@($_.Suitability.Apis) -contains 'vulkan')})
        if($vk.Count){
            $all=$vk
            if($Notes -is [ref]){$Notes.Value=@($Notes.Value)+@('这个游戏只用 Vulkan（dxgi 只是用来枚举显卡的），按 Vulkan 路线装。')}
        }
    }
    # 点名游戏主程序的专用路线（ExecutableNames，比如燕云 yysls.exe）胜过同样匹配的通用路线：
    # 业主 2026-09-11「燕云就单独一条，识别到燕云就走这个」。手动指定 -Profile 时以用户为准。
    if($all.Count -gt 1 -and -not $Profile){
        $named=@($all|Where-Object {$_.ContainsKey('ExecutableNames') -and @($_.ExecutableNames).Count})
        if($named.Count -eq 1){$all=$named}
    }
    if(-not $all.Count){
        $gfxOnly=@($apis|Where-Object {$_ -ne 'dxgi'})
        # The managed x64 layer supplies NR through the shared hosted pipeline;
        # a game-owned DLSS library is not required for this route.
        if($gfxOnly.Count -and -not @($gfxOnly|Where-Object {$_ -ne 'vulkan'}).Count){
            $vkAny=@($Package.Data.Profiles|Where-Object {$_.ContainsKey('Suitability') -and $_.Suitability.ContainsKey('Apis') -and (@($_.Suitability.Apis) -contains 'vulkan')})
            if(-not $vkAny.Count){throw '这是只用 Vulkan 的游戏，而这个包里没有 Vulkan 路线。'}
            if($pe.Architecture -ne 'x64'){throw '这是 32 位的 Vulkan 游戏：本包的 Vulkan 路线只有 64 位，装不了，目录里什么都没写。'}
            throw '当前 Vulkan 配方没有匹配所选程序的架构或明确条件，请核对完整安装包与实际游戏入口；尚未改动目录。'
        }
        throw ('包里没有适合这个游戏的路线（游戏接口：'+$(if(@($apis|Where-Object {$_ -ne 'dxgi'}).Count){@($apis) -join '、'}else{'没识别出来'})+'）。可以试试手动选一条路线。')
    }
    if($all.Count -gt 1){
        # 5.0 从不因为「好几条都行」拒装：几条路线往往文件一模一样，按包里的顺序取第一条，并说明另外几条。
        if($Notes -is [ref]){$Notes.Value=@($Notes.Value)+@('同时符合的路线：'+(@($all|ForEach-Object {$_.Id}) -join '、')+'；按包里顺序装 '+$all[0].Id+'，不对就用手动模式换。')}
    }
    return $all[0]
}
# 挂载点手工指定 (-Proxy)。配方给的默认挂载点绝大多数是 dxgi.dll; 个别游戏不加载它,
# 或那个名字已经被别的东西占着, 就需要换一个 ReShade 能顶替的系统 DLL 名。
# 这份名单不是猜的: 逐项对应 ReShade fork 的 res/exports.def 里真正导出了该模块入口
# 点的那些节 (d2d1 / d3d9 / d3d10 / d3d10_1 / d3d11 / d3d12 / ddraw / dxgi / opengl32 /
# dinput / dinput8)。d3d8.dll 不在其中 —— DX8 那条靠 dgVoodoo 顶, 所以这里也不许填。
# 换名会改变 ReShade 自己的判定 (source/dll_main.cpp 依模块名分 is_d3d/is_dxgi/is_opengl),
# 这是它本来就支持的用法, 不是绕过。
$Script:K033ProxyNames=@('dxgi.dll','d3d9.dll','d3d10.dll','d3d10_1.dll','d3d11.dll','d3d12.dll','ddraw.dll','d2d1.dll','dinput8.dll','dinput.dll','opengl32.dll','ReShade64.dll','ReShade32.dll')
# ReShade64.dll / ReShade32.dll 不是系统代理名：只在 OptiScaler 占着 dxgi 时用，由 OptiScaler 按官方协议 LoadReshade=true 加载（Fable 2026-09-17，5.0 同一做法）。
# Retained core-proxy metadata for historical recipes. The current Vulkan
# route uses a real loader layer and never selects a system proxy from this list.
$Script:K033CoreProxyNames=@('dxgi.dll','d3d12.dll','winmm.dll','version.dll','dbghelp.dll','winhttp.dll','wininet.dll')
# Proxy entry metadata; a registered Vulkan layer has no system proxy entry.
# 返回 @{File=<配方里的那一项>;Names=<这个入口允许的挂载名>} ，找不到唯一入口返回 $null。
function Get-033RouteEntry($Profile){
    if(-not $Profile -or -not $Profile.Files){return $null}
    $reshade=@($Profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'entry' -and (($_.ContainsKey('HostKind') -and $_.HostKind -cin @('reshade','reshade-yanyun-v1')) -or [IO.Path]::GetFileName([string]$_.Source) -imatch '^ReShade(32|64)\.dll$')})
    if($reshade.Count -eq 1){return @{File=$reshade[0];Names=$Script:K033ProxyNames;Kind='reshade'}}
    if($reshade.Count -gt 1){return $null}
    $core=@($Profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'entry' -and [IO.Path]::GetFileName([string]$_.Source) -imatch '^033-engine\.dll$'})
    if($core.Count -eq 1){return @{File=$core[0];Names=$Script:K033CoreProxyNames;Kind='core'}}
    return $null
}
function Assert-033ProxyName([string]$Proxy,$Names=$null){
    $name=$Proxy.Trim()
    if(-not $name){return $null}
    $allowed=@(if($null -ne $Names -and @($Names).Count){@($Names)}else{$Script:K033ProxyNames})
    if($name -ine [IO.Path]::GetFileName($name) -or $name -match '[*?]'){throw ('挂载点只能是一个文件名，不能带路径或通配: '+$Proxy)}
    if($allowed -notcontains $name){throw ('本路线的入口文件不导出 '+$name+' 的入口点，改成它游戏会加载失败。可用: '+($allowed -join '、'))}
    return $name
}

# 2026-09-17 Fable：OptiScaler 链式共存时追加一条 OptiScaler.ini 的种子合并（[Plugins] LoadReshade=true、[DlssNr] Enabled=false）。
#   这条不在配方里，只在目录里真的有 OptiScaler 时动态加入；原件先进备份库，卸载时原样放回。
function Add-033OptiChainFile($Profile,$Package){
    if(-not $Profile -or -not $Package){return $Profile}
    if(@($Profile.Files|Where-Object {[string]$_.Target -ieq 'OptiScaler.ini'}).Count){return $Profile}
    $rel='payload/seeds/opti/OptiScaler.ini';$src=Get-033Path ([string]$Package.Root) $rel
    if(-not (Test-Path -LiteralPath $src -PathType Leaf)){throw '包里缺少 OptiScaler 链式种子 payload/seeds/opti/OptiScaler.ini'}
    $copy=Copy-033ManagedObject $Profile
    $copy.Files=@($copy.Files)+@(@{Source=$rel;Target='OptiScaler.ini';Policy='seed';Role='config';SeedMerge='033-opti-v1';Hash=(Get-033FileHash $src)})
    return $copy
}
function Set-033ProxyMount($Profile,[string]$Proxy){
    if(-not $Proxy){return $Profile}
    # 配方里 Role=entry 可能有两项 (DX8/DX9 那两条是 dgVoodoo 的 D3D8/D3D9 加 ReShade),
    # 只准动 ReShade 那一项; 动了 dgVoodoo 那项等于把包装链拆了。
    $copy=Copy-033ManagedObject $Profile
    $found=Get-033RouteEntry $copy
    if(-not $found){throw ('路线 '+$copy.Id+' 没有唯一的入口文件，不支持改挂载点')}
    $name=Assert-033ProxyName $Proxy $found.Names
    $entry=@($found.File)
    if($entry[0].Target -ieq $name){return $copy}
    $taken=@($copy.Files|Where-Object {$_.Target -ieq $name})
    if($taken.Count){throw ('路线 '+$copy.Id+' 里 '+$name+' 已经被本路线自己的组件占着，换过去会互相顶掉')}
    $entry[0].Target=$name
    return $copy
}
# v5.0 同一条安全禁令(dlss5_install.ps1 1542-1547, 1602-1607): 目录里有 Agility SDK 或 Streamline
# 时, 挂载点绝不能是 d3d12.dll —— 游戏自己的 D3D12Core / sl.interposer 加载链会被截断。
# 只禁这一个名字: dxgi.dll 在这类游戏上是跑通过的(巫师3 DX12 = Agility 1.600 + Streamline 1.5.6)。
# Agility 判据复用 Get-033Dx12RuntimeEvidence(和选路同一套), 只认 agility-sdk 这一种 ——
# DXC 文件或程序里的 d3d12 字样不算, v5.0 也不因为它们禁。Streamline 照 v5.0 看根目录下一层内。
function Assert-033ProxyAllowedHere([string]$Root,[string]$Exe,[string]$Name){
    if($Name -ine 'd3d12.dll'){return}
    $Root=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $rt=Get-033Dx12RuntimeEvidence $Root $Exe
    $sl=@(Get-ChildItem -LiteralPath $Root -File -Filter 'sl.interposer.dll' -Recurse -Depth 1 -ErrorAction SilentlyContinue)
    $why=@()
    if($rt.Kind -eq 'agility-sdk'){$why+=@('Agility SDK（'+(@($rt.Evidence) -join '、')+'）')}
    if($sl.Count){$why+=@('Streamline（'+(@($sl|ForEach-Object {$_.FullName.Substring($Root.Length+1)}) -join '、')+'）')}
    if($why.Count){throw ('不能把挂载点设成 d3d12.dll：目录里有 '+($why -join '、')+'。挂成 d3d12.dll 会截断游戏自己的 D3D12/DLSS 加载链（v5.0 同一条禁令）。请换 dxgi.dll 或 dinput8.dll。')}
}
# v1.4 optional per-machine components (the RTX 20/30 frame-generation bridge). A component stays
# in the plan only when every NVIDIA adapter is of an allowed generation and the game ships DLSS-G;
# its loader takes the first free candidate name, and an update keeps the name it was installed
# under. Decisions are recorded per target so the report, the plan and later updates agree.
function Resolve-033ManagedComponents($Profile,[string]$Root,$Survey,$OldTarget,[switch]$Clean){
    $copy=Copy-033ManagedObject $Profile;$decisions=@()
    if(-not $copy.ContainsKey('Components')){return @{Profile=$copy;Decisions=$decisions}}
    # 准备类组件排在最后：它要看这一次安装里转接件装没装。
    foreach($name in @(@($copy.Components.Keys|Where-Object {-not $copy.Components[$_].ContainsKey('Kind')}|Sort-Object)+@($copy.Components.Keys|Where-Object {$copy.Components[$_].ContainsKey('Kind')}|Sort-Object))){
        $spec=$copy.Components[$name];$reasons=@();$choice=$null
        if($spec.ContainsKey('Kind')){
            # 2026-09-19 「033 帧生成准备」：声明 + 光流运行库。条件缺一不可，不装就把原因写进报告。
            $gpuFacts=$null;if($Survey -and $Survey.PSObject.Properties['Gpu']){$gpuFacts=$Survey.Gpu}
            $gensNow=@();if($gpuFacts){$gensNow=@($gpuFacts.Generations)}
            $allowedNow=@($spec.GpuGenerations|ForEach-Object {[int]$_})
            if(-not $gpuFacts){$reasons+=@('没有显卡侦察结果')}
            elseif(-not $gensNow.Count){$reasons+=@('没找到 NVIDIA RTX 显卡（033 的两种帧生成都要它）')}
            elseif(@($gensNow|Where-Object {$_ -notin $allowedNow}).Count){
                $shownNow=@($gpuFacts.Nvidia|ForEach-Object {$g=Get-033NvidiaGeneration $_;$_+$(if($g){'（'+$g+' 系）'}else{'（代数未知）'})}) -join '、'
                $reasons+=@('显卡 '+$shownNow+'：033 的帧生成目前只在 RTX '+((@($allowedNow)|ForEach-Object {[string]$_}) -join '/')+' 系上实机验证过，这一版先不给别的显卡准备（画质 NR 不受影响）')
            }
            $fgNow=Get-033GameFrameGenEvidence $Root
            if($fgNow.Streamline){$reasons+=@('游戏自带 DLSS 帧生成（'+(@($fgNow.Evidence) -join '、')+'）：用帧生成页第一项，不需要准备')}
            foreach($bridgeName in @('nvidia_mfg_bridge.dll','dlssg_to_fsr3_amd_is_better.dll')){if((Test-Path -LiteralPath (Join-Path $Root $bridgeName)) -and -not($Clean -and (Test-033CleanRemoves (Join-Path $Root $bridgeName)))){$reasons+=@('目录里有多帧转接件 '+$bridgeName+'：033 的帧生成不和它同时工作')}}
            if(@($decisions|Where-Object {$_.Installed -and $_.Loader}).Count){$reasons+=@('这次安装放了多帧转接件：033 的帧生成不和它同时工作')}
            $guard=@(Get-033AntiCheatEvidence $Root)
            if($guard.Count){$reasons+=@('目录里有反作弊的痕迹（'+($guard -join '、')+'）：帧生成要接管显示通道，不在这类游戏上准备')}
            $installedNow=(-not $reasons.Count)
            if(-not $installedNow){$copy.Files=@($copy.Files|Where-Object {-not($_.ContainsKey('Component') -and $_.Component -ceq $name)})}
            $decisions+=@(@{Name=$name;Label=[string]$spec.Label;Installed=$installedNow;Loader=$null;LoaderHash=$null;RuntimeFiles=@();Reasons=@($reasons)})
            continue
        }
        $mine=@($copy.Files|Where-Object {$_.ContainsKey('Component') -and $_.Component -ceq $name})
        $loader=@($mine|Where-Object {$_.ContainsKey('Role') -and $_.Role -ceq $spec.LoaderRole})[0]
        $gpu=$null;if($Survey -and $Survey.PSObject.Properties['Gpu']){$gpu=$Survey.Gpu}
        $gens=@();if($gpu){$gens=@($gpu.Generations)}
        $allowed=@($spec.GpuGenerations|ForEach-Object {[int]$_})
        if(-not $gpu){$reasons+=@('没有显卡侦察结果')}
        elseif(-not $gens.Count){$reasons+=@('没找到 NVIDIA 显卡')}
        elseif(@($gens|Where-Object {$_ -notin $allowed}).Count){
            $shown=@($gpu.Nvidia|ForEach-Object {$g=Get-033NvidiaGeneration $_;$_+$(if($g){'（'+$g+' 系）'}else{'（代数未知）'})}) -join '、'
            $reasons+=@('显卡 '+$shown+' 不在 '+((@($allowed)|ForEach-Object {[string]$_+' 系'}) -join '/')+' 范围')
        }
        if(-not $spec.ContainsKey('GameFrameGen') -or $spec.GameFrameGen -eq 'present'){
            $fg=Get-033GameFrameGenEvidence $Root
            if(-not $fg.Present){$reasons+=@('游戏没有自带 DLSS 帧生成（sl.dlss_g.dll / nvngx_dlssg.dll），转接件无事可做')}
        }
        foreach($legacyName in @('nvidia_mfg_bridge.dll','dlssg_to_fsr3_amd_is_better.dll')){
            if((Test-Path -LiteralPath (Join-Path $Root $legacyName)) -and -not($Clean -and (Test-033CleanRemoves (Join-Path $Root $legacyName)))){$reasons+=@('目录里有手工装的旧版转接件 '+$legacyName+'（v0.4x）；请先把它和它的加载器删掉或用当初的工具关掉，再重新安装')}
        }
        if($spec.ContainsKey('RequiredDlls')){
            # The loader and the libraries it imports resolve these from the game folder or the system folder of
            # the loader's bitness; one missing DLL makes the renamed loader fail and the game fail to start.
            $systemDir=Get-033SystemDllDirectory ([string]$loader.Architecture)
            # Read-033ManagedPackage has verified these payload hashes and PE
            # architectures. This same enabled component supplies them before
            # launch; they need not already exist on the player's machine.
            $providedNames=@($mine|Where-Object {
                $_.ContainsKey('Role') -and $_.Role -eq 'fg-bridge-dependency' -and
                $_.ContainsKey('Architecture') -and $_.Architecture -eq $loader.Architecture -and
                ([string]$_.Target -notmatch '[\\/]')
            }|ForEach-Object {[string]$_.Target})
            $missing=@(@($spec.RequiredDlls)|Where-Object {$providedNames -inotcontains $_ -and -not(Test-Path -LiteralPath (Join-Path $Root $_)) -and -not(Test-Path -LiteralPath (Join-Path $systemDir $_))})
            if($missing.Count){$reasons+=@('游戏目录和系统目录（'+$systemDir+'）里都找不到 '+($missing -join '、')+'：转接件的加载器和它带的 Intel 库要靠它们加载，缺了游戏会启动失败；MSVCP140/VCRUNTIME140 系列来自微软 Visual C++ 2015–2022 x64 运行库，D3DCOMPILER_47.dll 是 Windows 自带组件，补齐后重新安装')}
        }
        if(-not $reasons.Count){
            $names=@($spec.LoaderNames);$previous=$null
            if($OldTarget -and $OldTarget.ContainsKey('Components')){
                $p=@($OldTarget.Components|Where-Object {$_.Name -ceq $name -and $_.Installed -and $_.Loader})
                if($p.Count){$previous=[string]$p[0].Loader}
            }
            $routeTargets=@($copy.Files|Where-Object {-not($_.ContainsKey('Component') -and $_.Component -ceq $name)}|ForEach-Object {[string]$_.Target})
            if($previous -and $names -icontains $previous){$choice=$previous}
            else{
                foreach($n in $names){
                    if($routeTargets -icontains $n){continue}
                    $path=Join-Path $Root $n
                    if(Test-Path -LiteralPath ($path+'.dlss5-off')){continue}
                    if(Test-Path -LiteralPath $path){if((Get-033ProxyIdentity $path).Kind -eq 'nvidia-mfg-bridge'){$choice=$n;break};if($Clean -and (Test-033CleanRemoves $path)){$choice=$n;break};continue}
                    $choice=$n;break
                }
            }
            if(-not $choice){$reasons+=@('加载器候选名 '+($names -join '、')+' 都被别的模组占用')}
        }
        $installed=(-not $reasons.Count)
        if($installed){$loader.Target=$choice}
        else{$copy.Files=@($copy.Files|Where-Object {-not($_.ContainsKey('Component') -and $_.Component -ceq $name)})}
        $decisions+=@(@{Name=$name;Label=[string]$spec.Label;Installed=$installed;Loader=$choice;LoaderHash=$(if($installed){[string]$loader.Hash}else{$null});
            RuntimeFiles=@(if($installed){@($spec.RuntimeFiles|Where-Object {$_})});Reasons=@($reasons)})
    }
    return @{Profile=$copy;Decisions=$decisions}
}
function New-033ManagedPlan([string[]]$GameExe,$Package,[switch]$ForceFeeder,[string]$Profile,$OldState,[string]$Proxy,$Survey=$null,[switch]$Manual,[switch]$Clean){
    $targets=@();$actions=@();$entries=@();$seen=@{};$planWarnings=@()
    $planCleaned=@();$planRemoveDirs=@();$planUnverified=@();$planCleanedDirs=@()
    if($OldState -and $OldState.ContainsKey('CleanedDirectories')){$planCleanedDirs=@($OldState.CleanedDirectories)}
    foreach($exe in $GameExe){
        $full=[IO.Path]::GetFullPath($exe);$installRoot=Get-033InstallRoot $full;$root=$installRoot.Root
        # V3.4: a route the person picked by hand is kept on update; automatic matching already refused this game once.
        $manualTarget=$null;if($OldState){$manualTarget=@($OldState.Targets|Where-Object {$_.Exe -ieq $full -and $_.ContainsKey('Manual') -and $_.Manual})|Select-Object -First 1}
        $useManual=[bool]$Manual;$useProfile=$Profile
        if(-not $useManual -and -not $useProfile -and $manualTarget){
            # 2026-09-12 dd2-x64 并进 native-re-x64：上次手动选的路线新包里没有了，就退回自动选路并说一声，不卡住更新。
            if(@($Package.Data.Profiles|Where-Object {$_.Id -ceq [string]$manualTarget.Profile}).Count){$useManual=$true;$useProfile=[string]$manualTarget.Profile}
            else{$planWarnings+=@('上次手动选的路线 '+[string]$manualTarget.Profile+' 新包里已经没有了（并进了别的路线），这次按自动选路装；卸载照样原样还原。')}
        }
        $selected=Select-033ManagedProfile $Package $full -ForceFeeder:$ForceFeeder -Profile $useProfile -Manual:$useManual
        # 更新时本次没再指定挂载点, 就沿用上次装的那个 —— 否则一次更新会把入口悄悄
        # 搬回默认名, 旧名那份被当成"被移除的路线文件"还原掉, 游戏下次启动就没人挂载了。
        $vulkanLayer=Test-033VulkanProfile $selected
        # Vulkan is scoped by the actual executable, not DLL search redirections.
        if($vulkanLayer){$root=Split-Path -Parent $full;$installRoot=@{Root=$root;Target=$null}}
        if($seen.ContainsKey($root)){throw '同一目录不能重复选多个入口'};$seen[$root]=$true
        $mount=$Proxy
        if($vulkanLayer -and $Proxy){throw 'Vulkan uses its registered layer, not a system DLL proxy'}
        if(-not $vulkanLayer){
        if(-not $mount -and $OldState){
            $prev=@($OldState.Targets|Where-Object {$_.Exe -ieq $full -and $_.ContainsKey('Proxy') -and $_.Proxy})
            if($prev.Count){$mount=$prev[0].Proxy}
        }
        # 2026-09-12 回退到 5.0：没指定挂载点时按 5.0 的规则挑（Unity 挂 d3d11、逐游戏表挂 d3d12，安全禁令优先）。
        # Reuse a verified prior automatic mount; explicit Proxy keeps its existing semantics.
        $autoMount=$null
        if(-not $mount){$autoMount=Get-033RetainedAutoMount $full $selected $root ([string]$Package.Root) $Survey $OldState}
        if(-not $mount -and -not $autoMount){$autoMount=Get-033AutoMount $full $selected $root ([string]$Package.Root)}
        $selected=Set-033ProxyMount $selected $(if($mount){$mount}elseif($autoMount){$autoMount.Proxy}else{$null})
        # OptiScaler 链式（Fable 2026-09-17）：挂在 ReShade64/32.dll 上的路线要一并合并 OptiScaler.ini，更新沿用旧挂载名时也一样。
        $chainMountName=$(if($mount){[string]$mount}elseif($autoMount){[string]$autoMount.Proxy}else{''})
        if(($autoMount -and $autoMount.ContainsKey('Chain') -and $autoMount.Chain -eq 'optiscaler') -or ($chainMountName -match '(?i)^ReShade(32|64)\.dll$')){$selected=Add-033OptiChainFile $selected $Package}
        # 账本沿用下来的挂载点也要过这道: 游戏更新后可能新带了 Agility SDK / Streamline。
        if($mount){Assert-033ProxyAllowedHere $root $full $mount;Assert-033ProxyNotForeign $root $mount}
        if($autoMount){$planWarnings+=@($autoMount.Warning)}
        }else{$mount=$null;$autoMount=$null}
        if($installRoot.Target){$planWarnings+=@('逐游戏名单（ReShade 官方兼容名单）：这个游戏从子目录 '+$installRoot.Target+' 加载 DLL，033 装到那里。')}
        $oldTarget=$null;if($OldState){$oldTarget=@($OldState.Targets|Where-Object {$_.Exe -ieq $full})|Select-Object -First 1}
        $companions=Add-033ApiCompanions $selected $Package $full $root $OldState
        $selected=$companions.Profile;$planWarnings+=@($companions.Warnings)
        $resolved=Resolve-033ManagedComponents $selected $root $Survey $oldTarget -Clean:$Clean;$selected=$resolved.Profile
        $record=@{Root=$root;Exe=$full;Hash=Get-033FileHash $full;Profile=$selected.Id}
        if($vulkanLayer){$record['VulkanLayer']=Get-033VulkanIdentity $full $root}
        if($useManual){$record['Manual']=$true}
        if(@($resolved.Decisions).Count){$record['Components']=@($resolved.Decisions)}
        if(@($companions.Decisions).Count){$record['ApiCompanions']=@($companions.Decisions)}
        if($mount){$record['Proxy']=$mount}
        # 记下这次到底挂在哪个名字上。默认路线也记 —— 装完只说"路线=…"的话, 用户没法
        # 知道该去看哪个文件, 换过挂载点之后尤其。
        $entryTargets=@($selected.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'entry'}|ForEach-Object {$_.Target})
        if($entryTargets.Count){$record['Mount']=($entryTargets -join ' + ')}
        $targets+=@($record)
        $ri=$targets.Count-1
        # RE Engine loads the host from its _storage_ mirror, and the fork loads the core from beside its
        # own module. Installing only the game root left 鬼武者 with the panel and no core on 2026-09-11
        # (ReShade.log: stage=load_core ..\_storage_\033-runtime\033-engine.dll win32=126). The mirror
        # copies loose DLLs only, never folders, so put 033-runtime there too - but only where the game
        # already keeps such a directory.
        $planFiles=@($selected.Files)
        foreach($mirror in @(Get-033MirrorDirectories $selected $root)){
            foreach($file in @($selected.Files|Where-Object {[string]$_.Target -match '(?i)^033-runtime[\\/]'})){
                $mirrored=Copy-033ManagedObject $file;$mirrored.Target=$mirror+'/'+[string]$file.Target
                $planFiles+=@($mirrored)
            }
        }
        $planFiles+=@(Get-033OwnedMirrorComponents $selected $root $OldState $ri)
        foreach($f in $planFiles){
            $current=Get-033ManagedSnapshot $root $f.Target
            $old=@();if($OldState){$old=@($OldState.Entries|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $f.Target})}
            if($old.Count -gt 1){throw '旧账本重复目标'}
            if($selected.Id -eq 'yanyun-exclusive-x64' -and $f.Policy -eq 'replace' -and
               $f.ContainsKey('Role') -and $f.Role -eq 'crt' -and
               $f.Target -match '^(?i:msvcp140(?:_1|_2|_atomic_wait|_codecvt_ids)?|vcruntime140(?:_1|_threads)?)\.dll$' -and
               $old.Count -and $old[0].Policy -eq 'seed'){
                # S2 registered app-local CRTs as optional seeds for 20/30-series.
                # These are binary dependencies, not settings. S3 replaces the
                # matched runtime while preserving the first-original snapshot.
                $crtEntry=Copy-033ManagedObject $old[0];$crtEntry.Policy='replace';$old=@($crtEntry)
            }
            if($f.ContainsKey('OwnedMirror') -and $f.OwnedMirror -and $old.Count){
                # It was registered as runtime output in old packages. Its exact
                # bytes have just been tied to the old root component; migrate to
                # replace without changing the first-original snapshot.
                $ownedMirror=Copy-033ManagedObject $old[0];$ownedMirror.Policy='replace';$ownedMirror.Current=$current
                $old=@($ownedMirror)
            }
            if($old.Count -and $f.ContainsKey('SeedMerge')){
                # Older installers recorded these INIs as replace. Their user
                # edits are still settings, not foreign replacement binaries.
                $ownedConfig=Copy-033ManagedObject $old[0];$ownedConfig.Policy='seed';$old=@($ownedConfig)
            }
            $vkGenerated=Get-033VulkanGeneratedFile $selected $full $root $f
            if($vkGenerated){
                $entry=@{Root=$ri;Path=$f.Target;Original=$current;OriginalBlob=$null;Current=$null;Policy='replace'}
                if($old.Count){$entry=Copy-033ManagedObject $old[0]}
                $entries+=@($entry)
                $actions+=@(@{Root=$ri;Path=$f.Target;Before=$current;After=$vkGenerated.After;Source=$null;GeneratedBytes=$vkGenerated.Bytes;OriginalEntry=$entry})
                continue
            }
            $routeMerge=Get-033RouteSeedMerge $f $root $Package.Root $current $selected
            if($routeMerge){
                $entry=@{Root=$ri;Path=$f.Target;Original=$current;OriginalBlob=$null;Current=$null;Policy=$f.Policy}
                if($old.Count){$entry=Copy-033ManagedObject $old[0]}
                $entries+=@($entry)
                $actions+=@(@{Root=$ri;Path=$f.Target;Before=$current;After=$routeMerge.After;Source=$null;GeneratedBytes=$routeMerge.Bytes;ReadInput=$routeMerge.ReadInput;OriginalEntry=$entry})
                $planWarnings+=@('已按本次路线迁移 '+$f.Target+' 的 033 必需处理配置；旧文件原样备份，其他参数保留。')
                continue
            }
            if($old.Count -and (Test-033MutableEntry $old[0]) -and $current.Exists){
                # Preserve runtime-edited settings across updates. Uninstall
                # archives their latest content outside the game before cleaning.
                $entries+=@(Copy-033ManagedObject $old[0]);continue
            }
            if($old.Count -and -not(Test-033ManagedSnapshot $current $old[0].Current -HashOnly)){
                # 2026-09-11 回退到 5.0：不再停止覆盖。丢了就重写；被还原成原件就照常覆盖；是别人的就先当新原件备份，卸载时放回这份。
                if(-not $current.Exists){$planWarnings+=@('已安装的 '+$f.Target+' 不见了（多半被杀毒软件删了），这次重新写入；卸载时照样放回最初的原件。')}
                # 已经是新包里这个组件的字节（手工换成了新核心之类）：是 033 自己的，不收作原件（业主：跨次按哈希认归属）。
                elseif(([string]$current.Hash -ine [string]$f.Hash) -and -not(Test-033ManagedSnapshot $current $old[0].Original -HashOnly)){
                    $adopt=Copy-033ManagedObject $old[0];$sup=@();if($adopt.ContainsKey('SupersededOriginals')){$sup=@($adopt.SupersededOriginals)}
                    $sup+=@(@{Original=$adopt.Original;OriginalBlob=$adopt.OriginalBlob;At=[DateTime]::UtcNow.ToString('o')})
                    $adopt['SupersededOriginals']=$sup;$adopt.Original=$current;$adopt.OriginalBlob=$null;$old=@($adopt)
                    $planWarnings+=@($f.Target+' 已经被别的程序或手工换过（不是 033 装的那份）：先把它当新原件备份进备份库再装；卸载时放回的是这份。')
                }
            }
            if(-not $old.Count -and $f.Policy -eq 'seed' -and $current.Exists){
                # A pre-existing seed is retained, not written. Register its baseline like a
                # runtime file so uninstall archives the current content and restores the original.
                $entry=@{Root=$ri;Path=$f.Target;Original=$current;OriginalBlob=$null;Current=$current;Policy='seed'}
                $entries+=@($entry)
                $actions+=@(@{Root=$ri;Path=$f.Target;Before=$current;After=$current;Source=(Get-033Path $root $f.Target);OriginalEntry=$entry;NoWrite=$true})
                continue
            }
            $src=Get-033Path $Package.Root $f.Source;$after=Get-033ManagedSnapshot $Package.Root $f.Source
            $after.Identity=$null
            $entry=@{Root=$ri;Path=$f.Target;Original=$current;OriginalBlob=$null;Current=$null;Policy=$f.Policy}
            if($old.Count){$entry=Copy-033ManagedObject $old[0]}
            elseif($f.ContainsKey('OwnedMirror') -and $f.OwnedMirror){$entry.Original=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}}
            $entries+=@($entry)
            $actions+=@(@{Root=$ri;Path=$f.Target;Before=$current;After=$after;Source=$src;OriginalEntry=$entry})
        }
        # ★装之前把旧版 033 的足迹清场★ 2026-09-12 业主：「V6 能删掉 V5 之前的所有东西吗？」
        #   老包卸载器还在时走 handover（先静默跑它再装）。可只要卸载器被删了、游戏挪过目录、
        #   或者本来就是手工装的，以前这里只说一句「其余残留不动」—— 旧核心、旧 addon、旧 cfg
        #   全留在原地跟新的打架。第二轮闪烁调查就吃过一次：5.0 的 renodx-dlss5.addon64 在场时
        #   6.0 核心主动不建 NR，而 6.0 的 ReShade 分支又不加载它，两边都没有 NR。
        #   ★但不能照抄 5.0 的删法★：5.0 是【按固定文件名】删，把游戏自己的 libxess_fg / libxell /
        #   ReShade.ini / reshade-shaders 一并删掉，RE9 和刺客信条影就是这么坏的。这里按
        #   【是不是我们的】删：
        #     1 只可能是 033 的名字（游戏不会取这些名字）→ 清，卸载时放回；
        #     2 033 专用目录里的东西 → 整棵清，只归档不回填（那两个目录卸载时必须是空的，
        #       见 Get-033CleanupInventory；放回去反而会被同一次卸载再删一遍）；
        #     3 会撞名的挂载名 → 只有文件里带 033 自己的导出标记（K033_ReShadeEntry /
        #       K033_FeederEntry，即 Get-033ProxyIdentity 判成 033-fork）才算我们的才清；
        #       别人的 ReShade / OptiScaler / ENB / SpecialK 一个不动。
        #   清掉的东西由事务层自动抄进备份库（Before 存在就写 BeforeBlob），一件都不会真没了。
        $skip=@{}
        foreach($f in $planFiles){$skip[([string]$f.Target).ToLowerInvariant().Replace('\','/')]=$true}
        foreach($e in $entries){if($e.Root -eq $ri){$skip[([string]$e.Path).ToLowerInvariant().Replace('\','/')]=$true}}
        # 旧账本登记过的路径由后面「移除的路线文件回到最初原件」那段处理，这里不能再加一份动作。
        if($OldState){foreach($e in $OldState.Entries){if($e.Root -eq $ri){$skip[([string]$e.Path).ToLowerInvariant().Replace('\','/')]=$true}}}
        # 033 自己的运行产物（dlss5-033.cfg / .state / 各种日志 / *.dlss5-off 那些挪开的原件）归
        # Add-033RuntimePlan 管：它在本段【之后】才跑，会把它们登记成保留项，卸载时归档再删。
        # 清场碰它们等于把玩家调好的设置、和被挪开的游戏原件一起清掉，所以整份排除。
        foreach($n in @((Get-033RuntimeFootprint $selected $root).Files)){$skip[([string]$n).ToLowerInvariant().Replace('\','/')]=$true}
        $residue=@()
        foreach($name in @('_安装记录.txt','_033-integrated.json','dlss5-033.addon64','dlss5-feed.addon64',
                           'dlss5-feed.addon32','renodx-dlss5.addon64')){
            if($skip.ContainsKey($name.ToLowerInvariant())){continue}
            if(Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf){$residue+=@(@{Path=$name;Restore=$true})}
        }
        # 目录本身是 033 建的，但【里面的东西不一定都是我们的】：跨位宽套件就在 033-runtime 里放了一个
        # foreign.txt，合同是「装完卸完目录一字不差」。所以 033-runtime 只清名字带 033 / dlss5- 的
        # （旧核心、旧转发器、旧补帧提供者、旧模型都在这套命名里），别人放进来的东西一律不碰；
        # _033transactions 是 033 自己的事务日志目录，只有 033 会往里写，整个清。
        foreach($dir in @(@{Name='033-runtime';OursOnly=$true},@{Name='_033transactions';OursOnly=$false})){
            $dp=Join-Path $root $dir.Name
            if(-not(Test-Path -LiteralPath $dp -PathType Container)){continue}
            foreach($child in @(Get-ChildItem -LiteralPath $dp -Force -File -Recurse -ErrorAction SilentlyContinue)){
                # 连接点/符号链接不碰：Assert-033ManagedEntry 会直接拒，整趟安装就废了。
                if([int]$child.Attributes -band [int][IO.FileAttributes]::ReparsePoint){continue}
                if($dir.OursOnly -and [string]$child.Name -notmatch '(?i)033|^dlss5-'){continue}
                $rel=$child.FullName.Substring($root.Length+1).Replace('\','/')
                if($skip.ContainsKey($rel.ToLowerInvariant())){continue}
                $residue+=@(@{Path=$rel;Restore=$false})
            }
        }
        foreach($alias in @('dxgi.dll','d3d12.dll','d3d11.dll','d3d10.dll','d3d9.dll','dinput8.dll','winmm.dll','version.dll','opengl32.dll')){
            if($skip.ContainsKey($alias)){continue}
            $ap=Join-Path $root $alias
            if(-not(Test-Path -LiteralPath $ap -PathType Leaf)){continue}
            if((Get-033ProxyIdentity $ap).Kind -ne '033-fork'){continue}
            $residue+=@(@{Path=$alias;Restore=$true})
        }
        $purged=@()
        foreach($item in $residue){
            if(@($entries|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $item.Path}).Count){continue}
            $lnow=Get-033ManagedSnapshot $root $item.Path
            if(-not $lnow.Exists){continue}
            $gone=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}
            if($item.Restore){
                $lentry=@{Root=$ri;Path=$item.Path;Original=$lnow;OriginalBlob=$null;Current=$null;Policy='replace'}
                $entries+=@($lentry)
                $actions+=@(@{Root=$ri;Path=$item.Path;Before=$lnow;After=$gone;Source=$null;OriginalEntry=$lentry})
            }else{
                $actions+=@(@{Root=$ri;Path=$item.Path;Before=$lnow;After=$gone;Source=$null;OriginalEntry=$null})
            }
            $purged+=@([string]$item.Path)
        }
        if($purged.Count){
            $planWarnings+=@('清掉了旧版 033 留在这个目录里的 '+$purged.Count+' 个文件（全部先抄进了备份库，卸载时该放回的放回）：'+
                ((@($purged)|Select-Object -First 6) -join '、')+$(if($purged.Count -gt 6){' 等'}else{''})+
                $(if($Clean){'。'}else{'。别人的模组（ReShade / OptiScaler / ENB / SpecialK 这些）一个没动。'}))
        }
        if($Clean){
            # S31 净化后安装（game_clean.ps1）：不属于游戏的文件在同一个事务里移进备份库，Original 就是现在这份，
            # 「还原安装前」原样放回。本包要装的、账本里的 033 文件、运行产物和上面已清的旧 033 残留都不动；
            # 上次净化过、之后又放回来的，这次再清一遍，账本里的首次原件不变。
            $cleanSkip=@{};foreach($cKey in @($skip.Keys)){$cleanSkip[$cKey]=$true}
            $oldCleaned=@{}
            if($OldState){foreach($cOld in @($OldState.Entries)){if($cOld.Root -eq $ri -and $cOld.ContainsKey('Cleaned') -and $cOld.Cleaned){$cKey=([string]$cOld.Path).ToLowerInvariant().Replace('\','/');$oldCleaned[$cKey]=$cOld;[void]$cleanSkip.Remove($cKey)}}}
            foreach($cAct in @($actions|Where-Object {$_.Root -eq $ri})){$cleanSkip[([string]$cAct.Path).ToLowerInvariant().Replace('\','/')]=$true}
            foreach($cEnt in @($entries|Where-Object {$_.Root -eq $ri})){$cleanSkip[([string]$cEnt.Path).ToLowerInvariant().Replace('\','/')]=$true}
            $cleanGone=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}
            $cleanScan=Get-033CleanCandidates $root $cleanSkip $full
            $cleanedHere=@();$cleanStuck=@()
            foreach($cItem in @($cleanScan.Found)){
                $cNow=$null
                try{$cNow=Get-033ManagedSnapshot $root $cItem.Path}catch{$cleanStuck+=@([string]$cItem.Path);continue}
                if(-not $cNow.Exists){continue}
                $cKey=([string]$cItem.Path).ToLowerInvariant()
                if($oldCleaned.ContainsKey($cKey)){
                    $cEntry=Copy-033ManagedObject $oldCleaned[$cKey]
                    $entries+=@($cEntry)
                    $actions+=@(@{Root=$ri;Path=[string]$cEntry.Path;Before=$cNow;After=$cleanGone;Source=$null;OriginalEntry=$null})
                }else{
                    $cEntry=@{Root=$ri;Path=[string]$cItem.Path;Original=$cNow;OriginalBlob=$null;Current=$null;Policy='replace';Cleaned=[string]$cItem.Reason}
                    $entries+=@($cEntry)
                    $actions+=@(@{Root=$ri;Path=[string]$cItem.Path;Before=$cNow;After=$cleanGone;Source=$null;OriginalEntry=$cEntry})
                }
                $cleanedHere+=@([string]$cItem.Path);$planCleaned+=@(@{Root=$ri;Path=[string]$cItem.Path;Reason=[string]$cItem.Reason})
            }
            foreach($cDir in @(Get-033CleanableDirectories $root $cleanedHere)){
                $planRemoveDirs+=@(@{Root=$ri;Path=[string]$cDir})
                if(-not @($planCleanedDirs|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $cDir}).Count){$planCleanedDirs+=@(@{Root=$ri;Path=[string]$cDir})}
            }
            $cleanName=Split-Path -Leaf $root
            if($cleanedHere.Count){$planWarnings+=@('净化（'+$cleanName+'）：把不属于游戏的 '+$cleanedHere.Count+' 个文件移进了备份库，「还原安装前」原样放回：'+((@($cleanedHere)|Select-Object -First 8) -join '、')+$(if($cleanedHere.Count -gt 8){' 等'}else{''})+'。')}
            else{$planWarnings+=@('净化（'+$cleanName+'）：没有不属于游戏的文件。')}
            if($cleanStuck.Count){$planWarnings+=@('净化（'+$cleanName+'）：这几个文件移不了（多半是模组管理器用硬链接放进来的），没有动：'+($cleanStuck -join '、')+'。请用放它们进来的工具卸掉。')}
            foreach($cOdd in @($cleanScan.Unverified)){$planUnverified+=@(@{Root=$ri;Path=[string]$cOdd.Path;Signer=[string]$cOdd.Signer;Status=[string]$cOdd.Status})}
            if(@($cleanScan.Unverified).Count){$planWarnings+=@('净化（'+$cleanName+'）：这几个文件带着游戏厂商的签名、但签名验不过（可能被改过或损坏），没有动：'+((@($cleanScan.Unverified)|ForEach-Object {[string]$_.Path+'（'+[string]$_.Status+'）'}) -join '、')+'。游戏不正常的话，用游戏启动器的「修复」换回原版。')}
        }
    }
    $runtimePlan=@{Targets=$targets;Actions=$actions;Entries=$entries}
    Add-033RuntimePlan $runtimePlan $Package $OldState
    $actions=@($runtimePlan.Actions);$entries=@($runtimePlan.Entries)
    if($OldState){
        if(($targets.Exe -join '|') -ine ($OldState.Targets.Exe -join '|')){throw '更新不能改变已安装的入口组'}
        foreach($old in $OldState.Entries){
            if(@($entries|Where-Object {$_.Root -eq $old.Root -and $_.Path -ieq $old.Path}).Count){continue}
            $now=Get-033ManagedSnapshot $targets[$old.Root].Root $old.Path
            $movedOut=($old.Original -and $old.Original.Exists -and $old.Current -and -not $old.Current.Exists)
            if($movedOut -or ($old.ContainsKey('Cleaned') -and $old.Cleaned)){
                # S31：033 有意移走的文件（清掉的旧版 033 残留、净化移走的别人的文件）留在备份库，直到「还原安装前」
                # 放回；升级不放回。S24–S30 在这里把残留放了回来、账本也丢了这一条，下一次安装再清一遍，来回折腾
                # （S31 夹具抓到：旧 dlss5-033.addon64 升级后又回到游戏目录）。净化过的位置后来装过 033 的文件
                # （比如只给 20/30 系装的转接件）这次不装了，就删掉，回到净化后的样子；别的东西不动。
                $entries+=@(Copy-033ManagedObject $old)
                if($now.Exists -and $old.Current -and $old.Current.Exists -and (Test-033ManagedSnapshot $now $old.Current -HashOnly)){
                    $actions+=@(@{Root=$old.Root;Path=$old.Path;Before=$now;After=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null};Source=$null;OriginalEntry=$null})
                }
                continue
            }
            if(Test-033RemovalForeign $old $now){
                $keptEntry=Copy-033ManagedObject $old;$keptEntry.Current=$now;$keptEntry['LeftForeign']=$true;$entries+=@($keptEntry)
                $planWarnings+=@($old.Path+' 已经不是 033 装的那份，这次不动它（原件仍在备份库）。');continue
            }
            # Removed route files go back to their FIRST original, not deletion by name.
            $actions+=@(@{Root=$old.Root;Path=$old.Path;Before=$now;After=$old.Original;Source=$old.OriginalBlob;OriginalEntry=$null;FromVault=$true})
        }
    }
    $result=@{Targets=$targets;Actions=$actions;Entries=$entries;PackageHash=$Package.Hash;Version=$Package.Data.Version;CleanupTrees=$runtimePlan.CleanupTrees;Warnings=$planWarnings}
    if($Clean){$result['Cleaned']=@($planCleaned)}
    if($planCleanedDirs.Count){$result['CleanedDirectories']=@($planCleanedDirs)}
    if($planRemoveDirs.Count){$result['RemoveDirectories']=@($planRemoveDirs)}
    if($planUnverified.Count){$result['Unverified']=@($planUnverified)}
    Add-033VulkanPlan $result $OldState
    return $result
}
function Assert-033ManagedState($State,[string]$Folder){
    if(-not $State -or $State.Schema -ne 1 -or $State.Id -notmatch '^[a-f0-9]{32}$' -or $State.Status -notin @('installed','restored') -or $State.Targets.Count -lt 1 -or $State.Targets.Count -gt 16){throw '安装账本格式无效'}
    if((Get-033Path $State.Vault ('groups/'+$State.Id)) -ine $Folder){throw '账本不属于指定备份库'}
    $seen=@{}
    foreach($e in $State.Entries){
        $path=Assert-033ManagedEntry $State $e
        if($seen.ContainsKey($path)){throw '安装账本重复文件'};$seen[$path]=$true
        if($e.Original.Exists){
            if(-not $e.OriginalBlob -or (Get-033FileHash (Get-033Path $Folder $e.OriginalBlob)) -ine $e.Original.Hash){throw "首次原件备份缺失或损坏: $($e.Path)"}
        }elseif($e.OriginalBlob){throw '原本不存在的文件不能有原件备份'}
    }
}
function Test-033LegacyInstallation([string]$Root){
    # ReShade/shaders alone are NEVER historical 033 ownership.
    foreach($name in @('_033-integrated.json','_安装记录.txt','_033transactions','033-runtime','dlss5-033.addon64','dlss5-feed.addon64','dlss5-feed.addon32')){
        if(Test-Path -LiteralPath (Get-033Path $Root $name)){return $true}
    }
    return $false
}
function Remove-033ManagedEmptyDirectories($Journal,$Gates){
    # Dispose temporary guards before checking emptiness. Never recursively delete.
    $finishing=($Journal.Action -eq 'Restore' -and $Journal.Status -eq 'committed')
    if($finishing){
        $latest=Get-033CleanupInventory $Journal.Group $Gates
        if($latest.Files.Count){throw "033目录仍有文件，清理尚未完成: $($latest.Files[0].Path)"}
        $Journal.CreatedDirectories=@($Journal.CreatedDirectories+$latest.Directories|Select-Object -Unique)
    }
    foreach($path in @($Journal.CreatedDirectories|Sort-Object Length -Descending|Select-Object -Unique)){
        $root=@($Journal.Group.Targets|Where-Object {$path.StartsWith($_.Root+'\',[StringComparison]::OrdinalIgnoreCase)})
        if($root.Count -ne 1){throw '空目录清理越过目标边界'}
        $relative=$path.Substring($root[0].Root.Length+1);[void](Get-033Path $root[0].Root $relative)
        if($Gates.Directories.ContainsKey($path)){$Gates.Directories[$path].Dispose();$Gates.Directories.Remove($path)}
        if([IO.Directory]::Exists($path) -and @(Get-ChildItem -LiteralPath $path -Force).Count -eq 0){[IO.Directory]::Delete($path,$false)}
    }
    if($finishing){
        $remaining=Get-033CleanupInventory $Journal.Group $Gates
        if($remaining.Files.Count -or $remaining.Directories.Count){throw '033专用目录仍有残留，清理尚未完成'}
    }
}
function Invoke-033ManagedOperation {
    param([ValidateSet('Plan','Install','Restore','Recover','Survey')][string]$Action,
          [string[]]$GameExe,[string]$PackageRoot,[switch]$ForceFeeder,[string]$Profile,
          [string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer'),
          [switch]$AllowNativeDlss,[switch]$HandoverOld,[string]$Proxy,[switch]$Manual,[switch]$Clean)
    if(-not $GameExe.Count -or $GameExe.Count -gt 16){throw '请指定1至16个准确游戏入口'}
    # 名字先验, 连 Survey 也验 —— 打错字要当场说, 不能等到写文件那一步。
    if($Proxy){$Proxy=Assert-033ProxyName $Proxy}
    $Vault=Get-033ManagedRoot $Vault
    # V3.1: a game folder is accepted and resolved to its executable (this
    # installer's own index first, then the bounded folder scan); the resolution is
    # shown in the report and returned, never hidden.
    $resolutions=[Collections.Generic.List[object]]::new();$resolvedExe=[Collections.Generic.List[string]]::new()
    foreach($given in $GameExe){
        $full=[IO.Path]::GetFullPath($given)
        # 文件夹 → 主程序；虚幻引擎根目录的启动器 → Binaries\Win64 里的 -Shipping.exe（v5.0 的规则，见 Resolve-033GameExe）。
        if((Test-Path -LiteralPath $full -PathType Container) -or (Test-Path -LiteralPath $full -PathType Leaf)){
            $r=Resolve-033GameExe $full $Vault
            if($r.FromFolder -or $r.Source -in @('ue-shipping','launcher-redirect')){$resolutions.Add($r)}
            $resolvedExe.Add($r.Exe)
        }else{$resolvedExe.Add($full)}
    }
    $GameExe=@($resolvedExe|Sort-Object -Unique)
    $GameExe=@(Get-033YanYunTargets $GameExe -IncludeSiblings:($Action -in @('Plan','Install','Survey'))) 
    # S39：国际版（wwm.exe）的目录还没逐个核对过，「净化后安装」先不开放；安装 / 升级 / 还原照常。
    if($Clean -and $Action -in @('Plan','Install')){Assert-033YanYunCleanSupported $GameExe}
    # 2026-09-17 Fable（5.0 并列目录）：只给了一个入口、又还没被本安装器登记过时，同一游戏的并列二进制目录一起进同一个安装组。
    $twinText=''
    if($Action -in @('Plan','Install','Survey') -and $GameExe.Count -eq 1){
        $twinExisting=$null;try{$twinExisting=Read-033ManagedGroup $Vault $GameExe[0]}catch{$twinExisting=$null}
        if(-not $twinExisting){
            $twins=@();try{$twins=@(Get-033TwinEntries $GameExe[0] $Vault)}catch{$twins=@()}
            if($twins.Count){$GameExe=@(@($GameExe)+@($twins)|Sort-Object -Unique);$twinText=('== 同一游戏的并列二进制目录 =='+[Environment]::NewLine+'也会一起安装（5.0 的做法：一起装、一起卸）：'+(($twins|ForEach-Object {Split-Path -Parent $_}) -join '、')+[Environment]::NewLine)}
        }
    }
    # 挂载点禁令在侦察之前就查: 填错了当场说, 不要等用户看完报告、确认完提醒才在写文件那步拒绝。
    if($Proxy -and $Action -in @('Plan','Install','Survey')){foreach($exe in $GameExe){Assert-033ProxyAllowedHere (Split-Path -Parent $exe) $exe $Proxy;Assert-033ProxyNotForeign (Get-033InstallRoot $exe).Root $Proxy}}
    $resolutionText='';foreach($r in $resolutions){$resolutionText+=(Format-033Resolution $r)}
    if($twinText){$resolutionText+=$twinText}
    if($Action -in @('Plan','Install')){Assert-033ManagedVaultCapacity $Vault}
    foreach($exe in $GameExe){
        $root=Get-033ManagedRoot (Split-Path -Parent $exe)
        if($Vault -ieq $root -or $Vault.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($Vault+'\',[StringComparison]::OrdinalIgnoreCase)){throw '备份库必须位于游戏目录之外'}
    }
    # Plan is read-only. Package validation precedes any old installation mutation.
    $package=if($Action -in @('Plan','Install') -or ($Action -eq 'Survey' -and $PackageRoot)){Read-033ManagedPackage $PackageRoot}else{$null}
    # V3: survey first, judge second, only then plan. Survey is read-only.
    $survey=$null;$verdict=$null;$report=$null;$handover=$null
    if($Action -in @('Plan','Install','Survey')){
        $survey=Get-033GameSurvey $GameExe[0] $Vault
        if($package){$verdict=Get-033InstallVerdict $survey $package -ForceFeeder:$ForceFeeder -Profile $Profile -AllowNativeDlss:$AllowNativeDlss -HandoverOld:$HandoverOld -Manual:$Manual -Proxy $Proxy}
        if($Clean -and $verdict){Update-033CleanVerdictText $verdict $survey}
        $report=$resolutionText+(Format-033SurveyReport $survey $verdict)
        if($Action -eq 'Survey'){return @{Status='surveyed';Survey=$survey;Verdict=$verdict;Report=$report;Message=$report;Resolution=@($resolutions);GameWrites=0}}
        if($verdict.Decision -eq 'refuse'){throw ("不安装：`n"+(($verdict.Reasons|ForEach-Object {'  × '+$_}) -join "`n"))}
        if($verdict.Decision -eq 'handover-then-install'){
            if($Action -eq 'Plan'){return @{Status='handover-required';Message='计划：先用旧版自带卸载器还原，再核对残留，再安装（只读检查未执行还原）';Report=$report;Verdict=$verdict;GameWrites=0}}
            # 2026-09-11 回退到 5.0：自动交接，不再要 -HandoverOld。
            $mountHistory=$survey.Existing.LegacyMount
            $handover=Invoke-033OldPackageHandover $survey $GameExe[0] $Vault
            if($mountHistory){$handover['MountHistory']=$mountHistory}
            $survey=Get-033GameSurvey $GameExe[0] $Vault -NoCache
            if($mountHistory){$survey|Add-Member -NotePropertyName PreviousMount -NotePropertyValue $mountHistory}
            $report=$resolutionText+(Format-033SurveyReport $survey $verdict)+"`n== 已用旧版自带卸载器还原（"+$handover.CompletedAt+"）==`n"+$handover.Output
        }
    }
    $existing=Resolve-033RequestedGroup $Vault $GameExe $Action
    $staleLedgerNotes=@()
    if($existing){
        foreach($exe in $GameExe){if(@($existing.Group.Targets|Where-Object Exe -IEQ $exe).Count -ne 1){throw '所选入口属于不同安装组'}}
        $GameExe=@($existing.Group.Targets.Exe)
        if($existing.State){
            # 2026-09-12 龙之信条2 报障：全新安装被挡死在「首次原件备份缺失或损坏：dxgi.dll」
            # （下面 Assert-033ManagedState 抛的）。可侦察里写着「既有 033：无」—— 游戏目录是干净的，
            # 脏的是备份库里【上一轮已经恢复完】的那份账本：它记的原件早就放回游戏目录了，
            # 账本只剩历史价值。拿一份用完的历史记录去挡一次全新安装是纯阻拦 —— 玩家看不懂那句话，
            # 也没有出路（只能自己去 %LOCALAPPDATA%\033Installer 里翻着删）。
            # 所以：那一轮只要不是【还装着】、而且没有没跑完的事务，就跳过它按全新装；
            # 旧记录原样留在备份库，一个字节不删。仍然装着（installed）的账本坏了才是真问题，照旧拦住。
            try{Assert-033ManagedState $existing.State $existing.Folder}
            catch{
                $pendingBusy=$existing.Pending -and $existing.Pending.Status -notin @('committed','recovered')
                if($existing.State.Status -eq 'installed' -or $pendingBusy){throw}
                $staleLedgerNotes+=@('备份库里上一轮的安装记录已经损坏或不全（'+$_.Exception.Message+'）。那一轮早就恢复完了、原件也放回了游戏目录，这份记录只剩历史，所以跳过它按全新安装处理。旧记录原样留在 '+$existing.Folder+'，一个字节没删。')
                $existing=$null
            }
        }
    }
    if($staleLedgerNotes.Count){$report=[string]$report+"`n== 备份库里的旧记录 ==`n"+($staleLedgerNotes -join "`n")+"`n"}
    if($Action -eq 'Plan'){
        if($existing -and $existing.Pending -and $existing.Pending.Status -notin @('committed','recovered')){throw '上次操作中断；请先恢复该事务，备份仍保留'}
        $old=if($existing -and $existing.State -and $existing.State.Status -eq 'installed'){$existing.State}else{$null}
        return New-033ManagedPlan $GameExe $package -ForceFeeder:$ForceFeeder -Profile $Profile -OldState $old -Proxy $Proxy -Survey $survey -Manual:$Manual -Clean:$Clean
    }
    $mutex=Enter-033TransactionLock $Vault;$gates=$null;$folder=$null;$journal=$null
    try{
        $existing=Resolve-033RequestedGroup $Vault $GameExe $Action
        if($existing){
            $GameExe=@($existing.Group.Targets.Exe)
            if($existing.Pending -and $existing.Pending.Status -notin @('committed','recovered')){
                $gates=Open-033ManagedGates $existing.Group
                # 上次没装完（报错、窗口被关、被杀软打断）。收拾不下去时把上次的原因一起说出来，
                # 别只剩一句收拾时撞到的话 —— 用户照片里就只有那一句，真正的原因看不到。
                try{Restore-033ManagedPending $existing.Folder $existing.Pending $gates}
                catch{
                    $why=$(if($existing.Pending.ContainsKey('Failure') -and $existing.Pending.Failure){'上次的原因：'+$existing.Pending.Failure+'。'}else{'上次可能是窗口被关或程序被打断。'})
                    throw ('上次安装没有完成，自动撤销时停下了。'+$why+'这次停在：'+$_.Exception.Message)
                }
                Remove-033ManagedEmptyDirectories $existing.Pending $gates
                Close-033ManagedGates $gates;$gates=$null
                $existing=Resolve-033RequestedGroup $Vault $GameExe $Action
            }
        }
        if($Action -eq 'Recover'){
            if($existing -and $existing.Pending -and $existing.Pending.Status -eq 'committed'){
                $gates=Open-033ManagedGates $existing.Group
                if($existing.Pending.Operation -notmatch '^operations/[a-f0-9]{32}$'){throw '已提交事务缺少准确操作目录'}
                $receipt=Get-033Path $existing.Folder ($existing.Pending.Operation+'/receipt.json')
                $pendingPath=Get-033Path $existing.Folder 'pending.json'
                if(Test-Path -LiteralPath $receipt){
                    if((Get-033FileHash $receipt) -ine (Get-033FileHash $pendingPath)){throw '已提交回执副本不匹配主记录'}
                }else{Copy-033ManagedBlob $pendingPath $receipt (Get-033FileHash $pendingPath)}
                if($existing.Pending.Action -eq 'Restore'){
                    $existing.Pending.CreatedDirectories=$existing.Group.CreatedDirectories
                    Remove-033ManagedEmptyDirectories $existing.Pending $gates
                    Assert-033NoRuntimeRemainder $existing.Group $gates
                }
                return @{Status=$existing.Group.Status;Message='上次文件事务已提交，已核查后续清理；没有撤销已提交的文件。';Vault=$Vault;Storage=(Complete-033VaultStorage $Vault $existing.Group.Id)}
            }
            return @{Status='recovered';Message='已撤销上次未完成操作；历史备份保留。';Vault=$Vault;Storage=$(if($existing){Complete-033VaultStorage $Vault $existing.Group.Id}else{$null})}
        }
        $old=if($existing -and $existing.State -and $existing.State.Status -eq 'installed'){$existing.State}else{$null}
        if($Action -eq 'Restore' -and -not $old){
            if(-not $existing -and (Test-033LegacyInstallation (Split-Path -Parent $GameExe[0]))){throw '这是旧版或手工安装，缺少新账本；不会执行旧通配卸载。请导入原始恢复证据。'}
            if($existing -and $existing.Pending -and $existing.Pending.Status -eq 'committed' -and $existing.Pending.Action -eq 'Restore'){
                $gates=Open-033ManagedGates $existing.Group
                $existing.Pending.CreatedDirectories=$existing.Group.CreatedDirectories
                Remove-033ManagedEmptyDirectories $existing.Pending $gates
                Assert-033NoRuntimeRemainder $existing.Group $gates
            }
            return @{Status='not-installed';Message='没有本安装器管理的现装版本，目录清理状态已核对。';Vault=$Vault}
        }
        if($old){Assert-033ManagedState $old $existing.Folder}
        if($Action -eq 'Install'){
            # 2026-09-11 回退到 5.0：旧残留没有账本也照样装，本次覆盖的现有文件按原件备份，卸载时原样放回。
            $plan=New-033ManagedPlan $GameExe $package -ForceFeeder:$ForceFeeder -Profile $Profile -OldState $old -Proxy $Proxy -Survey $survey -Manual:$Manual -Clean:$Clean
        }else{
            $plan=New-033UninstallPlan $old
            $plan.Targets=@($plan.Targets|ForEach-Object {Copy-033ManagedObject $_})
            foreach($t in $plan.Targets){if($t.ContainsKey('VulkanLayer')){[void]$t.Remove('VulkanLayer')}}
            Add-033VulkanPlan $plan $old
        }
        $id=if($old){$old.Id}else{[Guid]::NewGuid().ToString('N')}
        $group=@{Schema=1;Id=$id;Vault=$Vault;Targets=$plan.Targets;Entries=$plan.Entries;Version=$plan.Version;PackageHash=$plan.PackageHash;Status=$(if($Action -eq 'Restore'){'restored'}else{'installed'});CreatedDirectories=@()}
        if($old){$group.CreatedDirectories=$old.CreatedDirectories}
        $group['CleanupTrees']=$plan.CleanupTrees
        $group['VulkanLayers']=@($plan.VulkanLayers)
        if($Action -eq 'Install' -and $plan.ContainsKey('CleanedDirectories') -and @($plan.CleanedDirectories).Count){$group['CleanedDirectories']=@($plan.CleanedDirectories)}
        if($plan.ContainsKey('CleanupDirectories')){$group.CreatedDirectories=@($group.CreatedDirectories+$plan.CleanupDirectories|Select-Object -Unique)}
        if(-not $gates){$gates=Open-033ManagedGates $group}
        # Count CAS additions and independent game staging together per volume.
        # Restore needs working space too; never confuse C: vault and E: game budgets.
        $needs=Get-033VaultSpacePlan $Vault $plan $(if($existing){$existing.Folder}else{''})
        $short=@()
        foreach($drive in $needs.Keys){
            $need=[long]$needs[$drive]+16MB
            $free=[IO.DriveInfo]::new($drive).AvailableFreeSpace
            if($free -lt $need){$short+=@($drive+' 还差 '+[math]::Ceiling(($need-$free)/1MB)+' MB')}
        }
        if($short.Count){throw ('磁盘空间不足，尚未写入游戏文件：'+($short -join '；')+'。原件和记录保留。')}
        foreach($a in $plan.Actions){
            if($a.ContainsKey('ReadInput') -and $a.ReadInput -and (Get-033FileHash $a.ReadInput.Path) -ine $a.ReadInput.Hash){throw 'Selected preset changed during planning; no game files written'}
            $p=Assert-033ManagedEntry $group $a
            if(-not(Test-033ManagedSnapshot (Get-033ManagedSnapshot $group.Targets[$a.Root].Root $a.Path) $a.Before)){throw '计划后现场发生变化，未开始写入'}
            if($a.Before.Exists){
                # Preflight real sharing locks without modifying read-only attributes.
                $stream=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$stream.Dispose()
            }
        }
        # All package, occupancy, ownership and target checks passed before vault/index creation.
        [void][IO.Directory]::CreateDirectory((Get-033Path $Vault 'index'))
        $folder=Get-033Path $Vault ('groups/'+$id);[void][IO.Directory]::CreateDirectory($folder)
        $op='operations/'+[Guid]::NewGuid().ToString('N');$opFolder=Get-033Path $folder $op;[void][IO.Directory]::CreateDirectory($opFolder)
        if($report){[IO.File]::WriteAllText((Get-033Path $opFolder '安装报告.txt'),$report,[Text.UTF8Encoding]::new($true))}
        $files=@();$i=0
        if(Get-Command Publish-033Progress -ErrorAction SilentlyContinue){Publish-033Progress 'backup' 0 $plan.Actions.Count}
        foreach($a in $plan.Actions){
            $entry=@{Root=$a.Root;Path=$a.Path;Before=$a.Before;After=$a.After;BeforeBlob=$null;AfterBlob=$null;Applied=$null;Intent=$null;UndoIntent=$null;Recovered=$null;NoWrite=($a.ContainsKey('NoWrite') -and $a.NoWrite)}
            $target=Assert-033ManagedEntry $group $a
            if($a.Before.Exists){$entry.BeforeBlob=$op+'/'+$i+'.before';[void](Copy-033VaultBlob $Vault $target (Get-033Path $folder $entry.BeforeBlob) $a.Before.Hash)}
            if($a.After.Exists -and -not $entry.NoWrite){
                $source=if($a.ContainsKey('FromVault') -and $a.FromVault){Get-033Path $existing.Folder $a.Source}else{$a.Source}
                $entry.AfterBlob=$op+'/'+$i+'.after'
                $afterPath=Get-033Path $folder $entry.AfterBlob
                if($a.ContainsKey('GeneratedBytes')){
                    $stream=[IO.FileStream]::new($afterPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
                    try{$stream.Write($a.GeneratedBytes,0,$a.GeneratedBytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
                    if((Get-033FileHash $afterPath) -ine $a.After.Hash){throw 'Generated route configuration hash mismatch'}
                    [void](Copy-033VaultBlob $Vault $afterPath $afterPath $a.After.Hash -Existing)
                }else{[void](Copy-033VaultBlob $Vault $source $afterPath $a.After.Hash)}
            }
            if($a.OriginalEntry -and -not $a.OriginalEntry.OriginalBlob -and $a.OriginalEntry.Original.Exists){$a.OriginalEntry.OriginalBlob=$entry.BeforeBlob}
            $files+=@($entry);$i++
            if(Get-Command Publish-033Progress -ErrorAction SilentlyContinue){Publish-033Progress 'backup' $i $plan.Actions.Count $a.Path}
        }
        $journal=@{Schema=1;Id=[Guid]::NewGuid().ToString('N');Operation=$op;Status='prepared';Action=$Action;Group=$group;OldState=$old;Files=$files;CreatedDirectories=@();Failure=$null;
            Verdict=$(if($verdict){@{Decision=$verdict.Decision;Route=$verdict.Route;Warnings=@($verdict.Warnings);Overrides=@($verdict.Overrides);Manual=[bool]$verdict.Manual}}else{$null});Handover=$handover}
        $journal['RegistryActions']=@($plan.RegistryActions)
        $jp=Get-033Path $folder 'pending.json'
        if(Test-Path -LiteralPath $jp){Copy-033ManagedBlob $jp (Get-033Path $opFolder 'previous-journal.json') (Get-033FileHash $jp)}
        Write-033Json $jp $journal
        foreach($t in $group.Targets){Write-033Json (Get-033ManagedIndex $Vault $t.Exe) @{Schema=1;Exe=$t.Exe;Group=$id}}
        $lateWarnings=@()
        try{
            $journal.Status='applying';Write-033Json $jp $journal
            if($Action -eq 'Restore'){Invoke-033VulkanRegistryActions $journal $jp}
            if($Action -eq 'Restore' -and $old -and $old.ContainsKey('CleanedDirectories')){
                # S31：净化时删掉的目录先按原样建回（含本来就空的子目录），放回的文件才落回原处，也不会被当成 033 建的目录。
                foreach($cDir in @($old.CleanedDirectories|Sort-Object {([string]$_.Path).Split('/').Count})){$cPath=Assert-033ManagedEntry $group $cDir;if(-not(Test-Path -LiteralPath $cPath)){[void][IO.Directory]::CreateDirectory($cPath)}}
            }
            $progressWritten=0
            foreach($e in $files){
                if($e.NoWrite){
                    $e.Applied=Get-033ManagedSnapshot $group.Targets[$e.Root].Root $e.Path
                    if(-not(Test-033ManagedSnapshot $e.Applied $e.Before)){throw '登记运行文件期间现场发生变化'}
                }else{$e.Applied=Set-033ManagedFile $journal $e $jp $folder $gates}
                Write-033Json $jp $journal;Invoke-033ManagedStep 'after-record' $journal $e
                ++$progressWritten;if(Get-Command Publish-033Progress -ErrorAction SilentlyContinue){Publish-033Progress 'write' $progressWritten $files.Count $e.Path}
            }
            if(Get-Command Publish-033Progress -ErrorAction SilentlyContinue){Publish-033Progress 'validate'}
            foreach($e in $files){if(-not(Test-033ManagedSnapshot (Get-033ManagedSnapshot $group.Targets[$e.Root].Root $e.Path) $e.Applied -HashOnly)){throw '完成核验时发现外部改动'}}
            if($Action -eq 'Restore'){
                # 卸载途中游戏或别的程序新写进 033 专用目录的文件：先归档进备份库再删；删不掉只提醒（5.0 不会为这个整个撤销）。
                $late=Get-033CleanupInventory $group $gates;$li=0
                foreach($item in @($late.Files)){
                    $lp=Assert-033ManagedEntry $group $item
                    try{
                        $lh=Get-033FileHash $lp;$lb=$op+'/late-'+$li+'.bin';$li++
                        Copy-033ManagedBlob $lp (Get-033Path $folder $lb) $lh
                        [IO.File]::SetAttributes($lp,[IO.FileAttributes]::Normal);[IO.File]::Delete($lp)
                        $journal.Files+=@(@{Root=$item.Root;Path=$item.Path;Before=@{Exists=$true;Hash=$lh;Identity=$null;Attributes=0;Time=$null};After=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null};BeforeBlob=$lb;AfterBlob=$null;Applied=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null};Intent=$null;UndoIntent=$null;Recovered=$null;NoWrite=$true;Late=$true})
                    }catch{$lateWarnings+=@('卸载途中新出现的 '+$item.Path+' 删不掉（'+$_.Exception.Message+'），留在原处。')}
                }
                Write-033Json $jp $journal
            }
            foreach($e in $group.Entries){
                $match=@($files|Where-Object {$_.Root -eq $e.Root -and $_.Path -ieq $e.Path})
                if($match.Count){$e.Current=$match[0].Applied}
            }
            if($Action -eq 'Install'){Invoke-033VulkanRegistryActions $journal $jp}
            $group.CreatedDirectories=@($group.CreatedDirectories+$journal.CreatedDirectories|Select-Object -Unique)
            Write-033Json (Get-033Path $folder 'state.json') $group
            Invoke-033ManagedStep 'after-state' $journal $null
            $journal.Status='committed';Write-033Json $jp $journal
            Invoke-033ManagedStep 'after-commit' $journal $null
            Copy-033ManagedBlob $jp (Get-033Path $opFolder 'receipt.json') (Get-033FileHash $jp)
            if($Action -eq 'Install'){
                # 燕云 S24：升级不再安装的文件（如整套 033-runtime\streamline）走后，033 早先自己建的目录空了就删掉，
                # 不空的不动；和卸载时清空目录是同一个函数。删不掉只提醒，不影响这次安装。
                $sweep=@{Action='Install';Status='committed';Group=$group;CreatedDirectories=@($group.CreatedDirectories)}
                try{Remove-033ManagedEmptyDirectories $sweep $gates}catch{$lateWarnings+=@('033 自己建的空目录没删掉：'+$_.Exception.Message+'。不影响游戏，可以手动删掉。')}
            }
            if($Action -eq 'Install' -and $plan.ContainsKey('RemoveDirectories') -and @($plan.RemoveDirectories).Count){
                # S31：净化清空的目录（计划里算好，深的在前）确实空了才删，从不整棵删；「还原安装前」会先按原样建回。
                $cleanRemoved=0
                foreach($cDir in @($plan.RemoveDirectories)){
                    try{
                        $cPath=Assert-033ManagedEntry $group $cDir
                        if(-not [IO.Directory]::Exists($cPath)){continue}
                        if([int](Get-Item -LiteralPath $cPath -Force).Attributes -band [int][IO.FileAttributes]::ReparsePoint){continue}
                        if(@(Get-ChildItem -LiteralPath $cPath -Force).Count){$lateWarnings+=@('净化：'+[string]$cDir.Path+' 里又有了东西，这个目录留着。');continue}
                        [IO.Directory]::Delete($cPath,$false);$cleanRemoved++
                    }catch{$lateWarnings+=@('净化：目录 '+[string]$cDir.Path+' 没删掉：'+$_.Exception.Message+'。不影响游戏。')}
                }
                if($cleanRemoved){$lateWarnings+=@('净化：清空的 '+$cleanRemoved+' 个目录也删了，「还原安装前」会原样建回。')}
            }
            if($Action -eq 'Restore'){
                $journal.CreatedDirectories=$group.CreatedDirectories
                try{Remove-033ManagedEmptyDirectories $journal $gates;Assert-033NoRuntimeRemainder $group $gates}catch{$lateWarnings+=@('原件已还原，但 033 专用目录没清干净：'+$_.Exception.Message+'。不影响游戏，可以手动删掉。')}
                # Global defaults and per-game profiles belong to the user,
                # outside this game's ledger. Retain them on uninstall, even
                # when this vault has no other installed groups. Another vault
                # or a portable installation may still use them.

            }
            $storage=Complete-033VaultStorage $Vault $id
            if($storage.Status -eq 'deferred'){$lateWarnings+=@($storage.Warning)}
            $done=@{Status=$group.Status;Storage=$storage;Message=$(if($Action -eq 'Restore'){'已卸载目录内本安装管理的033文件并核对残留；原有文件已恢复，配置和日志备份保留在游戏目录外。'}else{$(if($plan.ContainsKey('Cleaned') -and @($plan.Cleaned).Count){'净化：'+@($plan.Cleaned).Count+' 个不属于游戏的文件已移进备份库（「还原安装前」可放回）；'}else{''})+'文件安装及哈希核验完成，游戏效果待手动验证。'});Group=$id;Receipt=(Get-033Path $opFolder 'receipt.json');Files=$files.Count;Targets=$group.Targets.Exe;Vault=$Vault;GameTested=$false;
                Mount=@(@($group.Targets|Where-Object {$_.ContainsKey('Mount')}|ForEach-Object {$_.Mount})|Select-Object -Unique);
                Route=$(if($verdict){$verdict.Route}else{$null});Manual=$(if($verdict){[bool]$verdict.Manual}else{$false});Warnings=@(@($(if($verdict){$verdict.Warnings}else{@()}))+@($(if($handover -and $handover.ContainsKey('Notes')){$handover.Notes}else{@()}))+@($(if($plan.ContainsKey('Warnings')){$plan.Warnings}else{@()}))+@($lateWarnings)+@($staleLedgerNotes)|Select-Object -Unique);Overrides=@($(if($verdict){$verdict.Overrides}else{@()}));Handover=$handover;Report=$report;Resolution=@($resolutions)}
            if($plan.ContainsKey('Cleaned')){$done['Cleaned']=@($plan.Cleaned)}
            if($plan.ContainsKey('Unverified')){$done['Unverified']=@($plan.Unverified)}
            return $done
        }catch{
            # Only the durable commit marker decides whether rollback is allowed.
            # A failed receipt copy/empty-directory cleanup cannot undo a commit
            # or be reported as a successful rollback.
            $persisted=Read-033ManagedJson $jp
            if($persisted.Id -eq $journal.Id -and $persisted.Status -eq 'committed'){
                throw "文件事务已提交为 $($group.Status)，回执副本或空目录清理未完成: $($_.Exception.Message)。主记录及原件仍在: $jp；可运行 Recover 重试清理。"
            }
            $journal.Status='applying'
            $failure=$_.Exception.Message
            if($failure -match '(?i)访问被拒绝|access to the path .* is denied|UnauthorizedAccess|拒绝访问'){$failure+='。多半是杀毒软件拦了写入，或没有权限：把游戏目录加进杀软信任区，或右键「033安装器.exe」选「以管理员身份运行」后再装'}
            $journal.Failure=$failure;Write-033Json $jp $journal
            try{Restore-033ManagedPending $folder $journal $gates;Remove-033ManagedEmptyDirectories $journal $gates}
            catch{throw "操作失败: $failure；精确恢复尚未完成: $($_.Exception.Message)。备份保留: $jp"}
            throw "操作失败，已撤销本次改动: $failure。原件和记录保留: $jp"
        }
    }finally{Close-033ManagedGates $gates;$mutex.ReleaseMutex();$mutex.Dispose()}
}
