# Record runtime output BEFORE first use. Archive current contents in the same
# transaction before removal; backups/diagnostics remain outside the game root.
function Test-033MutableEntry($Entry){
    return $Entry.Policy -in @('seed','runtime') -or $Entry.Path -match '(?i)\.(ini|cfg|log|state)$' -or ($Entry.ContainsKey('Mutable') -and $Entry.Mutable)
}
# 2026-09-11 回退到 5.0：卸载 / 更新时文件已不是 033 装的那份，不再停下——原地保留、提醒，原件仍在备份库。
function Test-033RemovalForeign($Entry,$Now){
    if(-not $Now.Exists){return $false}
    if($Entry.ContainsKey('VariantHash') -and $Entry.VariantHash){return ($Now.Hash -ine $Entry.VariantHash -and -not(Test-033ManagedSnapshot $Now $Entry.Original -HashOnly))}
    if(Test-033MutableEntry $Entry){return $false}
    if(Test-033ManagedSnapshot $Now $Entry.Current -HashOnly){return $false}
    if(Test-033ManagedSnapshot $Now $Entry.Original -HashOnly){return $false}
    return $true
}
function Assert-033RemovalCurrent($Entry,$Now){
    if(-not $Now.Exists){return} # already removed, or the original will be restored
    # 只比内容（业主 2026-09-11）：编号、时间变了而字节没变，仍是 033 装的那份。
    if($Entry.ContainsKey('VariantHash') -and $Entry.VariantHash -and $Now.Hash -ine $Entry.VariantHash -and -not(Test-033ManagedSnapshot $Now $Entry.Original -HashOnly)){throw "停用副本不属于已安装033: $($Entry.Path)"}
    if(-not(Test-033MutableEntry $Entry) -and -not(Test-033ManagedSnapshot $Now $Entry.Current -HashOnly)){throw "文件已被其它程序替换，保留现场: $($Entry.Path)。它的内容已经不是 033 装的那份，卸载不会删它；确定不要了就手动删掉，再运行一键恢复。"}
}
# RE Engine copies the game's loose DLLs into _storage_ and loads them from there, so the host runs
# from the mirror and looks for its core next to itself (fork 033_product.cpp load_core).
# 2026-09-12 生化危机9：这里原来要求 `_storage_` 在安装那一刻就存在，注释还写着「never creates one」。
# 那个前提只在 9-11 修鬼武者时成立（那台机器上目录早就有了）。RE 引擎是【第一次启动游戏才建】这个目录，
# 游戏更新还会把它整个删掉重建——也就是说它永远晚于安装，干净的新装一次也命中不了：镜像整步静默跳过，
# 宿主从 `_storage_` 起来后找不到核心（ReShade.log: stage=load_core … win32=126），玩家只看到有面板没效果。
# 所以不再看目录在不在，声明了就建。代价是可能给用不上 `_storage_` 的 RE 游戏留一个空壳目录，
# 但它跟其它目标一样进账本的 CreatedDirectories，一键恢复时空了就删，不留痕。
function Get-033MirrorDirectories($Profile,[string]$Root){
    $out=@()
    if($Root -and $Profile.ContainsKey('MirrorDirectories')){
        foreach($name in @($Profile.MirrorDirectories)){
            if($name -isnot [string] -or [string]::IsNullOrWhiteSpace($name) -or $name -match '[\\/*?]'){throw '镜像目录必须是游戏根目录下的单层目录名'}
            $out+=@([string]$name)
        }
    }
    return @($out|Sort-Object -Unique)
}
function Get-033RuntimeFootprint($Profile,[string]$Root=''){
    $files=@('dlss5-033.log','dlss5-033.state','dlss5-033-nrscale.log','dlss5-033.cfg','033-framegen.log','033-gpu-fault.log','033-present-status.log','DLSS5已自动停用-看这里.txt')
    $dirs=@('DLSS5 Screenshots');$variants=@{}
    foreach($file in $Profile.Files){
        # Optional component files register per target, and only when the plan installs them.
        if($file.ContainsKey('Component')){continue}
        if($file.Target -ieq 'ReShade.ini'){$files+=@('ReShade.log')}
        # 链式共存时 OptiScaler.ini 只是种子合并（Policy=seed），OptiScaler 本身是别人的模组：它的日志和目录绝不是 033 的运行产物（Fable 2026-09-17）。
        if($file.Target -ieq 'OptiScaler.ini' -and [string]$file.Policy -ne 'seed'){$files+=@('OptiScaler.log');$dirs+=@('OptiScaler')}
        if($file.Target -match '(?i)(^|[\\/])dlss5-feed'){$files+=@('dlss5-feed.log','dlss5-feed.cfg')}
        if($file.Target -match '(?i)^[^\\/]+\.(dll|addon32|addon64)$'){
            $path=$file.Target+'.dlss5-off';$files+=@($path);$variants[$path]=$file.Hash
        }
    }
    if($Profile.Mode -eq 'feeder'){$files+=@('dlss5-feed.log','dlss5-feed.cfg')}
    if($Profile.ContainsKey('RuntimeFiles')){$files+=@($Profile.RuntimeFiles)}
    if($Profile.ContainsKey('RuntimeDirectories')){$dirs+=@($Profile.RuntimeDirectories)}
    # The game makes the mirrored copies itself, so they are runtime output, not files we write: register
    # them and uninstall archives and removes them. Without this a restore leaves the host in the mirror
    # with no core beside it, and the game still loads it (2026-09-11 鬼武者, after 一键恢复).
    foreach($mirror in @(Get-033MirrorDirectories $Profile $Root)){
        foreach($name in @(@($Profile.Files|Where-Object {-not($_.ContainsKey('Component')) -and [string]$_.Target -match '(?i)^[^\\/]+\.dll$'}|ForEach-Object {[string]$_.Target})|Sort-Object -Unique)){
            $files+=@($mirror+'/'+$name)
        }
        foreach($name in @($files|Where-Object {$_ -match '(?i)^033-runtime[\\/]'})){$files+=@($mirror+'/'+$name)}
        foreach($name in @($dirs|Where-Object {$_ -ieq '033-runtime'})){$dirs+=@($mirror+'/'+$name)}
    }
    return @{Files=@($files|Sort-Object -Unique);Directories=@($dirs|Sort-Object -Unique);Variants=$variants}
}
function Add-033RuntimePlan($Plan,$Package,$OldState){
    $planTrees=@();if($OldState -and $OldState.ContainsKey('CleanupTrees')){$planTrees=@($OldState.CleanupTrees)}
    for($ri=0;$ri -lt $Plan.Targets.Count;$ri++){
        $target=$Plan.Targets[$ri];$profile=@($Package.Data.Profiles|Where-Object Id -CEQ $target.Profile)[0]
        $footprint=Get-033RuntimeFootprint $profile $target.Root
        if($target.ContainsKey('Components')){
            foreach($c in @($target.Components)){
                if(-not $c.Installed){continue}
                $footprint.Files=@(@($footprint.Files)+@($c.RuntimeFiles)|Where-Object {$_}|Sort-Object -Unique)
                if($c.Loader){$v=[string]$c.Loader+'.dlss5-off';$footprint.Files=@(@($footprint.Files)+@($v)|Sort-Object -Unique);$footprint.Variants[$v]=[string]$c.LoaderHash}
            }
        }
        # 2026-09-12 生化危机9：一键恢复跑完（报告成功、42 个文件），`_storage_` 里仍然留着 033 的 dxgi.dll 和
        # dinput8.dll。游戏把我们的代理复制进镜像，下面登记运行产物时看见文件已经在，就把【033 自己的字节】当成
        # 玩家的原件记进账本，恢复于是"忠实地"把我们的 DLL 留下——RE 游戏正是从 `_storage_` 加载的，所以卸载完
        # 033 仍然挂着（只是旁边没核心）。按业主 2026-09-11 定的规矩「跨次按哈希认归属」：镜像目录里的副本只要
        # 内容等于本包要装的某个文件，就判为 033 的，不记原件、卸载时删掉。只限镜像目录——游戏根目录里的同名
        # 文件仍然当玩家的原件保护，那条线不能松。
        $mirrorPrefixes=@(@(Get-033MirrorDirectories $profile $target.Root)|ForEach-Object {[string]$_+'\'})
        $ownHashes=@{}
        foreach($pf in @($profile.Files)){if($pf.Hash){$ownHashes[([string]$pf.Hash).ToUpperInvariant()]=$true}}
        if($target.ContainsKey('Components')){foreach($c in @($target.Components)){if($c.Installed -and $c.LoaderHash){$ownHashes[([string]$c.LoaderHash).ToUpperInvariant()]=$true}}}
        foreach($path in $footprint.Files){
            $full=Assert-033ManagedEntry $Plan @{Root=$ri;Path=$path}
            $parent=Split-Path -Parent $full
            while($parent -ine $target.Root -and -not(Test-Path -LiteralPath $parent)){
                $relative=$parent.Substring($target.Root.Length+1)
                if(-not @($planTrees|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $relative}).Count){$planTrees+=@(@{Root=$ri;Path=$relative})}
                $parent=Split-Path -Parent $parent
            }
            if(-not(Test-Path -LiteralPath $parent -PathType Container)){throw '运行产物父路径被文件占用'}
            if(@($Plan.Entries|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $path}).Count){continue}
            $old=@();if($OldState){$old=@($OldState.Entries|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $path})}
            if($old.Count){$entry=Copy-033ManagedObject $old[0]}else{
                $now=Get-033ManagedSnapshot $target.Root $path
                $baseline=$now
                $inMirror=@($mirrorPrefixes|Where-Object {([string]$path).Replace('/','\').StartsWith($_,[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0
                if($inMirror -and $now.Exists -and $now.Hash -and $ownHashes.ContainsKey(([string]$now.Hash).ToUpperInvariant())){
                    $baseline=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null}
                }
                $entry=@{Root=$ri;Path=$path;Original=$baseline;OriginalBlob=$null;Current=$now;Policy='runtime'}
                # Register a baseline and copy any original, without writing logs,
                # state counters or configuration into the game at install time.
                $Plan.Actions+=@(@{Root=$ri;Path=$path;Before=$now;After=$now;Source=(Get-033Path $target.Root $path);OriginalEntry=$entry;NoWrite=$true})
            }
            if($footprint.Variants.ContainsKey($path)){$entry['VariantHash']=$footprint.Variants[$path]}
            $Plan.Entries+=@($entry)
        }
        foreach($path in $footprint.Directories){
            $full=Assert-033ManagedEntry $Plan @{Root=$ri;Path=$path}
            if($path -match '[*?]' -or $path -match '(?i)^(_033transactions|_DLSS5_备份)([\\/]|$)'){throw '禁止猜测清理历史目录'}
            $parent=$full
            while($parent -ine $target.Root -and -not(Test-Path -LiteralPath $parent)){
                $relative=$parent.Substring($target.Root.Length+1)
                if(-not @($planTrees|Where-Object {$_.Root -eq $ri -and $_.Path -ieq $relative}).Count){$planTrees+=@(@{Root=$ri;Path=$relative})}
                $parent=Split-Path -Parent $parent
            }
            if(-not(Test-Path -LiteralPath $parent -PathType Container)){throw '运行目录被文件占用'}
        }
    }
    $Plan['CleanupTrees']=$planTrees
}
function Get-033CleanupTrees($State){
    $trees=@{};$items=@();if($State.ContainsKey('CleanupTrees')){$items+=@($State.CleanupTrees)}
    foreach($dir in $State.CreatedDirectories){
        for($ri=0;$ri -lt $State.Targets.Count;$ri++){
            $root=$State.Targets[$ri].Root
            if($dir.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){$items+=@(@{Root=$ri;Path=$dir.Substring($root.Length+1)})}
        }
    }
    foreach($item in $items){$full=Assert-033ManagedEntry $State $item;$trees[$full]=$item}
    return @($trees.Values)
}
function Get-033CleanupInventory($State,$Gates=$null){
    $files=@{};$dirs=@{};$queue=[Collections.Generic.Queue[object]]::new()
    foreach($tree in @(Get-033CleanupTrees $State)){$queue.Enqueue($tree)}
    while($queue.Count){
        $item=$queue.Dequeue();$path=Assert-033ManagedEntry $State $item
        if(-not(Test-Path -LiteralPath $path)){continue}
        if(-not(Test-Path -LiteralPath $path -PathType Container)){throw "033专用目录已变成文件: $path"}
        if($dirs.ContainsKey($path)){continue};$dirs[$path]=$true
        foreach($child in Get-ChildItem -LiteralPath $path -Force){
            if($child.Name -eq '.033-directory.guard' -and $Gates -and $Gates.Directories.ContainsKey($path)){continue}
            $rel=$item.Path+'/'+$child.Name;$entry=@{Root=$item.Root;Path=$rel}
            [void](Assert-033ManagedEntry $State $entry) # rejects links, saves and escaped paths
            if($child.PSIsContainer){$queue.Enqueue($entry)}else{$files[$child.FullName]=$entry}
        }
    }
    return @{Files=@($files.Values);Directories=@($dirs.Keys)}
}
function New-033UninstallPlan($State){
    $entries=@($State.Entries);$seen=@{}
    foreach($entry in $entries){$seen[(Assert-033ManagedEntry $State $entry)]=$true}
    $inventory=Get-033CleanupInventory $State
    foreach($item in $inventory.Files){
        $full=Assert-033ManagedEntry $State $item
        if($seen.ContainsKey($full)){continue}
        $now=Get-033ManagedSnapshot $State.Targets[$item.Root].Root $item.Path
        $entries+=@(@{Root=$item.Root;Path=$item.Path;Original=@{Exists=$false;Hash=$null;Identity=$null;Attributes=0;Time=$null};OriginalBlob=$null;Current=$now;Policy='runtime'})
        $seen[$full]=$true
    }
    $actions=@();$warnings=@()
    foreach($entry in $entries){
        $now=Get-033ManagedSnapshot $State.Targets[$entry.Root].Root $entry.Path
        if(Test-033RemovalForeign $entry $now){
            $warnings+=@($entry.Path+' 已经不是 033 装的那份（被别的程序或手工换过），卸载不动它'+$(if($entry.Original.Exists -and $entry.OriginalBlob){'；装 033 之前的原件还在备份库：'+$entry.OriginalBlob}else{''})+'。')
            continue
        }
        $actions+=@(@{Root=$entry.Root;Path=$entry.Path;Before=$now;After=$entry.Original;Source=$entry.OriginalBlob;FromVault=$true;OriginalEntry=$null})
    }
    return @{Targets=$State.Targets;Actions=$actions;Entries=$entries;Version=$State.Version;PackageHash=$State.PackageHash;CleanupTrees=@(Get-033CleanupTrees $State);CleanupDirectories=$inventory.Directories;Warnings=$warnings}
}
function Assert-033NoRuntimeRemainder($State,$Gates=$null){
    $inventory=Get-033CleanupInventory $State $Gates
    if($inventory.Files.Count){throw "目录中仍有033文件，卸载未完成: $($inventory.Files[0].Path)"}
}
