# 033 game survey: read-only reconnaissance before any plan. No LoadLibrary,
# execution, registry writes or network. Every fact carries its evidence so the
# verdict (Get-033InstallVerdict) can be explained in plain language.
$script:FOREIGN_TOOL_DIRS=@('OptiScaler','SpecialK','enbseries','REFramework','_测试备份2','_DLSS5_备份','_033transactions','_033routes','033-runtime','host64','reshade-shaders','.git','dxvk','DXVK-cache')
# Display adapter facts, read once per session by Get-033GpuFacts (tests inject a fixture here).
# 2026-09-11 公开包：玩家把整个安装包解压进了游戏目录（黎之轨迹），侦察把包里自己的 payload\mfg2030\nvngx.dll 当成了
# 游戏自带 DLSS、libxess_fg.dll 当成了 D3D12 证据，DX11、没有 DLSS 的游戏于是被判去走原生 DLSS 路线。
# 带 033-package.json 的目录就是 033 安装包；游戏根目录本身就是安装包时，跳过包里那几个子目录。
function Test-033SkipPackageDir([string]$Parent,$Child,[int]$Depth){
    if(Test-Path -LiteralPath (Join-Path $Child.FullName '033-package.json') -PathType Leaf){return $true}
    return ($Depth -eq 0 -and $Child.Name -in @('payload','managed','notices','工具') -and (Test-Path -LiteralPath (Join-Path $Parent '033-package.json') -PathType Leaf))
}
$script:K033GpuFacts=$null
$script:K033SystemDllDirectory=$null
function Get-033FileMarkers([string]$Path,[string[]]$Markers,[long]$MaxBytes=67108864){
    # Bounded ASCII marker scan over the raw image. Version info alone misses
    # renamed proxies; the entry-point names inside the image do not.
    $found=@()
    try{
        if((Get-Item -LiteralPath $Path).Length -gt $MaxBytes){return @()}
        $text=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path))
        foreach($m in $Markers){if($text.IndexOf($m,[StringComparison]::Ordinal) -ge 0){$found+=@($m)}}
    }catch{}
    return ,$found
}
function Get-033ProxyIdentity([string]$Path){
    $info=$null;try{$info=[Diagnostics.FileVersionInfo]::GetVersionInfo($Path)}catch{}
    $fields=@()
    if($info){foreach($v in @($info.ProductName,$info.FileDescription,$info.CompanyName,$info.InternalName)){if($v){$fields+=@([string]$v)}}}
    $text=($fields -join ' | ')
    $markers=Get-033FileMarkers $Path @('K033_ReShadeEntry','K033_FeederEntry','ReShadeRegisterAddon','ReShadeCreateEffectRuntime','OptiScaler','dxvk','DXVK','ENBSeries','enbseries','Special K','SpecialK','dgVoodoo','REFramework','praydog')
    $kind='unknown';$confidence='low'
    if($markers -contains 'K033_ReShadeEntry' -or $markers -contains 'K033_FeederEntry'){$kind='033-fork';$confidence='high'}
    elseif($markers -contains 'REFramework' -or $markers -contains 'praydog'){$kind='reframework';$confidence='high'}
    elseif($markers -contains 'ReShadeRegisterAddon' -or $markers -contains 'ReShadeCreateEffectRuntime' -or $text -match 'ReShade'){$kind='reshade';$confidence=$(if($markers.Count){'high'}else{'medium'})}
    elseif($markers -contains 'OptiScaler' -or $text -match 'OptiScaler'){$kind='optiscaler';$confidence='high'}
    elseif($markers -contains 'dxvk' -or $markers -contains 'DXVK' -or $text -match 'DXVK'){$kind='dxvk';$confidence='high'}
    elseif($markers -contains 'ENBSeries' -or $markers -contains 'enbseries' -or $text -match 'ENB'){$kind='enb';$confidence='high'}
    elseif($markers -contains 'Special K' -or $markers -contains 'SpecialK' -or $text -match 'Special K'){$kind='specialk';$confidence='high'}
    elseif($markers -contains 'dgVoodoo' -or $text -match 'dgVoodoo'){$kind='dgvoodoo';$confidence='high'}
    # nvidia_mfg_bridge loader (any renamed copy): it opens its INI and reads the override key by wide string.
    elseif(@(Test-033FileNeedles $Path @('dlssg_to_fsr3.ini','ForceFrameGenOverride') -Wide -MaxBytes 536870912).Count -eq 2){$kind='nvidia-mfg-bridge';$confidence='high'}
    elseif($text -match 'Microsoft'){$kind='microsoft-copy';$confidence='medium'}
    $pe=Get-033PeInfo $Path
    [pscustomobject]@{Path=$Path;Kind=$kind;Confidence=$confidence;VersionText=$text;FileVersion=$(if($info){[string]$info.FileVersion}else{$null});
        Architecture=$pe.Architecture;PeStatus=$pe.Status;Size=(Get-Item -LiteralPath $Path).Length}
}
function Get-033UpscalerStack([string]$Dir,[datetime]$ExeTimeUtc,[string[]]$ManagedPaths=@()){
    $spec=@(
        @{Name='nvngx_dlss.dll';Kind='dlss-sr';Family='nvidia';Native=$true},
        @{Name='nvngx_dlssg.dll';Kind='dlss-fg';Family='nvidia';Native=$true},
        @{Name='nvngx_dlssd.dll';Kind='dlss-rr';Family='nvidia';Native=$true},
        @{Name='sl.interposer.dll';Kind='streamline';Family='nvidia';Native=$true},
        @{Name='sl.dlss.dll';Kind='streamline-dlss';Family='nvidia';Native=$true},
        @{Name='sl.dlss_g.dll';Kind='streamline-fg';Family='nvidia';Native=$true},
        @{Name='sl.reflex.dll';Kind='streamline-reflex';Family='nvidia';Native=$false},
        @{Name='amd_fidelityfx_dx12.dll';Kind='fsr';Family='amd';Native=$false},
        @{Name='amd_fidelityfx_upscaler_dx12.dll';Kind='fsr';Family='amd';Native=$false},
        @{Name='amd_fidelityfx_framegeneration_dx12.dll';Kind='fsr-fg';Family='amd';Native=$false},
        @{Name='amd_fidelityfx_vk.dll';Kind='fsr';Family='amd';Native=$false},
        @{Name='libxess.dll';Kind='xess';Family='intel';Native=$false},
        @{Name='libxess_dx11.dll';Kind='xess';Family='intel';Native=$false},
        @{Name='libxess_fg.dll';Kind='xess-fg';Family='intel';Native=$false},
        @{Name='libxell.dll';Kind='xell';Family='intel';Native=$false})
    $items=@();$nativeDlss=$false;$streamline=$false;$nativeFg=$false
    foreach($s in $spec){
        $p=Join-Path $Dir $s.Name;if(-not(Test-Path -LiteralPath $p)){continue}
        $item=Get-Item -LiteralPath $p;$ver=$null;try{$ver=[string][Diagnostics.FileVersionInfo]::GetVersionInfo($p).FileVersion}catch{}
        $backups=@(Get-ChildItem -LiteralPath $Dir -Force -File -Filter ($s.Name+'.*') -ErrorAction SilentlyContinue|Where-Object {$_.Name -ine $s.Name}|ForEach-Object {$_.Name})
        $managed=($ManagedPaths -icontains $s.Name)
        $delta=[Math]::Abs(($item.LastWriteTimeUtc-$ExeTimeUtc).TotalDays)
        $owner=if($managed){'033-managed'}elseif($backups.Count){'user-replaced'}elseif($delta -le 45){'game-shipped'}else{'added-later'}
        if(-not $managed -and $s.Native){
            if($s.Kind -eq 'dlss-sr' -or $s.Kind -eq 'streamline-dlss'){$nativeDlss=$true}
            if($s.Kind -like 'streamline*'){$streamline=$true}
            if($s.Kind -eq 'dlss-fg' -or $s.Kind -eq 'streamline-fg'){$nativeFg=$true}
        }
        $items+=@([pscustomobject]@{Name=$s.Name;Kind=$s.Kind;Family=$s.Family;Version=$ver;Size=$item.Length;WrittenUtc=$item.LastWriteTimeUtc.ToString('o');Owner=$owner;Backups=$backups})
    }
    [pscustomobject]@{Items=$items;NativeDlss=$nativeDlss;Streamline=$streamline;NativeFrameGen=$nativeFg}
}
function Get-033ProxyOccupancy([string]$Dir){
    $aliases=@('dxgi.dll','d3d12.dll','d3d11.dll','d3d10.dll','d3d10_1.dll','d3d9.dll','d3d8.dll','ddraw.dll','dinput8.dll','winmm.dll','version.dll','dbghelp.dll','cryptsp.dll','wininet.dll','winhttp.dll','opengl32.dll','vulkan-1.dll','xinput1_3.dll','xinput9_1_0.dll')
    $rows=@()
    foreach($a in $aliases){
        $p=Join-Path $Dir $a
        if(Test-Path -LiteralPath $p){$id=Get-033ProxyIdentity $p;$rows+=@([pscustomobject]@{Alias=$a;Kind=$id.Kind;Confidence=$id.Confidence;VersionText=$id.VersionText;FileVersion=$id.FileVersion;Architecture=$id.Architecture;Size=$id.Size;Disabled=$false})}
        $off=$p+'.dlss5-off'
        if(Test-Path -LiteralPath $off){$id=Get-033ProxyIdentity $off;$rows+=@([pscustomobject]@{Alias=$a;Kind=$id.Kind;Confidence=$id.Confidence;VersionText=$id.VersionText;FileVersion=$id.FileVersion;Architecture=$id.Architecture;Size=$id.Size;Disabled=$true})}
    }
    return $rows
}
function Test-033OldPackageUninstaller([string]$Path){
    # Only the known v5 一键包 uninstaller qualifies for an automatic handover:
    # its header signature and a -Silent parameter. Anything else stays manual.
    if(-not(Test-Path -LiteralPath $Path)){return $false}
    try{
        $raw=[IO.File]::ReadAllText($Path,[Text.Encoding]::UTF8)
        $head=$raw.Substring(0,[Math]::Min(1200,$raw.Length))
        return ($head -match 'DLSS\s*5\s*一键卸载' -and $raw -match 'param\([^\)]*\$Silent[^\)]*\)')
    }catch{return $false}
}
function Get-033ExistingInstall([string]$Dir,[string]$Exe,[string]$Vault){
    $managed=$null;try{if($Vault){$managed=Read-033ManagedGroup $Vault $Exe}}catch{$managed=$null}
    $managedInstalled=($managed -and $managed.State -and $managed.State.Status -eq 'installed')
    $managedPaths=@();if($managedInstalled){foreach($e in $managed.State.Entries){$managedPaths+=@([string]$e.Path)}}
    $record=Join-Path $Dir '_安装记录.txt';$uninstaller=Join-Path $Dir 'dlss5_uninstall.ps1'
    $oldRecord=Test-Path -LiteralPath $record
    $uninstallerOk=Test-033OldPackageUninstaller $uninstaller
    $markers=@()
    foreach($name in @('_033-integrated.json','_安装记录.txt','_033transactions','033-runtime','dlss5-033.addon64','dlss5-feed.addon64','dlss5-feed.addon32','renodx-dlss5.addon64','dlss5-033.cfg','dlss5-033.state')){
        if(Test-Path -LiteralPath (Join-Path $Dir $name)){$markers+=@($name)}
    }
    $receipts=@();$tx=Join-Path $Dir '_033transactions'
    if(Test-Path -LiteralPath $tx -PathType Container){$receipts=@(Get-ChildItem -LiteralPath $tx -Force -ErrorAction SilentlyContinue|Select-Object -First 32|ForEach-Object {$_.Name})}
    $kind='none'
    if($managedInstalled){$kind='managed'}
    elseif($oldRecord -and $uninstallerOk){$kind='old-package'}
    elseif((Test-033LegacyInstallation $Dir)){$kind='residue'}
    [pscustomobject]@{Kind=$kind;Managed=$managed;ManagedPaths=$managedPaths;OldRecord=$(if($oldRecord){$record}else{$null});
        OldUninstaller=$(if($uninstallerOk){$uninstaller}else{$null});UninstallerPresent=(Test-Path -LiteralPath $uninstaller);Markers=$markers;Receipts=$receipts;
        LegacyMount=$(if($kind -eq 'old-package'){Get-033LegacyMountHistory $record $Exe $Dir}else{$null});HandoverAvailable=($kind -eq 'old-package')}
}
function Get-033NgxRuntime{
    # Where the NVIDIA driver keeps NGX (read-only): the registry NGXCore path
    # (what the core itself follows), the DCH driver store, then System32.
    $paths=@();$found=$null;$source=$null
    try{
        $key=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\NVIDIA Corporation\Global\NGXCore' -ErrorAction Stop
        foreach($name in @('FullPath','NGXPath')){
            if($key.PSObject.Properties[$name]){$v=[string]$key.$name;if($v){$paths+=@($v)}}
        }
    }catch{}
    foreach($p in $paths){
        foreach($candidate in @((Join-Path $p '_nvngx.dll'),(Join-Path $p 'nvngx.dll'),$p)){
            if(-not $found -and (Test-Path -LiteralPath $candidate -PathType Leaf)){$found=$candidate;$source='registry NGXCore'}
        }
    }
    if(-not $found){
        $store=Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository'
        try{
            $hits=@(Get-ChildItem -LiteralPath $store -Directory -Filter 'nv_disp*' -ErrorAction Stop|Sort-Object LastWriteTime -Descending|Select-Object -First 8)
            foreach($h in $hits){$c=Join-Path $h.FullName '_nvngx.dll';if(-not $found -and (Test-Path -LiteralPath $c)){$found=$c;$source='driver store'}}
        }catch{}
    }
    if(-not $found){$c=Join-Path $env:SystemRoot 'System32\nvngx.dll';if(Test-Path -LiteralPath $c){$found=$c;$source='System32'}}
    [pscustomobject]@{ShellPresent=[bool]$found;ShellPath=$(if($found){$found}else{'注册表 NGXCore / DriverStore nv_disp* / System32 均未找到 _nvngx.dll'});Source=$source}
}
function Get-033NvidiaGeneration([string]$Name){
    # GeForce RTX model number -> generation (RTX 2060 -> 20, RTX 3080 Ti -> 30, RTX 4090 -> 40).
    # Workstation names (Quadro RTX 4000 is Turing, RTX A4000 is Ampere) do not follow that
    # scheme and stay unknown; an unknown card never qualifies for a per-generation component.
    if($Name -match '(?i)GeForce\s+RTX\s*([2-5])0[5-9]0(?!\d)'){return [int]$Matches[1]*10}
    return 0
}
function Get-033GpuFacts{
    # Display adapters as Windows reports them (read-only WMI; no driver or NVAPI call).
    if($script:K033GpuFacts){return $script:K033GpuFacts}
    $names=@()
    try{$names=@(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop|ForEach-Object {[string]$_.Name}|Where-Object {$_})}catch{}
    $nvidia=@($names|Where-Object {$_ -match '(?i)NVIDIA|GeForce|Quadro|\bRTX\b'})
    $script:K033GpuFacts=[pscustomobject]@{Adapters=$names;Nvidia=$nvidia;Generations=@($nvidia|ForEach-Object {Get-033NvidiaGeneration $_});Source='Win32_VideoController'}
    return $script:K033GpuFacts
}
function ConvertTo-033NgxVersionText([long]$Number){
    # A version folder of NVIDIA's component cache is one integer: Streamline 2.10.3 = 0x020A03 = 133635,
    # DLSS frame generation 310.9.0 = 0x01360900 = 20318464.
    if($Number -le 0){return ''}
    return ('{0}.{1}.{2}' -f ($Number -shr 16),(($Number -shr 8) -band 0xFF),($Number -band 0xFF))
}
function Get-033NvidiaArchitecture([int]$Generation){
    # GeForce generation -> the architecture number NVIDIA's cache file names carry (hexadecimal, as text in the name):
    # a file serves every card AT OR ABOVE its number. 20 Turing 0x160, 30 Ampere 0x170, 40 Ada 0x190, 50 Blackwell 0x1B0.
    switch($Generation){20{return 0x160} 30{return 0x170} 40{return 0x190} 50{return 0x1B0} default{return 0}}
}
function Get-033NgxCacheEvidence([string]$Root=$(Join-Path $env:ProgramData 'NVIDIA\NGX\models'),[string[]]$Features=@('sl_dlss_g_0','sl_interposer_0','dlssg'),[int]$MaxEntries=4000){
    # READ-ONLY look into the cache where the NVIDIA driver keeps components it downloaded itself:
    #   <Root>\<feature>\versions\<version integer>\files\<minimum architecture, hex>_<application id>.<dll|bin>
    # Nothing is downloaded, started or changed. Why the installer looks: a game that bundles an old Streamline can only get
    # a multi-frame capable frame-generation plugin from here, and only from a file whose architecture the card meets.
    $items=@();$seen=0
    if(-not $Root -or -not(Test-Path -LiteralPath $Root -PathType Container)){return [pscustomobject]@{Present=$false;Root=$Root;Items=@()}}
    foreach($feature in $Features){
        $versions=Join-Path (Join-Path $Root $feature) 'versions'
        if(-not(Test-Path -LiteralPath $versions -PathType Container)){continue}
        $dirs=@();try{$dirs=@(Get-ChildItem -LiteralPath $versions -Directory -Force -ErrorAction Stop)}catch{continue}
        foreach($d in $dirs){
            $number=0L;if(-not [long]::TryParse($d.Name,[ref]$number) -or $number -le 0){continue}
            $files=@();try{$files=@(Get-ChildItem -LiteralPath (Join-Path $d.FullName 'files') -File -Force -ErrorAction Stop)}catch{continue}
            foreach($f in $files){
                $seen++;if($seen -gt $MaxEntries){break}
                if($f.Name -notmatch '(?i)\A([0-9A-F]{3})_([0-9A-F]{7})\.(dll|bin)\z'){continue}
                $items+=@([pscustomobject]@{Feature=$feature;Version=$number;VersionText=(ConvertTo-033NgxVersionText $number);Architecture=[Convert]::ToInt32($Matches[1],16);Application=$Matches[2].ToUpperInvariant();File=$f.Name})
            }
        }
    }
    [pscustomobject]@{Present=($items.Count -gt 0);Root=$Root;Items=@($items)}
}
function Get-033NgxCacheBest($Evidence,[string]$Feature,[int]$Generation){
    # The newest cached file of a feature, and the newest one THIS generation of card may use (architecture at or below its own).
    $all=@($Evidence.Items|Where-Object {$_.Feature -ceq $Feature}|Sort-Object Version -Descending)
    $arch=Get-033NvidiaArchitecture $Generation
    $usable=@($all|Where-Object {$arch -gt 0 -and $_.Architecture -le $arch})
    [pscustomobject]@{Newest=$(if($all.Count){$all[0]}else{$null});Usable=$(if($usable.Count){$usable[0]}else{$null});Architecture=$arch}
}
function Get-033BundledFrameGenVersion([string]$Dir,$FrameGen){
    # Version of the Streamline frame-generation plugin the GAME ships (read from the file's version resource; read-only).
    foreach($e in @($FrameGen.Evidence)){
        $p=[string]$e;if(-not $p -or (Split-Path -Leaf $p) -ine 'sl.dlss_g.dll'){continue}
        if(-not [IO.Path]::IsPathRooted($p)){$p=Join-Path $Dir $p}
        try{$v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p);if($v.FileMajorPart -gt 0){return [pscustomobject]@{Path=$p;Major=$v.FileMajorPart;Minor=$v.FileMinorPart;Text=('{0}.{1}.{2}' -f $v.FileMajorPart,$v.FileMinorPart,$v.FileBuildPart)}}}catch{}
    }
    return $null
}
function Get-033FrameGenCacheReport($Survey){
    # Report lines (Chinese, for the player) about multi-frame generation on this card. Facts first; the one inference is marked.
    $lines=@()
    if(-not $Survey.FrameGen -or -not $Survey.FrameGen.Present){return $lines}
    $generations=@($Survey.Gpu.Generations|Where-Object {$_ -gt 0}|Sort-Object -Unique)
    if($generations.Count -ne 1){return $lines}
    $generation=[int]$generations[0]
    $bundled=$(if($Survey.PSObject.Properties['FrameGenBundled']){$Survey.FrameGenBundled}else{$null})
    if($bundled){$lines+=@('游戏自带的帧生成插件：Streamline '+$bundled.Text)}
    $cache=$(if($Survey.PSObject.Properties['NgxCache']){$Survey.NgxCache}else{$null})
    if(-not $cache){return $lines}
    $best=Get-033NgxCacheBest $cache 'sl_dlss_g_0' $generation
    $label={param($a) switch($a){0x160{'20 系'} 0x170{'30 系'} 0x180{'30 系之后'} 0x190{'40 系'} 0x1A0{'50 系'} 0x1B0{'50 系'} default{'0x{0:X}' -f $a}}}
    if(-not $best.Newest){$lines+=@('NVIDIA 在线组件缓存（只读查看）：没有帧生成插件')}
    elseif($best.Usable){$lines+=@('NVIDIA 在线组件缓存（只读查看）：这张 '+$generation+' 系能用的帧生成插件最高 Streamline '+$best.Usable.VersionText)}
    else{$lines+=@('NVIDIA 在线组件缓存（只读查看）：帧生成插件最高 Streamline '+$best.Newest.VersionText+'，但文件只给 '+(& $label $best.Newest.Architecture)+'及以上的显卡用，这张 '+$generation+' 系用不上')}
    if($generation -in @(20,30) -and $bundled -and ($bundled.Major -lt 2 -or ($bundled.Major -eq 2 -and $bundled.Minor -lt 7)) -and -not $best.Usable){
        $lines+=@('提示（033 的推断，尚未在 '+$generation+' 系实机证实）：游戏自带的 Streamline 低于 2.7、缓存里也没有这张卡能用的新版，3× 以上的多帧可能开不出来；2× 不受影响。')
    }
    return $lines
}
function Get-033SystemDllDirectory([string]$Architecture='x64'){
    # Where Windows falls back to for a DLL that is neither loaded nor next to the game: the system
    # folder of the game's bitness (Sysnative from a 32-bit shell, SysWOW64 for x86 games). Tests pin it.
    if($script:K033SystemDllDirectory){return [string]$script:K033SystemDllDirectory}
    $windows=[Environment]::GetFolderPath('Windows')
    if(-not [Environment]::Is64BitOperatingSystem){return [Environment]::SystemDirectory}
    if($Architecture -eq 'x86'){return (Join-Path $windows 'SysWOW64')}
    if([Environment]::Is64BitProcess){return (Join-Path $windows 'System32')}
    return (Join-Path $windows 'Sysnative')
}
function Get-033GameFrameGenEvidence([string]$Dir,[int]$MaxDepth=3,[int]$MaxEntries=20000){
    # Does the game ship DLSS Frame Generation? sl.dlss_g.dll / nvngx_dlssg.dll in the folder or
    # up to three folders down, and for an Unreal Engine layout in the project's and the engine's
    # Plugins folders; other tools' folders never count. Read-only, bounded walk.
    $Dir=[IO.Path]::GetFullPath($Dir).TrimEnd('\')
    $names=@('sl.dlss_g.dll','nvngx_dlssg.dll');$skip=@($script:FOREIGN_TOOL_DIRS);$hits=@()
    $queue=[Collections.Generic.List[object]]::new();$queue.Add(@{Path=$Dir;Depth=0});$visited=0
    for($q=0;$q -lt $queue.Count -and $visited -lt $MaxEntries -and $hits.Count -lt 4;$q++){
        $node=$queue[$q];$children=@();try{$children=@(Get-ChildItem -LiteralPath $node.Path -Force -ErrorAction Stop)}catch{continue}
        foreach($c in $children){
            $visited++;if($visited -gt $MaxEntries){break}
            if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if($c.PSIsContainer){if(($c.Name -notin $skip) -and -not (Test-033SkipPackageDir $node.Path $c $node.Depth) -and $node.Depth -lt $MaxDepth){$queue.Add(@{Path=$c.FullName;Depth=$node.Depth+1})};continue}
            if($c.Name -in $names){$hits+=@($c.FullName.Substring($Dir.Length+1))}
        }
    }
    # 2026-09-19 「很多人反馈 30 系开不了帧生成」：虚幻引擎的 Streamline 不在主程序目录下面，而在 <项目>\Plugins 或 <根>\Engine\Plugins 里
    # （三角洲行动：DeltaForce\Plugins\Runtime\Nvidia\Sreamline\Binaries\ThirdParty\Win64；王者荣耀世界：Engine\Plugins\Runtime\Nvidia\StreamlineCore\…）。
    # 只从主程序目录往下找三层就永远找不到，20/30 系的转接件于是被判「游戏没有自带 DLSS 帧生成」而不装。
    # Get-033NativeDlssEvidence 在 09-12 已经补过同一处，这里照它再看这两处（同样有上限，别的工具的目录照样不算）。
    if(-not $hits.Count -and $Dir -match '(?i)\\Binaries\\Win(64|32)$'){
        $project=Split-Path -Parent (Split-Path -Parent $Dir);$ueRoot=$(if($project){Split-Path -Parent $project}else{''})
        foreach($pluginRoot in @($(if($project){Join-Path $project 'Plugins'}),$(if($ueRoot){Join-Path $ueRoot 'Engine\Plugins'}))){
            if(-not $pluginRoot -or -not(Test-Path -LiteralPath $pluginRoot -PathType Container)){continue}
            $pq=[Collections.Generic.List[object]]::new();$pq.Add(@{Path=$pluginRoot;Depth=0});$pv=0
            for($k=0;$k -lt $pq.Count -and $pv -lt $MaxEntries -and $hits.Count -lt 4;$k++){
                $pn=$pq[$k];$pc=@();try{$pc=@(Get-ChildItem -LiteralPath $pn.Path -Force -ErrorAction Stop)}catch{continue}
                foreach($c in $pc){
                    $pv++;if($pv -gt $MaxEntries){break}
                    if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
                    if($c.PSIsContainer){if(($c.Name -notin $skip) -and $pn.Depth -lt 8){$pq.Add(@{Path=$c.FullName;Depth=$pn.Depth+1})};continue}
                    if($c.Name -in $names){$hits+=@($c.FullName)}
                }
            }
        }
    }
    # 2026-09-19 审判之眼：目录里那个 nvngx_dlssg.dll 是 DLSS 更新工具放进去的，游戏本身没有帧生成。DLSS 帧生成只能经
    # Streamline 运行，所以「游戏自带帧生成」的凭据是 sl.dlss_g.dll；单独一个模型文件不算。Present 保持原义（转接件那条
    # 规则用它），Streamline 给「033 帧生成准备」和报告用。
    $streamline=(@($hits|Where-Object {[IO.Path]::GetFileName([string]$_) -ieq 'sl.dlss_g.dll'}).Count -gt 0)
    [pscustomobject]@{Present=($hits.Count -gt 0);Streamline=$streamline;Evidence=@($hits)}
}
function Get-033SiblingExecutables([string]$Dir,[int]$Max=48){
    $rows=@();$queue=@(@{Path=$Dir;Depth=0});$seen=0
    for($q=0;$q -lt $queue.Count -and $seen -lt $Max;$q++){
        $node=$queue[$q]
        $children=@();try{$children=@(Get-ChildItem -LiteralPath $node.Path -Force -ErrorAction Stop)}catch{continue}
        foreach($c in $children){
            if($c.PSIsContainer){if($node.Depth -lt 2 -and $c.Name -notin @('_DLSS5_备份','_033transactions','reshade-shaders','033-runtime','crashreport')){$queue+=@(@{Path=$c.FullName;Depth=$node.Depth+1})};continue}
            if($c.Extension -ine '.exe'){continue}
            $seen++;if($seen -gt $Max){break}
            $pe=Get-033PeInfo $c.FullName
            $apis=@();if($pe.Status -eq 'valid'){$apis=@(Get-033ApiNames (@($pe.Imports)+@($pe.DelayImports)))}
            $rows+=@([pscustomobject]@{Path=$c.FullName;Name=$c.Name;Size=$c.Length;Architecture=$pe.Architecture;Status=$pe.Status;Apis=$apis;Graphics=@($apis|Where-Object {$_ -ne 'dxgi'})})
        }
    }
    return $rows
}
function Test-033FileNeedles([string]$Path,[string[]]$Needles,[long]$MaxBytes=268435456,[switch]$Wide){
    # Chunked needle scan (4 MB reads, 64-byte overlap) so a 200 MB game exe is
    # searched without being held in memory. Bytes are read as Latin-1 (one char per
    # byte), so -Wide can also match the UTF-16LE spelling of each needle (RE Engine
    # keeps "d3d12.dll" only as a wide string). Read-only; nothing is loaded.
    $latin=[Text.Encoding]::GetEncoding(28591)
    $forms=@{};foreach($m in $Needles){$forms[$m]=@($m);if($Wide){$forms[$m]+=@((-join ($m.ToCharArray()|ForEach-Object {[string]$_+[char]0})))}}
    $found=@();$fs=$null
    try{
        $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $buf=[byte[]]::new(4194304);$tail='';$read=[long]0
        while(($n=$fs.Read($buf,0,$buf.Length)) -gt 0){
            $read+=$n;$s=$tail+$latin.GetString($buf,0,$n)
            foreach($m in $Needles){
                if($found -contains $m){continue}
                foreach($form in $forms[$m]){if($s.IndexOf($form,[StringComparison]::Ordinal) -ge 0){$found+=@($m);break}}
            }
            if($found.Count -eq $Needles.Count -or $read -ge $MaxBytes){break}
            $tail=$(if($s.Length -gt 64){$s.Substring($s.Length-64)}else{$s})
        }
    }catch{}finally{if($fs){$fs.Dispose()}}
    return $found
}
function Get-033SurveyToolEvidence([IO.FileInfo]$File){
    # Runtime proxies advertise every API they can hook. Those strings describe
    # the tool, not the selected game's renderer. Only attribute known tool
    # identities at loader/tool names; an actual SKSE/DLSS consumer still counts.
    if($File.Extension -ine '.dll'){return $null}
    $proxyNames=@('dxgi.dll','d3d11.dll','d3d12.dll','d3d10.dll','d3d10_1.dll','d3d9.dll','d3d8.dll','ddraw.dll','opengl32.dll','vulkan-1.dll',
        'winmm.dll','version.dll','winhttp.dll','dinput8.dll','dbghelp.dll','dsound.dll','xinput1_3.dll','xinput1_4.dll','xinput9_1_0.dll','nvngx.dll',
        'ReShade64.dll','ReShade32.dll','OptiScaler.dll','SpecialK64.dll','SpecialK32.dll')
    if($File.Name -notin $proxyNames -and $File.Name -notmatch '^033-'){return $null}
    $id=Get-033ProxyIdentity $File.FullName
    if($id.Kind -in @('033-fork','reshade','optiscaler','specialk','dxvk','enb','dgvoodoo','reframework','nvidia-mfg-bridge')){
        return [pscustomobject]@{Path=$File.FullName;Kind=$id.Kind;Reason='工具自身的接口能力，不作为游戏自带 DLSS／DX12 的证据'}
    }
    return $null
}
function Get-033Dx12RuntimeEvidence([string]$Dir,[string]$Exe,[int]$MaxEntries=20000){
    # Does a D3D11-importing game actually run Direct3D 12? The v5 installer's signs,
    # any one of which forbids the DX11 bridge (鬼武者 / 生化危机4 lessons: a wrong
    # bridge fights the game for NGX feature 18 and ghosts): an Agility SDK
    # (D3D12Core.dll up to two folders down, or a D3D12 folder), DXC shader-compiler
    # files, or the string d3d12.dll (ASCII or wide) inside the main exe / largest
    # binaries. Read-only, bounded walk.
    $Dir=[IO.Path]::GetFullPath($Dir).TrimEnd('\')
    $skipDirs=@($script:FOREIGN_TOOL_DIRS)
    $ownNames=@('nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx_dlssg.dll','dxgi.dll','renodx-dlss5.addon64','dlss5-feed.addon64','dlss5-feed.addon32','dlss5-feed-host64.exe',
                'dlss5-033.addon64','dlss5-guide.addon64','nvngx.dll_033.dll','ReShade32.dll','renodx-dlss5-dxcbridge.addon64','dlss5-bridge.addon64','033-engine.dll','033-framegen-provider.dll','D3D8.dll','D3D9.dll','dinput8.dll','nvngx_dlssg.sm75.dll','nvngx_dlssg.sm86.dll')
    $evidence=@();$kind='none';$toolEvidence=@()
    foreach($n in @('D3D12Core.dll','D3D12SDKLayers.dll')){if(Test-Path -LiteralPath (Join-Path $Dir $n) -PathType Leaf){$evidence+=@($n)}}
    if(Test-Path -LiteralPath (Join-Path $Dir 'D3D12') -PathType Container){$evidence+=@('D3D12\')}
    $binaries=[Collections.Generic.List[object]]::new()
    $queue=[Collections.Generic.List[object]]::new();$queue.Add(@{Path=$Dir;Depth=0});$visited=0
    for($q=0;$q -lt $queue.Count -and $visited -lt $MaxEntries;$q++){
        $node=$queue[$q];$children=@();try{$children=@(Get-ChildItem -LiteralPath $node.Path -Force -ErrorAction Stop)}catch{continue}
        foreach($c in $children){
            $visited++;if($visited -gt $MaxEntries){break}
            if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if($c.PSIsContainer){if(($c.Name -notin $skipDirs) -and -not (Test-033SkipPackageDir $node.Path $c $node.Depth) -and $node.Depth -lt 3){$queue.Add(@{Path=$c.FullName;Depth=$node.Depth+1})};continue}
            if($node.Depth -ge 1 -and $node.Depth -le 2 -and $c.Name -ieq 'D3D12Core.dll'){$evidence+=@($c.FullName.Substring($Dir.Length+1))}
            if($c.Extension -in @('.exe','.dll') -and ($c.Name -notin $ownNames)){$binaries.Add($c)}
        }
    }
    $evidence=@($evidence|Select-Object -Unique)
    if($evidence.Count){$kind='agility-sdk'}
    else{
        foreach($n in @('dxil.dll','dxcompiler.dll')){if(Test-Path -LiteralPath (Join-Path $Dir $n) -PathType Leaf){$evidence+=@($n)}}
        if($evidence.Count){$kind='dxc-files'}
    }
    if($kind -eq 'none'){
        $ordered=@();if($Exe -and (Test-Path -LiteralPath $Exe -PathType Leaf)){$ordered+=@(Get-Item -LiteralPath $Exe)}
        $ordered+=@($binaries|Where-Object {$_.FullName -ine $Exe}|Sort-Object Length -Descending|Select-Object -First 12)
        foreach($b in $ordered){
            $m=Test-033FileNeedles $b.FullName @('d3d12.dll') -Wide
            if(@($m).Count){$tool=Get-033SurveyToolEvidence $b;if($tool){$toolEvidence+=@($tool);continue};$kind='strings';$evidence=@($b.FullName.Substring($Dir.Length+1)+'（含 d3d12.dll 字样）');break}
        }
    }
    [pscustomobject]@{Present=($kind -ne 'none');Kind=$kind;Evidence=@($evidence|Select-Object -First 6);ToolBinaryEvidence=$toolEvidence}
}
function Get-033NativeDlssEvidence([string]$Dir,[string]$Exe,[string[]]$ManagedPaths=@(),[int]$MaxDepth=6,[int]$MaxEntries=20000){
    # Does the game ship its own DLSS/Streamline? Three looks, in the order the v5
    # installer learned them the hard way: root files (exact attribution lives in
    # Get-033UpscalerStack), the same file names up to six folders down (Dragon's
    # Dogma 2 / Witcher 3 keep sl.dlss.dll at 0.2 MB in subfolders; UE plugins keep
    # nvngx_dlss.dll under Plugins/DLSS/Binaries/ThirdParty/Win64), then NGX SDK
    # strings inside the main exe and the largest game binaries. Our own files and
    # our own folders never count. Bounded walk; nothing is loaded or executed.
    $Dir=[IO.Path]::GetFullPath($Dir).TrimEnd('\')
    $dlssNames=@('nvngx_dlss.dll','nvngx_dlssd.dll','nvngx_dlssg.dll','sl.dlss.dll','sl.dlss_d.dll','sl.dlss_g.dll','sl.interposer.dll','sl.common.dll')
    $ownNames=@('nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx_dlssg.dll','dxgi.dll','renodx-dlss5.addon64','dlss5-feed.addon64','dlss5-feed.addon32','dlss5-feed-host64.exe',
                'dlss5-033.addon64','dlss5-guide.addon64','nvngx.dll_033.dll','ReShade32.dll','renodx-dlss5-dxcbridge.addon64','dlss5-bridge.addon64','033-engine.dll','033-framegen-provider.dll','D3D8.dll','D3D9.dll','nvngx_dlssg.sm75.dll','nvngx_dlssg.sm86.dll')
    $skipDirs=@($script:FOREIGN_TOOL_DIRS)
    $managed=@{};foreach($m in $ManagedPaths){$managed[([string]$m).Replace('/','\').ToLowerInvariant()]=$true}
    $hits=@();$binaries=[Collections.Generic.List[object]]::new()
    $queue=[Collections.Generic.List[object]]::new();$queue.Add(@{Path=$Dir;Depth=0});$visited=0
    for($q=0;$q -lt $queue.Count -and $visited -lt $MaxEntries;$q++){
        $node=$queue[$q]
        $children=@();try{$children=@(Get-ChildItem -LiteralPath $node.Path -Force -ErrorAction Stop)}catch{continue}
        foreach($c in $children){
            $visited++;if($visited -gt $MaxEntries){break}
            if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if($c.PSIsContainer){
                if($c.Name -in $skipDirs -or (Test-033SkipPackageDir $node.Path $c $node.Depth)){continue}
                if($node.Depth -lt $MaxDepth){$queue.Add(@{Path=$c.FullName;Depth=$node.Depth+1})}
                continue
            }
            $rel=$c.FullName.Substring($Dir.Length+1)
            if($managed.ContainsKey($rel.ToLowerInvariant())){continue}
            if($c.Name -in $dlssNames){$hits+=@($rel)}
            if($node.Depth -le 3 -and $c.Extension -in @('.exe','.dll') -and ($c.Name -notin $ownNames)){$binaries.Add($c)}
        }
    }
    # 2026-09-12 黑神话评论区：UE 游戏的 nvngx_dlss 常在 <根>\Engine\Plugins 或 <项目>\Plugins 下，不在主程序目录里，只从主程序目录往下找就漏了，
    # 被当成「不带 DLSS」走了 033 自带 DLSS 的路线。主程序在 <根>\<项目>\Binaries\Win64 时，再看这两处（有上限）；里面的 DLL 也拿去扫 NGX 字样。
    if(-not $hits.Count -and $Dir -match '(?i)\\Binaries\\Win(64|32)$'){
        $project=Split-Path -Parent (Split-Path -Parent $Dir);$ueRoot=Split-Path -Parent $project
        foreach($pluginRoot in @((Join-Path $project 'Plugins'),(Join-Path $ueRoot 'Engine\Plugins'))){
            if(-not $ueRoot -or -not(Test-Path -LiteralPath $pluginRoot -PathType Container)){continue}
            $pq=[Collections.Generic.List[object]]::new();$pq.Add(@{Path=$pluginRoot;Depth=0});$pv=0
            for($k=0;$k -lt $pq.Count -and $pv -lt $MaxEntries;$k++){
                $pn=$pq[$k];$pc=@();try{$pc=@(Get-ChildItem -LiteralPath $pn.Path -Force -ErrorAction Stop)}catch{continue}
                foreach($c in $pc){
                    $pv++;if($pv -gt $MaxEntries){break}
                    if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
                    if($c.PSIsContainer){if($pn.Depth -lt 8){$pq.Add(@{Path=$c.FullName;Depth=$pn.Depth+1})};continue}
                    if($c.Name -in $dlssNames){$hits+=@($c.FullName)}
                    if($c.Extension -eq '.dll' -and ($c.Name -notin $ownNames)){$binaries.Add($c)}
                }
            }
        }
    }
    # Always ask the binaries too, even when a DLSS file is sitting in the folder.
    # A shipped file is not proof the game uses it: 巫师3 的 DX11 入口和古剑奇谭三
    # 目录里都躺着一个 nvngx_dlss.dll, 而那两个程序里 NGX 字样是 0 -- 只看文件就把
    # 它们判成"自带 DLSS", 于是选错路(巫师3)或直接拒装(古剑三)。反过来也不能只看
    # 主程序: UE 的 DLSS 插件在单独 DLL 里, 所以连同最大的那几个二进制一起看。
    $needles=@('NVSDK_NGX','nvngx_dlss','sl.interposer')
    $binaryEvidence=@();$binaryUsesNgx=$false;$capabilityEvidence=@();$toolEvidence=@()
    $ordered=@();if($Exe -and (Test-Path -LiteralPath $Exe -PathType Leaf)){$ordered+=@(Get-Item -LiteralPath $Exe)}
    # 2026-09-11 审判之眼实测闪退: 世嘉自带的 DLSS 插件 pfx_dlss.dll 只有 654 KB, 体积排第 22, 挤不进"最大的 12 个",
    # 于是被判成"游戏不用 DLSS"; Feeder 路线又自建了一套 DLSS, 两套同时在场 -> CreateFeature 0xC0000005, 崩在驱动的 _nvngx.dll。
    # 所以名字里带 dlss/ngx/upscal 的二进制一律先扫, 不受体积名次限制; 其余按体积取最大的 40 个。
    # 40 与 v5.0 的 Test-NativeDLSS 一致（用户 2026-09-11 定）：v5.0 正是靠前 40 扫到 pfx_dlss.dll（第 22）才在审判之眼上
    # 走了直挂；V3 重写时收成 12 个，才有了那两次闪退。读法是 4 MB 分块、找到就停，40 个不会慢多少。
    $byName=@($binaries|Where-Object {$_.FullName -ine $Exe -and $_.Name -match '(?i)(dlss|ngx|upscal)'}|Sort-Object Length -Descending)
    $ordered+=@($byName)
    $ordered+=@($binaries|Where-Object {$_.FullName -ine $Exe -and @($byName|ForEach-Object {$_.FullName}) -notcontains $_.FullName}|Sort-Object Length -Descending|Select-Object -First 40)
    foreach($b in $ordered){
        # v5.0 read each binary to the end; the 256 MB default cut large shipping executables short. Stops at the first hit.
        # Unity ships this NVIDIA bridge even in games that never ship or use DLSS.
        # Keep its evidence visible, but do not remove the Feeder based on this
        # helper alone. An actual DLSS runtime or another game NGX consumer wins.
        if($b.Name -ieq 'NVUnityPlugin.dll' -and -not $hits.Count){
            $capabilityEvidence+=@($b.FullName);continue
        }
        $m=@(Test-033FileNeedles $b.FullName $needles 4294967296)
        if($m.Count){$tool=Get-033SurveyToolEvidence $b;if($tool){$toolEvidence+=@($tool);continue};$binaryUsesNgx=$true;$binaryEvidence=@($(if($b.FullName.StartsWith($Dir+'\',[StringComparison]::OrdinalIgnoreCase)){$b.FullName.Substring($Dir.Length+1)}else{$b.FullName})+'（'+($m -join '、')+'）');break}
    }
    $binariesChecked=$ordered.Count
    $kind='none';$evidence=@()
    if($hits.Count){
        $kind=$(if(@($hits|Where-Object {$_ -notmatch '\\'}).Count){'root-files'}else{'deep-files'});$evidence=@($hits|Select-Object -First 8)
    }elseif($binaryUsesNgx){
        $kind='needle';$evidence=$binaryEvidence
    }
    # Files present but no binary in the game references NGX/Streamline at all:
    # the shipped file is dead weight and the game does not actually run DLSS.
    # Streamline runtime files are different: nothing ships sl.interposer / sl.dlss* / nvngx_dlssd unless the game wires
    # Streamline in, and v5.0 counted them by name alone. Demoting them as well is how 6.0 told a 鸣潮 player "no DLSS"
    # where v5.0 had recognised it (2026-09-11 comment). Only a bare nvngx_dlss.dll can be dead weight (巫师3 DX11, 古剑三),
    # so only that still needs a binary to confirm it.
    $slHits=@($hits|Where-Object {[IO.Path]::GetFileName($_) -in @('sl.dlss.dll','sl.dlss_d.dll','sl.dlss_g.dll','sl.interposer.dll','sl.common.dll','nvngx_dlssd.dll')})
    $filesUnused=($hits.Count -gt 0 -and -not $slHits.Count -and -not $binaryUsesNgx -and $binariesChecked -gt 0)
    [pscustomobject]@{Present=($kind -ne 'none');Kind=$kind;Evidence=$evidence;Scanned=$visited;
        BinaryUsesNgx=$binaryUsesNgx;BinaryEvidence=$binaryEvidence;BinariesChecked=$binariesChecked;ToolBinaryEvidence=$toolEvidence;
        CapabilityEvidence=$capabilityEvidence;CapabilityOnly=($capabilityEvidence.Count -gt 0 -and -not $binaryUsesNgx -and -not $hits.Count);
        FilesUnused=$filesUnused;EffectivePresent=(($kind -ne 'none') -and -not $filesUnused)}
}
function Test-033NativeDlssPresence([string]$Exe,[string[]]$ManagedPaths=@()){
    # 必须和侦察/判断同一个判据(EffectivePresent)。原来这里返回 .Present(只看文件), 判断却用
    # EffectivePresent: 目录里躺着 DLSS 文件、游戏却不用的(古剑三、巫师3 DX11), 报告说走 legacy,
    # 生成计划时选的却是 native —— 装进去的不是报告里那条(2026-09-11 查出)。
    # 计划阶段不传 ManagedPaths 也不会因此分歧: 我们自己装的 DLSS 文件在 033-runtime\ 里,
    # FOREIGN_TOOL_DIRS 本来就跳过它。
    [bool](Get-033NativeDlssEvidence (Split-Path -Parent $Exe) $Exe $ManagedPaths).EffectivePresent
}
function Find-033GameExeCandidates([string]$Root,[int]$MaxDepth=4,[int]$MaxEntries=20000){
    # Folder → game executable, by the v5 installer's field rules plus PE facts:
    # junk names (uninstallers, redists, launchers, crash handlers, anti-cheat…) and
    # runtime/tool folders are out; size is the strongest signal (10 points per MB —
    # 燕云's 60 MB yysls.exe must beat DirectX's tiny d3dconfig.exe); UE 'Binaries',
    # Win64/x64, dx12 folders and 'Win64-Shipping' add; Win32 subtracts; graphics-API
    # imports (direct or through a same-folder renderer module) add 600 so a
    # bootstrap exe never outranks the real game. Read-only PE header parsing only.
    $Root=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if(-not(Test-Path -LiteralPath $Root -PathType Container)){throw ('不是文件夹：'+$Root)}
    # 2026-09-17 Fable：暗黑4 实测 6.1.3 选中 BlizzardBrowser 目录里 2 MB 的浏览器壳，把 52 MB 的 Diablo IV.exe 当候选；5.0 按体积选对。
    #   浏览器/CEF/WebView/崩溃上报壳一律不算；图形导入的加分见下面的体积门槛。
    $junk='(?i)unins|setup|redist|vcredist|dxsetup|directx|crash|handler|report|launcher|prelauncher|updater|update|patcher|activation|benchmark|dotnet|oalinst|touchup|cleanup|helper|installer|unitycrash|eac|battleye|anticheat|svc|service|diag|d3dconfig|dxcap|pixtool|gpuview|physx|systemsoftware|dxwebsetup|browser|cefsubprocess|cef_|webview|crashpad|errorreport|blizzarderror|fenriserror|unrealcef|etwanalyzer'
    $junkDir='(?i)(^|\\)(redist|_?CommonRedist|DirectX|DotNet|PhysX|vcredist|Tools?|SDK|Support|Drivers?|_Redist|033-runtime|host64|_DLSS5_备份|_033transactions|reshade-shaders|\.git)(\\|$)'
    $queue=[Collections.Generic.List[object]]::new();$queue.Add(@{Path=$Root;Depth=0});$visited=0;$found=@()
    for($q=0;$q -lt $queue.Count -and $visited -lt $MaxEntries;$q++){
        $node=$queue[$q];$children=@();try{$children=@(Get-ChildItem -LiteralPath $node.Path -Force -ErrorAction Stop)}catch{continue}
        foreach($c in $children){
            $visited++;if($visited -gt $MaxEntries){break}
            if($c.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
            if($c.PSIsContainer){
                $relDir=$c.FullName.Substring($Root.Length)
                if($node.Depth -lt $MaxDepth -and ($relDir -notmatch $junkDir) -and -not (Test-033SkipPackageDir $node.Path $c $node.Depth)){$queue.Add(@{Path=$c.FullName;Depth=$node.Depth+1})}
                continue
            }
            if($c.Extension -ine '.exe' -or ($c.Name -match $junk)){continue}
            $found+=@($c)
        }
    }
    $big=@($found|Where-Object {$_.Length -gt 409600});if($big.Count){$found=$big}
    $maxLength=[long]0;foreach($c in $found){if([long]$c.Length -gt $maxLength){$maxLength=[long]$c.Length}}
    $rows=@()
    foreach($c in $found){
        $pe=Get-033PeInfo $c.FullName
        # 2026-09-12 只读全库选路扫描查出来的：逆转裁判调查档（Unity IL2CPP，GK12.exe）机器位数和「是不是 DLL」
        # 都读到了，只是导入目录的 RVA 在节表里算不出唯一归属，PE 被判成 unknown。这里原来只认 Status=valid，
        # 于是这类游戏【拖文件夹进来说找不到主程序】——而直接拖那个 exe 反而装得上（证据梯子后面的 Unity 标记
        # 照样判出 dx11）。挑主程序这一步只需要「是不是一个能跑的 exe」，位数读到了就够；读不出导入表只是少
        # 一层证据，评分时本来就拿不到那 600 分。PE 语义不动：加壳 exe 仍然是 unknown，装的时候照样提醒
        # 「文件头读不全」，手动选路那条路也不变。
        if($pe.IsDll -or $pe.Architecture -notin @('x86','x64')){continue}
        $imports=@($pe.Imports)+@($pe.DelayImports);$apis=@(Get-033ApiNames $imports);$via=$null
        if(-not @($apis|Where-Object {$_ -ne 'dxgi'}).Count){
            # A thin exe whose renderer lives in a same-folder module it imports.
            $dir=Split-Path -Parent $c.FullName;$n=0
            foreach($name in $imports){
                if($n -ge 16){break};if($name -match '[\\/:]'){continue}
                $modulePath=Join-Path $dir $name;if(-not(Test-Path -LiteralPath $modulePath -PathType Leaf)){continue};$n++
                $dpe=Get-033PeInfo $modulePath;if($dpe.Status -ne 'valid' -or $dpe.Architecture -ne $pe.Architecture){continue}
                $dapis=@(Get-033ApiNames (@($dpe.Imports)+@($dpe.DelayImports))|Where-Object {$_ -ne 'dxgi'})
                if($dapis.Count){$apis=@(@($apis)+@($dapis)|Sort-Object -Unique);$via=$name;break}
            }
        }
        $graphics=@($apis|Where-Object {$_ -ne 'dxgi'})
        $rel=$c.FullName.Substring($Root.Length)
        $s=[double]$c.Length/1MB*10
        if($rel -match '(?i)(^|\\)Binaries(\\|$)'){$s+=350}
        if($rel -match '(?i)Win64|x64|bin64|64bit'){$s+=400}
        if($rel -match '(?i)dx12|d3d12'){$s+=300}
        if($c.Name -match '(?i)Win64-Shipping'){$s+=500}
        if($rel -match '(?i)Win32|x86(?!_64)|bin32|32bit'){$s-=300}
        # 图形导入是真信号，但 5.0 的「体积是最强的真信号」也不能丢：一个不到最大候选十分之一的小壳，
        # 就算导入了 d3d11 也只加 100（暗黑4 的 BlizzardBrowser.exe），真身本来就 LoadLibrary 图形接口、导入表里看不见。
        if($graphics.Count){$s+=$(if([long]$c.Length*10 -ge $maxLength){600}else{100})}
        if($pe.Architecture -eq 'x64'){$s+=50}
        $act=$null;try{$newest=@(Get-ChildItem -LiteralPath (Split-Path -Parent $c.FullName) -File -Force -ErrorAction Stop|Select-Object -First 400|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1);if($newest.Count){$act=$newest[0].LastWriteTimeUtc.ToString('o')}}catch{}
        $rows+=@([pscustomobject]@{Path=$c.FullName;Relative=$rel.TrimStart('\');Name=$c.Name;Size=$c.Length;Architecture=$pe.Architecture;Apis=$apis;Graphics=$graphics;Via=$via;Score=[Math]::Round($s,1);LastActivityUtc=$act})
    }
    return @($rows|Sort-Object -Property @{Expression='Score';Descending=$true},@{Expression='Relative';Descending=$false})
}
function Test-033VaultKnowsExe([string]$Vault,[string]$Exe){
    # 已经按这个准确路径登记过的入口（比如手动模式装在启动器上的）不改道，否则卸载找不到账本。
    if(-not $Vault){return $false}
    $index=Join-Path $Vault 'index'
    if(-not(Test-Path -LiteralPath $index -PathType Container)){return $false}
    foreach($file in @(Get-ChildItem -LiteralPath $index -File -Filter '*.json' -ErrorAction SilentlyContinue)){
        try{if(([string](Read-033ManagedJson $file.FullName).Exe) -ieq $Exe){return $true}}catch{}
    }
    return $false
}
function Resolve-033GameExe([string]$Path,[string]$Vault){
    # A folder becomes its game executable: the entry this installer already
    # registered for that folder when the vault knows it, else the best candidate by
    # Find-033GameExeCandidates. Never guesses when nothing qualifies.
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    if(-not(Test-Path -LiteralPath $full)){throw ('找不到：'+$full)}
    if(Test-Path -LiteralPath $full -PathType Leaf){
        # v5.0「虚幻引擎的经典坑」：根目录那个 exe 只是启动器，真游戏在 XXX\Binaries\Win64\XXX-Win64-Shipping.exe，
        # 装在启动器旁边游戏根本不加载。V3 重写时丢了这条，于是启动器上只剩一句「游戏接口：。」（2026-09-11 评论区）。
        if([IO.Path]::GetFileName($full) -notmatch '(?i)-Shipping\.exe$' -and -not (Test-033VaultKnowsExe $Vault $full)){
            # Skyrim launchers and SKSE loaders are not the renderer. Match exact
            # launcher names to a same-directory main executable of the right bitness.
            # Never climb into a mod manager or select the other Skyrim edition.
            $skyrimLaunchers=@{
                'skyrimlauncher.exe'=@('TESV.exe','x86')
                'skse_loader.exe'=@('TESV.exe','x86')
                'skyrimselauncher.exe'=@('SkyrimSE.exe','x64')
                'skse64_loader.exe'=@('SkyrimSE.exe','x64')
                'sksevr_loader.exe'=@('SkyrimVR.exe','x64')
            }
            $skyrimName=[IO.Path]::GetFileName($full).ToLowerInvariant()
            if($skyrimLaunchers.ContainsKey($skyrimName)){
                $expected=$skyrimLaunchers[$skyrimName]
                $main=Join-Path (Split-Path -Parent $full) $expected[0]
                if(Test-Path -LiteralPath $main -PathType Leaf){
                    $mainPe=Get-033PeInfo $main
                    if($mainPe.Architecture -eq $expected[1] -and -not $mainPe.IsDll){
                        return [pscustomobject]@{Exe=$main;Input=$full;FromFolder=$false;Source='launcher-redirect';Candidates=@()}
                    }
                }
                return [pscustomobject]@{Exe=$full;Input=$full;FromFolder=$false;Source='given';Candidates=@()}
            }
            $ship=@(Get-ChildItem -LiteralPath (Split-Path -Parent $full) -Recurse -Depth 3 -File -Filter '*-Win64-Shipping.exe' -ErrorAction SilentlyContinue|Sort-Object Length -Descending|Select-Object -First 1)
            if($ship.Count){return [pscustomobject]@{Exe=$ship[0].FullName;Input=$full;FromFolder=$false;Source='ue-shipping';Candidates=@()}}
            # 2026-09-17 Fable：米哈游 HoYoPlay、鹰角 Hypergryph Launcher、网易启动器都把游戏放在启动器旁边的 games\<游戏> 里；
            #   玩家点到启动器 exe（HoYoPlay.exe 这类名字不带 launch）时，先在那个 games 目录里找真身，找到就改过去并列出候选。
            $launcherGames=Join-Path (Split-Path -Parent $full) 'games'
            if(Test-Path -LiteralPath $launcherGames -PathType Container){
                $gameRows=@();try{$gameRows=@(Find-033GameExeCandidates $launcherGames 5)}catch{$gameRows=@()}
                $gameRows=@($gameRows|Where-Object {$_.Path -ine $full})
                if($gameRows.Count){return [pscustomobject]@{Exe=$gameRows[0].Path;Input=$full;FromFolder=$false;Source='launcher-redirect';Candidates=@($gameRows|Select-Object -Skip 1 -First 5)}}
            }
            # 2026-09-17 Fable（5.0 仙剑4 规则）：选中的 exe 自己一个图形接口都不导入，而同目录另一个 exe 导入了，那才是真身
            #   （Pal4Revoke.exe 的导入表是空的，真正调 D3D9 的是同目录 launch.exe）。只看同一层目录，只看静态导入，不猜别处。
            $ownPe=Get-033PeInfo $full
            if($ownPe.Status -eq 'valid' -and -not @(Get-033ApiNames (@($ownPe.Imports)+@($ownPe.DelayImports))|Where-Object {$_ -ne 'dxgi'}).Count -and -not (Test-033UnityGame (Split-Path -Parent $full))){
                $junkSibling='(?i)unins|setup|redist|vcredist|dxsetup|dxwebsetup|crash|handler|report|updater|update|patcher|installer|eac|battleye|anticheat|d3dconfig|dgvoodoocpl|browser|webview'
                $siblingRows=@()
                foreach($x in @(Get-ChildItem -LiteralPath (Split-Path -Parent $full) -File -Filter '*.exe' -Force -ErrorAction SilentlyContinue|Where-Object {$_.FullName -ine $full -and $_.Length -gt 409600 -and $_.Name -notmatch $junkSibling})){
                    $xpe=Get-033PeInfo $x.FullName;if($xpe.Status -ne 'valid' -or $xpe.IsDll -or $xpe.Architecture -ne $ownPe.Architecture){continue}
                    if(@(Get-033ApiNames (@($xpe.Imports)+@($xpe.DelayImports))|Where-Object {$_ -ne 'dxgi'}).Count){$siblingRows+=@($x)}
                }
                if($siblingRows.Count){
                    $best=@($siblingRows|Sort-Object Length -Descending)[0]
                    return [pscustomobject]@{Exe=$best.FullName;Input=$full;FromFolder=$false;Source='launcher-redirect';Candidates=@($siblingRows|Where-Object {$_.FullName -ine $best.FullName}|ForEach-Object {[pscustomobject]@{Path=$_.FullName;Relative=$_.Name;Name=$_.Name;Size=$_.Length;Architecture=$null;Graphics=@();LastActivityUtc=$null}})}
                }
            }
            # 2026-09-12 燕云评论区：玩家选了 D:\yysls\Win32\deploy\launcher.exe（启动器开着；装在它旁边游戏也不会加载）。
            # 只在启动器所属目录内找。通用向上三层会进入 Steam/common 等共享游戏库，
            # 把另一个游戏的 DD2.exe / yysls.exe 当成目标；最近活动时间不能证明它属于当前游戏。
            # 唯一需要上移的已知布局是燕云 Win32/deploy，准确退回这两层后不再向上。
            if([IO.Path]::GetFileName($full) -match '(?i)launch|updat|patch|bootstrap'){
                $cur=Split-Path -Parent $full
                if((Split-Path -Leaf $cur) -ieq 'deploy'){
                    $platform=Split-Path -Parent $cur
                    if((Split-Path -Leaf $platform) -ieq 'Win32'){
                        $gameRoot=Split-Path -Parent $platform
                        if($gameRoot -and $gameRoot.Length -gt 3){$cur=$gameRoot}
                    }
                }
                $hits=@()
                foreach($n in $script:K033KnownGameExeNames){$hits+=@(Get-ChildItem -LiteralPath $cur -Recurse -Depth 5 -File -Filter $n -ErrorAction SilentlyContinue)}
                if($hits.Count){
                    $ranked=@($hits|Sort-Object @{Expression={$w=[datetime]::MinValue;try{$w=@(Get-ChildItem -LiteralPath $_.DirectoryName -File -Force -ErrorAction Stop|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1)[0].LastWriteTimeUtc}catch{};$w};Descending=$true})
                    return [pscustomobject]@{Exe=$ranked[0].FullName;Input=$full;FromFolder=$false;Source='launcher-redirect';Candidates=@($ranked|Select-Object -Skip 1|ForEach-Object {[pscustomobject]@{Path=$_.FullName;Relative=$_.FullName.Substring($cur.Length).TrimStart('\');Name=$_.Name;Size=$_.Length;Architecture=$null;Graphics=@();LastActivityUtc=$null}})}
                }
            }
        }
        return [pscustomobject]@{Exe=$full;Input=$full;FromFolder=$false;Source='given';Candidates=@()}
    }
    $known=@()
    if($Vault){
        $index=Join-Path $Vault 'index'
        if(Test-Path -LiteralPath $index -PathType Container){
            foreach($file in @(Get-ChildItem -LiteralPath $index -File -Filter '*.json' -ErrorAction SilentlyContinue)){
                try{$record=Read-033ManagedJson $file.FullName;$exe=[string]$record.Exe;if($exe -and ((Split-Path -Parent $exe) -ieq $full) -and (Test-Path -LiteralPath $exe -PathType Leaf)){$known+=@($exe)}}catch{}
            }
        }
    }
    $known=@($known|Sort-Object -Unique)
    if($known.Count -eq 1){return [pscustomobject]@{Exe=$known[0];Input=$full;FromFolder=$true;Source='vault-index';Candidates=@()}}
    $rows=@(Find-033GameExeCandidates $full)
    if(-not $rows.Count){throw ('这个文件夹里没找到像样的游戏主程序：'+$full+'。请直接选择游戏的 .exe。')}
    # Two near-identical builds in different folders (燕云's Win64r / Win64rh): 5.0 took the one with the most recent
    # activity and listed the other (2026-09-12 回退到 5.0 行为，不再拒绝)。
    if($rows.Count -ge 2 -and $rows[0].Graphics.Count -and $rows[1].Graphics.Count -and ([Math]::Abs($rows[0].Score-$rows[1].Score) -le 60) -and ((Split-Path -Parent $rows[0].Path) -ine (Split-Path -Parent $rows[1].Path))){
        $pair=@(@($rows|Select-Object -First 2)|Sort-Object @{Expression={if($_.LastActivityUtc){[datetime]$_.LastActivityUtc}else{[datetime]::MinValue}};Descending=$true})
        return [pscustomobject]@{Exe=$pair[0].Path;Input=$full;FromFolder=$true;Source='folder-scan-newest';Candidates=@($rows|Select-Object -First 6)}
    }
    [pscustomobject]@{Exe=$rows[0].Path;Input=$full;FromFolder=$true;Source='folder-scan';Candidates=@($rows|Select-Object -First 6)}
}
$script:K033KnownGameExeNames=@('yysls.exe','DD2.exe')
# 2026-09-12 业主「所有游戏都要挂对，网络上全查清」：逐游戏名单 game_rules.json，由 tools/build_game_rules.py 生成
# （ReShade 官方兼容名单 + 5.0 逐游戏表 + OptiScaler 兼容表里对得上主程序名的 + 评论区实测）。缺文件或读坏了就当没有名单。
$script:K033GameRulesPath=Join-Path $PSScriptRoot 'game_rules.json'
$script:K033GameRulesCache=$null
$script:K033GameRulesOverride=$null
# 别人的模组：它们占着的挂载名不覆盖（5.0「避开已占用挂载名」）。
$script:K033ForeignModKinds=@('optiscaler','specialk','dxvk','enb','dgvoodoo','reframework','nvidia-mfg-bridge')
function Get-033GameRules{
    if($null -ne $script:K033GameRulesOverride){return $script:K033GameRulesOverride}
    if($null -ne $script:K033GameRulesCache){return $script:K033GameRulesCache}
    $rules=@{}
    try{
        if(Test-Path -LiteralPath $script:K033GameRulesPath -PathType Leaf){
            $data=Read-033ManagedJson $script:K033GameRulesPath
            if($data.Schema -eq 1 -and $data.Kind -ceq '033-game-rules' -and $data.ContainsKey('Games')){$rules=$data.Games}
        }
    }catch{$rules=@{}}
    $script:K033GameRulesCache=$rules
    return $rules
}
function Get-033GameRule([string]$Exe){
    if(-not $Exe){return $null}
    $rules=Get-033GameRules;$name=[IO.Path]::GetFileName($Exe).ToLowerInvariant()
    if($rules -and $rules.ContainsKey($name)){return $rules[$name]}
    return $null
}
function Format-033GameRuleSources($Rule){
    $names=@{'reshade'='ReShade 官方兼容名单';'optiscaler'='OptiScaler 兼容表';'v5'='5.0 逐游戏表';'033'='评论区实测'}
    $src=@();if($Rule -and $Rule.ContainsKey('Src')){$src=@($Rule.Src|ForEach-Object {if($names.ContainsKey([string]$_)){$names[[string]$_]}else{[string]$_}})}
    if(-not $src.Count){return '逐游戏名单'}
    return ($src -join ' + ')
}
# 2026-09-12 评论区最大的一类「装完进不去游戏 / 一进就闪退」（FF7 重制版、生化危机4、燕云、上古卷轴5、地平线4…）：
# 033 的补帧路线记在【所有游戏共用的一份】全局设置 %LOCALAPPDATA%\033Runtime\settings.ini 里。旧版本只要打开过
# 一次面板，就会把它自动写成「通用补帧」(fg_route=1)；下次启动时通用补帧要替换游戏的交换链，本身没有帧生成的
# 游戏就崩在启动。新核心不再自动写坏它，但救不了已经被写坏的人——他们进不去游戏，也就打不开面板改回来。
# 玩家只能去评论区抄别人转发的记事本改法。所以这里【直接替他改好】，不是提醒他自己改：
#   fg_route=0     回到原生路线（不再替换交换链）
#   fg_automatic=0 不让旧核心（生化危机4、燕云这两条线没换核心）下次开面板时又自动写回 1
# 其余的键一个不碰（神经渲染的层数、强度、调色全保留），改之前把原件抄进备份库，随时能翻回去。
# 写法：整份读出来只换这两行，先写同目录临时文件再原子替换，中途断电也不会剩半份。
function Repair-033SharedFgRoute([string]$Vault){
    $path=Join-Path $env:LOCALAPPDATA '033Runtime\settings.ini'
    if(-not [IO.File]::Exists($path)){return $null}
    $text=$null
    try{$text=[IO.File]::ReadAllText($path)}catch{return $null}
    if($text -notmatch '(?m)^[ \t]*fg_route[ \t]*=[ \t]*1[ \t]*\r?$' -and $text -notmatch '(?m)^[ \t]*fg_automatic[ \t]*=[ \t]*1[ \t]*\r?$'){return $null}
    # 文件是 CRLF：.NET 多行模式里 $ 匹配在 \n 之前，\r 仍属于这一行，所以末尾那组要把 \r 算进去。
    $fixed=[regex]::Replace($text,'(?m)^([ \t]*fg_route[ \t]*=[ \t]*)1([ \t]*\r?)$','${1}0${2}')
    $fixed=[regex]::Replace($fixed,'(?m)^([ \t]*fg_automatic[ \t]*=[ \t]*)1([ \t]*\r?)$','${1}0${2}')
    if($fixed -ceq $text){return $null}
    try{
        if($Vault){
            $keep=Join-Path $Vault ('shared-settings/'+(Get-Date).ToString('yyyyMMdd-HHmmss'))
            [void][IO.Directory]::CreateDirectory($keep)
            [IO.File]::Copy($path,(Join-Path $keep 'settings.ini'),$true)
        }
    }catch{}
    $tmp=$path+'.033new'
    try{
        [IO.File]::WriteAllText($tmp,$fixed,[Text.UTF8Encoding]::new($false))
        # 第三个参数是「备份成哪个文件」，不要备份就得传【真正的 null】：PowerShell 把 $null 传给
        # .NET 的字符串参数会变成空字符串，File.Replace 当场抛「The path is empty」。
        [IO.File]::Replace($tmp,$path,[NullString]::Value)
    }catch{
        try{if([IO.File]::Exists($tmp)){[IO.File]::Delete($tmp)}}catch{}
        return $null
    }
    return '一个跟这个游戏无关的老毛病。全局设置（'+$path+'）里「033 通用补帧」被旧版本设成了默认路线，所有游戏共用这一项，本身没有帧生成的游戏会因此进不去或者闪退。已经改回原生路线，你调好的神经渲染参数一个没动，原件也抄了一份进备份库。033 自己的通用补帧这一版已经整项下线，不会再有游戏因为它进不去。'
}
function Get-033InstallRoot([string]$Exe){
    # ReShade 官方名单的 InstallTarget：这几个游戏从主程序目录下的子目录加载 DLL（Source 引擎的 bin、泰坦陨落2 的 bin\x64_retail），装在主程序旁边不会被加载。
    $dir=Split-Path -Parent ([IO.Path]::GetFullPath($Exe))
    $rule=Get-033GameRule $Exe
    if($rule -and $rule.ContainsKey('InstallTarget')){
        $it=Join-Path $dir ([string]$rule.InstallTarget)
        if(Test-Path -LiteralPath $it -PathType Container){return @{Root=(Get-033ManagedRoot $it);Target=[string]$rule.InstallTarget}}
    }
    return @{Root=(Get-033ManagedRoot $dir);Target=$null}
}
function Assert-033ProxyNotForeign([string]$Root,[string]$Name){
    # 5.0：别人的模组（OptiScaler / Special K / DXVK / ENB …）占着的名字，手工指定也不覆盖。
    $p=Join-Path $Root $Name
    if(-not(Test-Path -LiteralPath $p -PathType Leaf)){return}
    $id=Get-033ProxyIdentity $p
    if($id.Kind -in $script:K033ForeignModKinds){throw ('不能把挂载点设成 '+$Name+'：它现在是别的模组（'+$id.Kind+'），覆盖了它就停了。先用那个模组自己的卸载方式腾出这个名字，或者换一个挂载点。')}
}
function Format-033Resolution($Resolution){
    if(-not $Resolution){return ''}
    if($Resolution.Source -eq 'launcher-redirect'){
        $more=$(if(@($Resolution.Candidates).Count){[Environment]::NewLine+'另外还有：'+((@($Resolution.Candidates)|ForEach-Object {$_.Path}) -join '、')+'（选错了就直接选那个 .exe）'}else{''})
        return ('== 选的是启动器 =='+[Environment]::NewLine+'你选的是：'+$Resolution.Input+[Environment]::NewLine+'真正的游戏程序在：'+$Resolution.Exe+'（已自动改到游戏主程序；安装器据此判断位数和图形接口）'+$more+[Environment]::NewLine)
    }
    if($Resolution.Source -eq 'ue-shipping'){
        return ('== 虚幻引擎游戏 =='+[Environment]::NewLine+'你选的是启动器：'+$Resolution.Input+[Environment]::NewLine+'真正的游戏程序在：'+$Resolution.Exe+'（已自动改到这里；装在启动器旁边游戏不会加载）'+[Environment]::NewLine)
    }
    if(-not $Resolution.FromFolder){return ''}
    $sb=[Text.StringBuilder]::new()
    [void]$sb.AppendLine('== 从文件夹找到游戏主程序 ==')
    [void]$sb.AppendLine('文件夹：'+$Resolution.Input)
    [void]$sb.AppendLine('选中：'+$Resolution.Exe+$(if($Resolution.Source -eq 'vault-index'){'（本安装器已登记的入口）'}else{''}))
    $others=@($Resolution.Candidates|Where-Object {$_.Path -ine $Resolution.Exe})
    if($others.Count){
        [void]$sb.AppendLine('其他候选（选错了就直接选正确的 .exe）：')
        foreach($o in $others){[void]$sb.AppendLine('  - '+$o.Relative+'  '+[Math]::Round($o.Size/1MB,1)+' MB  '+$o.Architecture+$(if($o.Graphics.Count){'  '+($o.Graphics -join '+')}else{''})+$(if($o.PSObject.Properties['LastActivityUtc'] -and $o.LastActivityUtc){'  最近活动 '+([datetime]$o.LastActivityUtc).ToLocalTime().ToString('yyyy-MM-dd HH:mm')}else{''}))}
    }
    $sb.ToString()
}
function Get-033AntiCheatEvidence([string]$Dir){
    # v5.0 的反作弊提醒，V3 重写时丢了（2026-09-11：鸣潮带 ACE，6.0 一句没提）。业主 2026-09-05 定：只提醒、照样装。
    # 往上翻到游戏根：UE 真身在 <根>\Client\Binaries\Win64，反作弊装在根目录，中间隔三层。
    # 不越过库目录（common / steamapps / SteamLibrary / Games）也不碰盘符根——v5.0 那句停止条件被注释吞掉了，会扫到盘根。
    $hits=@();$probe=@();$cur=[IO.Path]::GetFullPath($Dir).TrimEnd('\')
    for($i=0;$i -le 4 -and $cur;$i++){
        if(([IO.Path]::GetPathRoot($cur).TrimEnd('\') -ieq $cur) -or ((Split-Path -Leaf $cur) -in @('common','steamapps','SteamLibrary','Games'))){break}
        $probe+=@($cur);$cur=Split-Path -Parent $cur
    }
    foreach($base in $probe){
        foreach($sig in @('EasyAntiCheat','EasyAntiCheat_EOS','BattlEye','EA AntiCheat','AntiCheatExpert','ACE-BASE','ACE','TenProtect','nProtect','anticheat_win64','AntiCheat','GameGuard','GameMon','npgg')){
            if(Test-Path -LiteralPath (Join-Path $base $sig)){$hits+=@($sig)}
        }
        foreach($pat in @('EAAntiCheat*','*_BE.exe','ACE-*.sys','ACE-*.exe','TenioDL*','*ACE-Guard*','anticheat*.dll','GameGuard.des','npgg*.des','GameMon*.des','GGSetup.exe','start_protected_game.exe')){
            if(@(Get-ChildItem -LiteralPath $base -Filter $pat -Force -ErrorAction SilentlyContinue|Select-Object -First 1).Count){$hits+=@($pat)}
        }
    }
    return @($hits|Sort-Object -Unique)
}
function Test-033UnityGame([string]$Dir){
    # 5.0 的 Unity 判据（dlss5_install.ps1 1397-1411）：UnityPlayer.dll、*_Data 里的标记、Unity 崩溃处理器。
    if(Test-Path -LiteralPath (Join-Path $Dir 'UnityPlayer.dll') -PathType Leaf){return $true}
    foreach($dd in @(Get-ChildItem -LiteralPath $Dir -Directory -Filter '*_Data' -Force -ErrorAction SilentlyContinue)){
        foreach($mark in @('globalgamemanagers','data.unity3d','il2cpp_data','Managed','resources.assets','boot.config')){if(Test-Path -LiteralPath (Join-Path $dd.FullName $mark)){return $true}}
    }
    return ((Test-Path -LiteralPath (Join-Path $Dir 'UnityCrashHandler64.exe')) -or (Test-Path -LiteralPath (Join-Path $Dir 'UnityCrashHandler32.exe')))
}
function Get-033GraphicsEvidence([string]$Exe){
    # 2026-09-11 回退到 5.0 的接口判据（dlss5_install.ps1 997-1007、1250-1319、1412-1461）。前一档有结果就不看后一档：
    # ① 主程序导入 ② 主程序里的模块名 ③ 同目录及下一层 DLL 的导入（<80 MB，天国拯救2 / 消光2 的渲染在引擎 DLL 里）
    # ④ 同目录及下一层其它 exe 的导入 ⑤ 这些 DLL 里的模块名。有 DX10/11/12 时不算 DX9/DX8/DirectDraw。
    # 只剩 OpenGL 的 Unity 游戏、以及一档都没找到的，按 D3D11（5.0 原话「按 D3D11 处理」）。
    $Exe=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $Exe
    $graphicsOf={param([string[]]$Modules) @(Get-033ApiNames @(Select-033ModernGraphicsModules @($Modules))|Where-Object {$_ -ne 'dxgi'})}
    $modulesOf={param([string[]]$Names) $map=@{'ddraw'='ddraw.dll';'glide'='glide2x.dll';'dx8'='d3d8.dll';'dx9'='d3d9.dll';'dx10'='d3d10.dll';'dx11'='d3d11.dll';'dx12'='d3d12.dll';'dxgi'='dxgi.dll';'opengl'='opengl32.dll';'vulkan'='vulkan-1.dll'};@($Names|ForEach-Object {if($map.ContainsKey($_)){$map[$_]}})}
    $pe=Get-033PeInfo $Exe
    $imports=@($pe.Imports)+@($pe.DelayImports)
    $apis=@(& $graphicsOf $imports);$tier='imports';$source=$Exe
    if(-not $apis.Count -and $pe.Status -in @('valid','unknown')){
        $dyn=@(Get-033DynamicApiNames $Exe);$apis=@(& $graphicsOf (& $modulesOf $dyn));$tier='exe-strings'
    }
    $skip=@($script:FOREIGN_TOOL_DIRS)
    $dlls=@()
    if(-not $apis.Count){
        $dlls=@(Get-ChildItem -LiteralPath $dir -Filter '*.dll' -File -Recurse -Depth 1 -Force -ErrorAction SilentlyContinue|Where-Object {$_.Length -lt 80MB -and ($_.Name -notin $K033ProxyNames) -and (($_.DirectoryName -ieq $dir) -or ((Split-Path -Leaf $_.DirectoryName) -notin $skip))}|Sort-Object Length -Descending|Select-Object -First 200)
        foreach($d in $dlls){
            $dpe=Get-033PeInfo $d.FullName;if($dpe.Status -ne 'valid'){continue}
            $found=@(& $graphicsOf (@($dpe.Imports)+@($dpe.DelayImports)))
            if($found.Count){$apis=@(@($apis)+@($found)|Sort-Object -Unique);if($tier -notlike 'engine-dll*'){$tier='engine-dll';$source=$d.FullName}}
        }
        if($apis.Count){$apis=@(& $graphicsOf (& $modulesOf $apis))}
    }
    if(-not $apis.Count){
        $junk='(?i)^(unins|setup|vcredist|dxsetup|dxwebsetup|crashreport|dgVoodooCpl|UnityCrashHandler)'
        foreach($x in @(Get-ChildItem -LiteralPath $dir -Filter '*.exe' -File -Recurse -Depth 1 -Force -ErrorAction SilentlyContinue|Where-Object {$_.FullName -ine $Exe -and $_.Length -lt 80MB -and $_.Name -notmatch $junk})){
            $xpe=Get-033PeInfo $x.FullName;if($xpe.Status -ne 'valid' -or $xpe.Architecture -ne $pe.Architecture){continue}
            $found=@(& $graphicsOf (@($xpe.Imports)+@($xpe.DelayImports)))
            if($found.Count){$apis=@(@($apis)+@($found)|Sort-Object -Unique);if($tier -ne 'sibling-exe'){$tier='sibling-exe';$source=$x.FullName}}
        }
        if($apis.Count){$apis=@(& $graphicsOf (& $modulesOf $apis))}
    }
    if(-not $apis.Count){
        foreach($d in @($dlls|Select-Object -First 24)){
            $found=@(& $graphicsOf (& $modulesOf @(Get-033DynamicApiNames $d.FullName 64)))
            if($found.Count){$apis=@(@($apis)+@($found)|Sort-Object -Unique);if($tier -ne 'dll-strings'){$tier='dll-strings';$source=$d.FullName}}
        }
        if($apis.Count){$apis=@(& $graphicsOf (& $modulesOf $apis))}
    }
    # 2026-09-12 逐游戏名单只补「一点证据都没有」的空（ReShade 官方兼容名单写明的接口）。DXGI 不分 11/12，不用；
    # DX8 / DX9 / DirectDraw 条目几乎都是 32 位老游戏，64 位主程序不套（2008 年的 Dead Space.exe 和 2023 重制版同名）。
    if(-not $apis.Count){
        $listRule=Get-033GameRule $Exe
        if($listRule -and $listRule.ContainsKey('Api')){
            $ra=[string]$listRule.Api
            if($ra -in @('dx10','dx11','dx12','opengl','vulkan') -or ($ra -in @('dx8','dx9','ddraw') -and $pe.Architecture -eq 'x86')){$apis=@($ra);$tier='game-list';$source='game_rules.json'}
        }
    }
    $unity=Test-033UnityGame $dir
    $assumed=$false
    if($unity -and -not @($apis|Where-Object {$_ -in @('dx9','dx10','dx11','dx12','dx8','ddraw','glide')}).Count){$apis=@('dx11');$tier='unity';$source=$dir}
    elseif(-not $apis.Count){$apis=@('dx11');$tier='assumed-dx11';$source=$null;$assumed=$true}
    [pscustomobject]@{Apis=@($apis|Sort-Object -Unique);Tier=$tier;Source=$source;Unity=$unity;Assumed=$assumed}
}
function Get-033GameRoot([string]$Dir){
    # 5.0 的 Get-GameRoot（dlss5_install.ps1 v5.0 1027-1046）：只在确实站在二进制子目录里时往上走，最多三层。
    $binNames=@('bin','bin64','bin32','binaries','win64','win32','x64','x86','retail','release','shipping','engine')
    $cur=[IO.Path]::GetFullPath($Dir).TrimEnd('\')
    for($i=0;$i -lt 3;$i++){
        $leaf=Split-Path -Leaf $cur
        if(-not $leaf -or ($binNames -notcontains $leaf.ToLowerInvariant())){break}
        $parent=Split-Path -Parent $cur
        if(-not $parent -or $parent -eq $cur -or $parent.Length -le 3){break}
        $cur=$parent
    }
    return $cur
}
function Get-033V5MountRule([string]$Exe,$Route){
    # 5.0 的挂载点规则（v5.0 1171-1205 逐游戏表、1481-1503 Unity 挂 d3d11、1541-1565 安全禁令优先）。
    # 只给通用 ReShade 路线：点名主程序的专用路线、RE 引擎路线都有验收过的挂载点，不动。
    if(-not $Route -or ($Route.ContainsKey('ExecutableNames') -and @($Route.ExecutableNames).Count)){return $null}
    $s=$(if($Route.ContainsKey('Suitability') -and $Route.Suitability){$Route.Suitability}else{@{}})
    if($s.ContainsKey('EngineMarkers') -and @($s.EngineMarkers).Count){return $null}
    $Exe=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $Exe;$n=[IO.Path]::GetFileName($Exe).ToLowerInvariant()
    $d3d12Games=@('rdr2.exe','f1_22.exe','forspoken.exe','zenlesszonezero.exe','doa6.exe','midnightsuns.exe','midnightsuns-win64-shipping.exe','aveum-win64-shipping.exe','immortalsofaveum-win64-shipping.exe','scorn.exe','scorn-win64-shipping.exe','thecallistoprotocol.exe','thecallistoprotocol-win64-shipping.exe','thymesia.exe','thymesia-win64-shipping.exe','libertycity.exe','vicecity.exe','sanandreas.exe')
    $rule=$null
    if($n -eq 'cyberpunk2077.exe'){
        $root=Get-033GameRoot $dir
        if(@(@((Join-Path $dir 'red4ext'),(Join-Path $dir 'redscript'),(Join-Path $root 'red4ext'),(Join-Path $root 'redscript'))|Where-Object {Test-Path -LiteralPath $_}).Count){$rule=@{Proxy='d3d12.dll';Why='逐游戏表（5.0）：赛博朋克 2077 装了 red4ext / redscript 时挂 d3d12.dll'}}
    }elseif($d3d12Games -contains $n){$rule=@{Proxy='d3d12.dll';Why='逐游戏兼容表（5.0）：这个游戏优先挂 d3d12.dll'}}elseif(($listRule=Get-033GameRule $Exe) -and $listRule.ContainsKey('Mount')){$rule=@{Proxy=[string]$listRule.Mount;Why=('逐游戏名单（'+(Format-033GameRuleSources $listRule)+'）：这个游戏要挂 '+[string]$listRule.Mount)}}
    if(-not $rule){
        $apis=@();if($s.ContainsKey('Apis')){$apis=@($s.Apis|ForEach-Object {[string]$_})}
        if((($apis -contains 'dx11') -or ($apis -contains 'dx10')) -and -not ($apis -contains 'dx12') -and (Test-033UnityGame $dir)){$rule=@{Proxy='d3d11.dll';Why='Unity 游戏（5.0）：Windows 上默认跑 D3D11，挂 d3d11.dll'}}
    }
    if($rule -and $rule.Proxy -eq 'd3d12.dll'){
        # 安全禁令优先：有 Agility SDK 或 Streamline 时绝不挂 d3d12.dll，和 5.0 一样静默用默认名。
        $rt=Get-033Dx12RuntimeEvidence $dir $Exe
        $sl=@(Get-ChildItem -LiteralPath $dir -File -Filter 'sl.interposer.dll' -Recurse -Depth 1 -ErrorAction SilentlyContinue)
        if($rt.Kind -eq 'agility-sdk' -or $sl.Count){return $null}
    }
    return $rule
}
function Get-033AutoMount([string]$Exe,$Route,[string]$Root,[string]$PackageRoot){
    # 计划和判断共用一个结论。先定想挂的名字（5.0 规则、逐游戏名单；规则给的名字这条路线换得了、安全禁令也放行才算数，否则用路线默认的），
    # 再照 5.0「避开已占用挂载名」：OptiScaler / Special K / DXVK / ENB 这类别人的模组占着的名字不覆盖（覆盖了它们就停了），换下一个空位。
    # 身份不明的旧文件、微软系统文件副本、别的 ReShade 仍按原来的办法备份后覆盖，卸载时原样放回。点名路线、RE 路线有验收过的挂载点，不动。
    if(-not $Route -or ($Route.ContainsKey('ExecutableNames') -and @($Route.ExecutableNames).Count)){return $null}
    if($Route.ContainsKey('Suitability') -and $Route.Suitability -and $Route.Suitability.ContainsKey('EngineMarkers') -and @($Route.Suitability.EngineMarkers).Count){return $null}
    # 2026-09-13：入口文件不一定是 ReShade —— Vulkan 路线挂的是核心本体。
    #   允许的挂载名跟着入口文件走（Get-033RouteEntry 给），别拿 ReShade 那张表去卡核心。
    $routeEntry=Get-033RouteEntry $Route
    if(-not $routeEntry){return $null}
    $entry=@($routeEntry.File)
    $entryNames=@($routeEntry.Names)
    $current=[string]$entry[0].Target
    $rule=Get-033V5MountRule $Exe $Route
    $want=$current;$why=$null
    if($rule){try{[void](Set-033ProxyMount $Route $rule.Proxy);Assert-033ProxyAllowedHere $Root $Exe $rule.Proxy;$want=[string]$rule.Proxy;$why=$rule.Why}catch{}}
    $foreign={param([string]$n) $p=Join-Path $Root $n;if(-not(Test-Path -LiteralPath $p -PathType Leaf)){return $null};$id=Get-033ProxyIdentity $p;if($id.Kind -in $script:K033ForeignModKinds){return [string]$id.Kind};return $null}
    $freeOrOurs={param([string]$n) $p=Join-Path $Root $n;if(-not(Test-Path -LiteralPath $p -PathType Leaf)){return $true};if((Get-033ProxyIdentity $p).Kind -eq '033-fork'){return $true};return ((Get-033FileHash $p) -ieq [string]$entry[0].Hash)}
    $avoided=$null;$occ=& $foreign $want
    if($occ){
        # 2026-09-17 Fable（找回 5.0 的 OptiScaler 共存协议）：dxgi 被 OptiScaler 占着时，5.0 不抢名字也不另挂一个名字，
        #   而是把 ReShade 装成 ReShade64.dll、在 OptiScaler.ini 里写 LoadReshade=true，由 OptiScaler 按它的官方协议加载
        #   （wiki「Compatibility with other mods」）。6.1.3 只是"避开"再挂一个名字，结果两套 NGX 钩子同进程各钩一份，
        #   违反「禁止两套 NR 消费者同时接管」。链式时顺手把 OptiScaler 自己的 DlssNr 关掉（卸载原样还原），一个游戏只留一套 NR。
        if($occ -eq 'optiscaler' -and $routeEntry.Kind -eq 'reshade'){
            $chainName=$(if([string]$Route.Architecture -eq 'x86'){'ReShade32.dll'}else{'ReShade64.dll'})
            $chainPath=Join-Path $Root $chainName
            $chainFree=(-not (Test-Path -LiteralPath $chainPath -PathType Leaf)) -or ((Get-033ProxyIdentity $chainPath).Kind -eq '033-fork') -or ((Get-033FileHash $chainPath) -ieq [string]$entry[0].Hash)
            if($chainFree){
                return @{Proxy=$chainName;Chain='optiscaler';Why=('已有 OptiScaler 占着 '+$want+'：按它的官方共存协议由它加载 '+$chainName);
                    Warning=('挂载点用 '+$chainName+'：'+$want+' 现在是 OptiScaler，不抢它的名字，改由 OptiScaler 按官方协议加载 033（OptiScaler.ini 写入 [Plugins] LoadReshade=true，并把它自己的 [DlssNr] 关掉，一个游戏只留一套神经渲染；卸载时原样还原）。')}
            }
        }
        $apis=@();if($Route.ContainsKey('Suitability') -and $Route.Suitability -and $Route.Suitability.ContainsKey('Apis')){$apis=@($Route.Suitability.Apis|ForEach-Object {[string]$_})}
        # Vulkan 游戏不会加载 dxgi/d3d11 那一套，只能挂它真的会加载的那几个系统 DLL；
        # 顺序按「游戏最常导入」排：winmm(高精度计时) > version > dbghelp > winhttp/wininet。
        $order=$(if($apis -contains 'vulkan'){@('winmm.dll','version.dll','dbghelp.dll','winhttp.dll','wininet.dll','dxgi.dll')}elseif($apis -contains 'opengl'){@('opengl32.dll','dxgi.dll','dinput8.dll')}elseif($apis -contains 'dx12'){@('dxgi.dll','d3d12.dll','d3d11.dll','dinput8.dll')}else{@('dxgi.dll','d3d11.dll','d3d10.dll','dinput8.dll')})
        $order=@($order|Where-Object {$entryNames -icontains $_})   # 入口文件顶不住的名字直接不考虑
        $taken=@($Route.Files|Where-Object {[string]$_.Target -ine $current}|ForEach-Object {[string]$_.Target})
        $pick=$null
        foreach($n in $order){
            if($n -ieq $want -or $taken -icontains $n){continue}
            if(-not (& $freeOrOurs $n)){continue}
            try{[void](Set-033ProxyMount $Route $n);Assert-033ProxyAllowedHere $Root $Exe $n}catch{continue}
            $pick=$n;break
        }
        if(-not $pick){throw ('这个游戏目录里，本包能用的挂载名被别的模组占满了（'+$want+' 是 '+$occ+'）：硬装会把它们冲掉，所以什么都没写。先用那个模组自己的卸载方式腾出一个名字，再回来装。')}
        $avoided=@{Name=$want;Kind=$occ};$want=$pick
    }
    # 2026-09-12 业主：「自动检测太蠢了，很多都检测错误，注入错误」。
    #   到这里为止，$want 是【规则表 / 路线默认】给的名字 —— 全程没问过最要紧的那个问题：
    #   这个游戏到底会不会加载这个名字？答案在二进制里查得到（导入表 / LoadLibrary 用的字符串）。
    #   所以：选中的名字要是一点证据都没有，而另一个允许用的名字在【导入表】里实打实存在，
    #   就换过去。只推翻「路线默认」，不推翻业主验过的逐游戏规则（$rule），也不碰
    #   固定挂载点的路线（上面已经 return $null 了）。
    $mountProof=$null
    try{$mountProof=Get-033MountEvidence $Exe}catch{$mountProof=$null}
    # ★还得问一句：这个名字我们顶得住吗★ 2026-09-12 实测：把发布包的入口文件（ReShade 分支，导出 494 个）
    #   跟各个系统 DLL 的常用入口对一遍 —— dxgi / d3d9~12 / ddraw / d2d1 / opengl32 / dinput 都齐，
    #   但 winmm 一个函数都没有、version 也一个都没有。而巫师3 这类游戏的主程序【确实导入 winmm】。
    #   顶不住却挂上去 = 游戏启动直接失败（0xc000007b / 找不到程序入口点），正是评论区
    #   「装完游戏就打不开了」那一整类。所以按【真实导出表】比对，不写死名单：换了入口文件结论自动跟着变。
    #   注：这里只比对主程序的导入。目录里其它 DLL 也可能从同名 DLL 导入，但拦住启动的主要是主程序这一份。
    $proxyExports=$null;$gameImports=$null
    if($PackageRoot){try{$proxyExports=Get-033PeExportNames (Get-033Path $PackageRoot ([string]$entry[0].Source))}catch{$proxyExports=$null}}
    try{$gameImports=Get-033PeImportedFunctions $Exe}catch{$gameImports=$null}
    if($mountProof -and -not $rule){
        $have=[string]$mountProof.Evidence[$want.ToLowerInvariant()]
        if((Get-033MountEvidenceRank $have) -eq 0){
            $taken2=@($Route.Files|Where-Object {[string]$_.Target -ine $current}|ForEach-Object {[string]$_.Target})
            # ★自动翻案不许选这两个名字★（手工 -Proxy 仍然可以，那是人自己定的）：
            #   · d3d12.dll —— 本文件上面那条 v5.0 安全禁令说得很清楚：挂它会截断游戏自己的
            #     D3D12Core / sl.interposer 加载链，而 dxgi.dll 在这类游戏上是跑通过的
            #     （巫师3 DX12 = Agility 1.600 + Streamline 1.5.6）。而「静态导入 d3d12、
            #     没静态导入 dxgi」恰恰是【绝大多数 DX12 游戏的常态】—— 导入表里看不见 dxgi
            #     不代表游戏不加载它（交换链一定要 dxgi，只是走的动态加载）。不挡住这一条，
            #     这条规则就会把一大批本来装得好好的 DX12 游戏从验证过的挂载点上挪走。
            #   · dinput8.dll —— RE 引擎那套适配器占着它，自动挪过去会互相顶掉。
            $autoAvoid=@('d3d12.dll','dinput8.dll','dinput.dll')
            foreach($n in @($script:K033MountCandidates)){
                if($n -ieq $want -or $taken2 -icontains $n -or $autoAvoid -icontains $n){continue}
                if((Get-033MountEvidenceRank ([string]$mountProof.Evidence[$n])) -lt 2){continue}
                $stand=Test-033ProxyCanStandIn $n $gameImports $proxyExports
                if(-not $stand.Ok){continue}   # 我们导不出游戏要的函数，挂上去游戏就起不来
                if(& $foreign $n){continue}
                if(-not (& $freeOrOurs $n)){continue}
                try{[void](Set-033ProxyMount $Route $n);Assert-033ProxyAllowedHere $Root $Exe $n}catch{continue}
                $why=('主程序或同目录的引擎 DLL 在导入表里真的加载 '+$n+'，而 '+$want+' 在这个游戏里一点痕迹都没有')
                $want=$n;break
            }
        }
    }
    # ★最后一道闸：不管上面是怎么选出来的，写进去之前再问一句「这个名字我们顶得住吗」★
    #   以前这句只在"证据翻案"那一支问过，【路线默认】的那个名字从来没问过。ReShade 那几条路线
    #   默认是 dxgi.dll，ReShade 全导得出，所以一直没出事；Vulkan 路线默认 winmm.dll 就不一样了 ——
    #   核心只导出 winmm 里的一小部分，游戏要是从 winmm 导入别的函数，挂上去当场起不来。
    #   ★只在两张表都读得到时才管★：加壳主程序读不出导入表（返回 $null），那种情况按老样子照装，
    #   不能因为"读不出来"就把今天装得上的游戏全挡了。
    if($null -ne $gameImports -and $null -ne $proxyExports){
        $standWant=Test-033ProxyCanStandIn $want $gameImports $proxyExports
        if(-not $standWant.Ok){
            $missing=@($standWant.Missing)
            $taken3=@($Route.Files|Where-Object {[string]$_.Target -ine $current}|ForEach-Object {[string]$_.Target})
            # 先挑有加载证据、又顶得住的；都没有就挑顶得住、且游戏根本不从它导入东西的（挂上去不会拦启动）。
            $pick2=$null
            foreach($pass in @('evidence','any')){
                foreach($n in @($entryNames)){
                    if($n -ieq $want -or $taken3 -icontains $n){continue}
                    if($pass -eq 'evidence' -and (Get-033MountEvidenceRank $(if($mountProof){[string]$mountProof.Evidence[$n.ToLowerInvariant()]}else{'none'})) -lt 2){continue}
                    if(-not (Test-033ProxyCanStandIn $n $gameImports $proxyExports).Ok){continue}
                    if(& $foreign $n){continue}
                    if(-not (& $freeOrOurs $n)){continue}
                    try{[void](Set-033ProxyMount $Route $n);Assert-033ProxyAllowedHere $Root $Exe $n}catch{continue}
                    $pick2=$n;break
                }
                if($pick2){break}
            }
            if(-not $pick2){
                throw ('这个游戏从 '+$want+' 导入了 '+$missing.Count+' 个函数（'+((@($missing)|Select-Object -First 4) -join '、')+$(if($missing.Count -gt 4){' 等'}else{''})+'），033 的入口文件导不出这些名字；挂上去游戏会直接起不来（0xc000007b / 找不到程序入口点）。本包能用的其它挂载名在这个游戏上也都不合适，所以什么都没写，游戏保持原样。')
            }
            $why=('原来要挂的 '+$want+' 顶不住（这个游戏从它导入 '+$missing.Count+' 个函数，我们导不出），换成 '+$pick2)
            $want=$pick2
        }
    }
    if($want -ieq $current -and -not $avoided -and -not $why){return $null}
    if($avoided){
        $text='挂载点用 '+$want+'：'+$avoided.Name+' 现在是别的模组（'+$avoided.Kind+'），不覆盖它（5.0 的做法）。'+$(if($avoided.Kind -eq 'optiscaler'){'OptiScaler 和 033 都会接管 DLSS，同时用可能冲突；进游戏不对就先卸掉 OptiScaler。'}else{''})+'按 Home 没反应就用「换挂载点.cmd」换。'
        return @{Proxy=$want;Why=('避开 '+$avoided.Name+'（'+$avoided.Kind+'）');Warning=$text}
    }
    return @{Proxy=$want;Why=$why;Warning=('挂载点按 5.0 的规则改成 '+$want+'：'+$why+'。按 Home 没反应就用「换挂载点.cmd」换回 dxgi.dll。')}
}
function Get-033GameSurveyUncached([string]$Exe,[string]$Vault){
    $Exe=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $Exe
    $pe=Get-033PeInfo $Exe
    $imports=@($pe.Imports)+@($pe.DelayImports)
    $apis=@();if($pe.Status -eq 'valid'){$apis=@(Get-033ApiNames $imports)}
    $graphics=@($apis|Where-Object {$_ -ne 'dxgi'})
    # 2026-09-17 Fable（找回 5.0 的天命奇御教训）：Unity 主程序静态导入 opengl32 只是可选后端，Windows 上实际跑 D3D11。
    #   6.1.3 只在「静态导入为空」时才套 Unity 规则，于是 Ori / Kingdom New Lands 被判成 opengl、挂 opengl32.dll，ReShade 进去了却一个 D3D 钩子都没有。
    $unityGlDemoted=$false
    if($graphics.Count -and -not @($graphics|Where-Object {$_ -ne 'opengl'}).Count -and (Test-033UnityGame $dir)){$apis=@($apis|Where-Object {$_ -ne 'opengl'});$graphics=@();$unityGlDemoted=$true}
    # Engines that LoadLibrary their graphics DLL never show it in the import
    # table. Route matching on imports alone therefore refuses them outright
    # (2026-09-11: 古剑奇谭三 loads d3d11 that way, and The Witcher 3's DX12
    # renderer reaches d3d12 through Streamline). Only scan when the import
    # table yielded no graphics API at all -- an import is stronger evidence and
    # scanning a large executable is slow (Judgment: 394 MB, ~21 s).
    $dynamicApis=@();$apiEvidence='imports';$graphicsEvidence=$null
    if($graphics.Count -eq 0){
        # 同一个判据给判断和计划用（Select-033ManagedProfile 不带侦察结果时也调它），两边永远一致。
        $graphicsEvidence=Get-033GraphicsEvidence $Exe
        $dynamicApis=@($graphicsEvidence.Apis)
        $apiEvidence=$(switch($graphicsEvidence.Tier){'exe-strings'{'module names in the executable'} 'engine-dll'{'engine DLL imports'} 'sibling-exe'{'sibling executable imports'} 'dll-strings'{'module names in DLLs'} 'unity'{'Unity engine'} 'game-list'{'the per-game list'} 'assumed-dx11'{'assumed D3D11'} default{'imports'}})
    }
    $effectiveApis=@(@($apis)+@($dynamicApis)|Sort-Object -Unique)
    $effectiveGraphics=@($effectiveApis|Where-Object {$_ -ne 'dxgi'})
    $exeItem=Get-Item -LiteralPath $Exe
    $existing=Get-033ExistingInstall $dir $Exe $Vault
    $upscalers=Get-033UpscalerStack $dir $exeItem.LastWriteTimeUtc $existing.ManagedPaths
    $nativeDlss=Get-033NativeDlssEvidence $dir $Exe $existing.ManagedPaths
    $dx12rt=Get-033Dx12RuntimeEvidence $dir $Exe
    $engine=[pscustomobject]@{ReEngine=(Test-Path -LiteralPath (Join-Path $dir 're_chunk_000.pak') -PathType Leaf);Markers=@(@('re_chunk_000.pak')|Where-Object {Test-Path -LiteralPath (Join-Path $dir $_) -PathType Leaf})}
    $proxies=@(Get-033ProxyOccupancy $dir)
    $dx12Signals=@();foreach($n in @('dxil.dll','dxcompiler.dll','D3D12Core.dll','d3d12core.dll','D3D12SDKLayers.dll')){if(Test-Path -LiteralPath (Join-Path $dir $n)){$dx12Signals+=@($n)}}
    $siblings=@(Get-033SiblingExecutables $dir)
    $candidates=@($siblings|Where-Object {$_.Path -ine $Exe -and $_.Status -eq 'valid' -and $_.Graphics.Count -gt 0})
    # A launcher has no graphics API by either kind of evidence.
    $launcherSuspect=($pe.Status -eq 'valid' -and $effectiveGraphics.Count -eq 0 -and $candidates.Count -gt 0)
    $ngx=Get-033NgxRuntime
    $gpu=Get-033GpuFacts;$frameGen=Get-033GameFrameGenEvidence $dir
    # 2026-09-19 read-only: what the game bundles and what NVIDIA's own component cache holds for THIS card (report only).
    $frameGenBundled=$null;$ngxCache=$null
    if($frameGen.Present -and @($gpu.Nvidia).Count){$frameGenBundled=Get-033BundledFrameGenVersion $dir $frameGen;$ngxCache=Get-033NgxCacheEvidence}
    $foreignDirs=@();foreach($n in @('OptiScaler','_测试备份2','_DLSS5_备份','enbseries','SpecialK')){if(Test-Path -LiteralPath (Join-Path $dir $n) -PathType Container){$foreignDirs+=@($n)}}
    $antiCheat=@(Get-033AntiCheatEvidence $dir)
    [pscustomobject]@{
        Exe=[pscustomobject]@{Path=$Exe;Name=$exeItem.Name;Size=$exeItem.Length;WrittenUtc=$exeItem.LastWriteTimeUtc.ToString('o');Status=$pe.Status;Architecture=$pe.Architecture;IsDll=$pe.IsDll;Imports=$imports;Error=$pe.Error}
        Directory=$dir;Apis=$apis;GraphicsApis=$graphics;Dx12Static=($apis -contains 'dx12');Dx12Signals=$dx12Signals
        DynamicApis=$dynamicApis;EffectiveApis=$effectiveApis;EffectiveGraphicsApis=$effectiveGraphics;ApiEvidence=$apiEvidence;UnityOpenGlDemoted=$unityGlDemoted
        LauncherSuspect=$launcherSuspect;Candidates=@($candidates|ForEach-Object {$_.Path})
        Upscalers=$upscalers;NativeDlss=$nativeDlss;Dx12Runtime=$dx12rt;Engine=$engine;Proxies=$proxies;Existing=$existing;Ngx=$ngx;Gpu=$gpu;FrameGen=$frameGen;FrameGenBundled=$frameGenBundled;NgxCache=$ngxCache;ForeignDirectories=$foreignDirs;AntiCheat=$antiCheat;GameRule=(Get-033GameRule $Exe)
        SurveyedAt=(Get-Date).ToUniversalTime().ToString('o')
    }
}
function Get-033InstallVerdict($Survey,$Package,[switch]$ForceFeeder,[string]$Profile,[switch]$AllowNativeDlss,[switch]$HandoverOld,[switch]$Manual,[string]$Proxy){
    $reasons=@();$warnings=@();$needs=@();$decision='install';$route=$null;$selected=$null;$components=@();$apiCompanions=@()
    # V3.4 manual route: an update of a game whose route was picked by hand keeps that route, and says so.
    if(-not $Manual -and -not $Profile -and $Survey.Existing -and $Survey.Existing.Kind -eq 'managed' -and $Survey.Existing.Managed -and $Survey.Existing.Managed.State){
        $manualTarget=@($Survey.Existing.Managed.State.Targets|Where-Object {$_.Exe -ieq $Survey.Exe.Path -and $_.ContainsKey('Manual') -and $_.Manual})|Select-Object -First 1
        # 2026-09-12 dd2-x64 并进 native-re-x64：上次手动选的路线新包里没有了，就退回自动选路并说一声，不卡住更新。
        if($manualTarget -and @($Package.Data.Profiles|Where-Object {$_.Id -ceq [string]$manualTarget.Profile}).Count){$Manual=$true;$Profile=[string]$manualTarget.Profile}
        elseif($manualTarget){$warnings+=@('上次手动选的路线 '+[string]$manualTarget.Profile+' 新包里已经没有了（并进了别的路线），这次按自动选路装；卸载照样原样还原。')}
    }
    if($Manual){$warnings+=@('手动模式：路线由你指定（'+$Profile+'），没有经过自动匹配；装上没效果或进不了游戏，就用安装器卸载，原件都在。')}
    # 2026-09-11 回退到 5.0：读不全的文件头、像启动器、找不到 NGX 运行库都只提醒，照样装（5.0 没有这几道门）。
    if($Survey.Exe.Status -eq 'unknown' -and -not $Survey.Exe.IsDll -and $Survey.Exe.Architecture -in @('x86','x64')){
        $warnings+=@('这个程序的文件头读不全（'+[string]$Survey.Exe.Error+'；加壳或加密的主程序常见），只按它的位数 '+$Survey.Exe.Architecture+' 安装。')
    }elseif($Survey.Exe.Status -ne 'valid' -or $Survey.Exe.IsDll){$reasons+=@('所选文件不是有效的游戏主程序：'+$Survey.Exe.Path);$decision='refuse'}
    if($decision -ne 'refuse' -and $Survey.LauncherSuspect){
        $warnings+=@('所选程序本身没有图形接口导入，可能是启动器；同目录下有图形接口的程序：'+(($Survey.Candidates|ForEach-Object {Split-Path -Leaf $_}) -join '、')+'。装上没反应就改选那个程序再装。')
    }
    if($decision -ne 'refuse' -and $Survey.PSObject.Properties['ApiEvidence'] -and $Survey.ApiEvidence -notin @('imports','module names in the executable')){
        $warnings+=@('主程序里看不出图形接口，按 5.0 的办法判断：'+$(switch($Survey.ApiEvidence){'engine DLL imports'{'取自同目录引擎 DLL 的导入'} 'sibling executable imports'{'取自同目录其它程序的导入'} 'module names in DLLs'{'取自 DLL 里的模块名'} 'Unity engine'{'Unity 游戏按 D3D11'} 'the per-game list'{'取自逐游戏名单（ReShade 官方兼容名单）'} default{'什么都没找到，按 D3D11 处理'}})+'。')
    }
    if($decision -ne 'refuse' -and -not $Survey.Ngx.ShellPresent){$warnings+=@('本次静态检查未定位到 NVIDIA NGX 运行库文件（'+$Survey.Ngx.ShellPath+'），不能据此判断驱动损坏或 NR 不可用。安装继续；实际加载结果和失败原因以游戏本局的 033 日志为准。')}
    # A store manifest is packaging metadata, not proof of an AppContainer or
    # an unwritable game. Win32/GDK flat-file installations can carry it too.
    # Actual writes retain the same exact transaction/rollback and permissions.
    # 2026-09-17 Fable：回到 5.0 —— WindowsApps 下的 UWP 目录既进不去（挂载名不被加载）也不能安全改权限，5.0 直接拒装并说清楚；
    #   商店清单出现在别处（XboxGames 的 GDK 平铺安装）仍只提醒，那类目录可写、ReShade 也能进。
    if($decision -ne 'refuse' -and ([string]$Survey.Exe.Path -match '(?i)\\WindowsApps\\')){
        $reasons+=@('这是受限的 GamePass/UWP 安装目录（WindowsApps）：本包的挂载名进不去，也不能安全改 WindowsApps 权限，什么都没写进游戏目录。这游戏在 Steam / Epic 上有的话，换那个版本装就能用。');$decision='refuse'
    }elseif($decision -ne 'refuse' -and (Test-Path -LiteralPath (Join-Path $Survey.Directory 'AppxManifest.xml') -PathType Leaf)){
        $warnings+=@('检测到商店打包清单（AppxManifest.xml）。名称本身不能说明游戏是 UWP 或禁止安装；继续按这个主程序的位数、图形接口和组件要求安装，实际目录权限由安装事务检查。')
    }
    # 5.0 的拒装与风险表（dlss5_install.ps1 v5.0 643-728），原样搬回来：
    # 真拒装的只有装了必然白装或打不开、而且没法安全写入的几类；米哈游客户端、霍格沃茨之遗只提醒，照常装。
    $exeLeaf=[string]$Survey.Exe.Name;$exeLower=$exeLeaf.ToLowerInvariant()
    if($decision -ne 'refuse'){
        if($exeLower -eq 'ffxiv_dx11.exe'){
            $reasons+=@('最终幻想14 的加载链本包顶不了：它要的是 winmm / winhttp 那一类挂载名，本包只带 ReShade 支持的代理。');$decision='refuse'
        }else{
            $dllscan=@{'fateseekerii.exe'='天命奇御二'}
            # 2026-09-12 赛博朋克2077 被拒装的那份反馈：命中的不是这张内置表，是老版 V5.0 一键包留在
            # %LOCALAPPDATA%\DLSS5一键包\拒装名单.txt 里的一行（那句「（本机 … 实测）」只有下面这段拼得出来）。
            # 那个文件本包只读、从不写，玩家也不知道它存在 —— 于是一条旧记录能把一个现在明明装得上的
            # 游戏永久挡在门外，提示里还不说这条是哪来的。那是阻拦，不是解决。
            # 改成：内置表照旧硬拒；本机残留名单只提醒、照常装（真打不开有一键卸载兜底），
            # 并且拿现场事实校验它 —— 目录里此刻就摆着别人的模组 DLL 而游戏照样能玩，
            # 「多一个 dll 就打不开」这句话对这个游戏就不成立，把证据一起说给玩家听。
            $staleScan=$null;$staleScanFile=Join-Path $env:LOCALAPPDATA 'DLSS5一键包\拒装名单.txt'
            try{
                if(Test-Path -LiteralPath $staleScanFile){foreach($l in @(Get-Content -LiteralPath $staleScanFile -Encoding UTF8)){$p=$l -split '\|';if($p.Count -ge 2 -and $p[0] -eq $exeLower -and $p[1] -eq 'dllscan' -and -not $dllscan.ContainsKey($exeLower)){$staleScan=$exeLeaf+'（老版包在本机记的'+$(if($p.Count -ge 3){' '+$p[2]}else{''})+'）'}}}
            }catch{}
            if($staleScan){
                $modKinds=@($script:K033ForeignModKinds)+@('reshade')
                $livingMods=@(@($Survey.Proxies)|Where-Object {$_ -and -not $_.Disabled -and ([string]$_.Kind) -in $modKinds})
                $proof=$(if($livingMods.Count){'这个目录里现在有 '+((@($livingMods|ForEach-Object {[string]$_.Alias+'＝'+[string]$_.Kind})|Select-Object -Unique) -join '、')+'；文件存在不能证明游戏已经正常启动，那条旧记录也不能证明当前版本一定不兼容。'}else{'本包没法验证这条记录现在还算不算数。'})
                $warnings+=@($staleScan+'：老版一键包在你机器上留了一条「这游戏多一个 DLL 就打不开」的记录（'+$staleScanFile+'，这个文件不是本包写的，本包只读它）。'+$proof+'所以这次照常装。万一真打不开，用安装器卸载就恢复原样；想彻底清掉这条记录，删掉上面那个文件即可。')
            }
            if($dllscan.ContainsKey($exeLower)){
                $reasons+=@($dllscan[$exeLower]+' 装不了：这游戏会扫自己目录里的 .dll，多一个就拒绝启动（不看内容不看名字），ReShade 必须以 DLL 的形式待在游戏旁边。');$decision='refuse'
            }
            $hard=@{'zenlesszonezero.exe'='绝区零';'genshinimpact.exe'='原神';'yuanshen.exe'='原神';'starrail.exe'='崩坏：星穹铁道';'bh3.exe'='崩坏3';'nap.exe'='绝区零'}
            if($hard.ContainsKey($exeLower)){$warnings+=@('先说清楚：'+$hard[$exeLower]+' 的客户端会校验自己的文件，目录里多一个 DLL 就拒绝启动（弹「The client is damaged, please reinstall the client」）。这不是客户端真坏了，用安装器卸载就能立刻打开。')}
            if($exeLower -in @('hogwartslegacy.exe','hogwartslegacy-win64-shipping.exe')){$warnings+=@('先说清楚：霍格沃茨之遗评论区有人装完闪退（不是所有机器都会）；闪退就用安装器卸载。')}
        }
    }
    if($decision -ne 'refuse'){
        # 2026-09-17 Fable（5.0 路线 N）：游戏自己带神经渲染运行库（2K27 一类，只是原厂库挑显卡）。5.0 只换通用运行库、不装任何插件，
        #   因为硬叠插件会跟游戏自己的 NR 管线抢同一个特征（实测 0xBAD00007 全帧失败）。本包没有"只换库"的路线，所以自动模式下拒装并说清楚；
        #   本包/5.0 自己放的那份（同哈希或 5.0 的两个已知大小）不算，旧包交接后会先清掉。
        $foreignNr=$null
        try{
            $nrp=Join-Path $Survey.Directory 'nvngx_dlssnr.dll'
            if((Test-Path -LiteralPath $nrp -PathType Leaf) -and ($Survey.Existing.Kind -ne 'old-package') -and (@($Survey.Existing.ManagedPaths) -inotcontains 'nvngx_dlssnr.dll')){
                $nrHash=Get-033FileHash $nrp;$nrLen=(Get-Item -LiteralPath $nrp).Length
                $ourNr=@($Package.Data.Profiles|ForEach-Object {$_.Files}|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'model' -and ([string]$_.Target) -match '(?i)nvngx_dlssnr\.dll$'}|ForEach-Object {[string]$_.Hash})
                if(($ourNr -inotcontains $nrHash) -and ($nrLen -notin @(165830144,165840496))){$foreignNr=$nrHash}
            }
        }catch{$foreignNr=$null}
        if($foreignNr){
            $nrText='游戏目录里有它自己的神经渲染运行库 nvngx_dlssnr.dll（SHA256 '+$foreignNr.Substring(0,12)+'…，不是本包放的）。5.0 对这类游戏（2K27 一类）只换通用运行库、不装任何插件；033 的核心再叠上去会跟游戏自己的 NR 管线抢同一个特征（5.0 实测 0xBAD00007 全帧失败）。'
            if($Manual){$warnings+=@($nrText+'【手动模式，按你选的路线装，后果自负】')}
            else{$reasons+=@($nrText+'本包这一版没有"只换运行库"的路线，所以什么都没写；确认要硬试插件路线，用手动模式选路线。');$decision='refuse'}
        }
    }
    if($decision -ne 'refuse' -and $Survey.PSObject.Properties['AntiCheat'] -and @($Survey.AntiCheat).Count){
        $warnings+=@('这个游戏带反作弊（发现：'+(@($Survey.AntiCheat) -join '、')+'）。风险自己掂量：轻则游戏打不开，重则账号被封，要上线的号想清楚；后悔了用安装器卸载就能原样还原。')
    }
    # 2026-09-12 逐游戏名单：会拦 ReShade 的只提醒（业主：反作弊只提醒、照样装），带上游戏名；同名主程序不是这个游戏就忽略。
    $gameRule=$(if($Survey.PSObject.Properties['GameRule']){$Survey.GameRule}else{$null})
    if($decision -ne 'refuse' -and $gameRule){
        if($gameRule.ContainsKey('Banned') -and $gameRule.Banned){$warnings+=@('ReShade 官方兼容名单把 '+$exeLeaf+'（'+[string]$gameRule.Title+'）标成「会拦截或封禁 ReShade」，ReShade 自己的安装程序遇到它直接拒装：装了轻则进不去游戏，重则封号。你装的不是这个游戏（只是主程序同名）就忽略；是的话强烈建议别装，装了用安装器卸载就能还原。')}
        if($gameRule.ContainsKey('Warn')){$warnings+=@([string]$gameRule.Warn)}
        if($gameRule.ContainsKey('Note')){$warnings+=@('逐游戏名单：'+[string]$gameRule.Note)}
    }
    if($decision -ne 'refuse'){
        $selNotes=@()
        try{$selected=Select-033ManagedProfile $Package $Survey.Exe.Path -ForceFeeder:$ForceFeeder -Profile $Profile -Manual:$Manual -NativeDlss $(if($Survey.PSObject.Properties['NativeDlss']){$Survey.NativeDlss}else{$null}) -Dx12Runtime $(if($Survey.PSObject.Properties['Dx12Runtime']){$Survey.Dx12Runtime}else{$null}) -EffectiveApis $(if($Survey.PSObject.Properties['EffectiveApis']){$Survey.EffectiveApis}else{$null}) -Notes ([ref]$selNotes);$route=$selected.Id}
        catch{$reasons+=@($_.Exception.Message);$decision='refuse'}
        foreach($n in @($selNotes)){if($n){$warnings+=@([string]$n)}}
        # 挂载点装之前就说：和计划同一个结论（Get-033AutoMount：5.0 规则、逐游戏名单、避开别人的模组）；手工指定的、账本沿用的挂载点不动。
        # 后面「现有代理会被覆盖」那几句按真正要写的名字说。
        if($selected){
            $installRoot=Get-033InstallRoot $Survey.Exe.Path
            $prevProxy=$null
            if(-not $Proxy -and $Survey.Existing -and $Survey.Existing.Kind -eq 'managed' -and $Survey.Existing.Managed -and $Survey.Existing.Managed.State){$prevProxy=@($Survey.Existing.Managed.State.Targets|Where-Object {$_.Exe -ieq $Survey.Exe.Path -and $_.ContainsKey('Proxy') -and $_.Proxy})|Select-Object -First 1}
            $useProxy=$(if($Proxy){$Proxy}elseif($prevProxy){[string]$prevProxy.Proxy}else{$null})
            if($useProxy){try{$selected=Set-033ProxyMount $selected $useProxy}catch{}}
            else{
                $am=Get-033RetainedAutoMount $Survey.Exe.Path $selected $installRoot.Root ([string]$Package.Root) $Survey
                if(-not $am){try{$am=Get-033AutoMount $Survey.Exe.Path $selected $installRoot.Root $(if($Package){[string]$Package.Root}else{$null})}catch{$reasons+=@($_.Exception.Message);$decision='refuse'}}
                if($am){$warnings+=@($am.Warning);try{$selected=Set-033ProxyMount $selected $am.Proxy;if(($am -is [Collections.IDictionary] -and $am.ContainsKey('Chain') -and $am.Chain -eq 'optiscaler') -or ([string]$am.Proxy -match '(?i)^ReShade(32|64)\.dll$')){$selected=Add-033OptiChainFile $selected $Package}}catch{}}
            }
            if($installRoot.Target){$warnings+=@('逐游戏名单（ReShade 官方兼容名单）：这个游戏从子目录 '+$installRoot.Target+' 加载 DLL，033 装到那里。')}
        }
    }
    if($Manual -and $selected){
        # Show what automatic matching would have done, so a hand-picked route is never mistaken for a detected one.
        $autoText=''
        try{$autoText='自动选路会选 '+(Select-033ManagedProfile $Package $Survey.Exe.Path -NativeDlss $(if($Survey.PSObject.Properties['NativeDlss']){$Survey.NativeDlss}else{$null}) -Dx12Runtime $(if($Survey.PSObject.Properties['Dx12Runtime']){$Survey.Dx12Runtime}else{$null}) -EffectiveApis $(if($Survey.PSObject.Properties['EffectiveApis']){$Survey.EffectiveApis}else{$null})).Id}
        catch{$autoText='自动选路不装：'+(($_.Exception.Message -split "`n")[0])}
        $warnings+=@('手动模式对照：'+$autoText+'。')
    }
    if($selected){
        # 2026-09-13 路线级「必需运行库」（Vulkan 路线用）：有的路线的入口文件是动态链接 CRT 的，
        #   它由【游戏的加载器】直接加载，解析不到 CRT 就是游戏起不来。这几个 DLL 属于微软
        #   Visual C++ 运行库，装机量极大但不是 100%，而且我们【不往游戏目录里塞】—— 游戏自己
        #   往往带着一份，覆盖它才是真的会出事。所以：装之前查，缺了就说清楚让人补，不硬装。
        #   判据和查法跟转接件那套完全一样（游戏目录 或 对应位数的系统目录）。
        # ★外面这层 @() 不能省★ 子表达式里返回空数组会被 PowerShell 展开成 $null，后面 .Count 直接抛。
        $reqSuit=@($(if($selected.ContainsKey('Suitability') -and $selected.Suitability -and $selected.Suitability.ContainsKey('RequiredDlls')){@($selected.Suitability.RequiredDlls)}else{@()}))
        if($reqSuit.Count){
            $sysDir=Get-033SystemDllDirectory ([string]$selected.Architecture)
            $rootDir=(Get-033InstallRoot $Survey.Exe.Path).Root
            $lack=@($reqSuit|Where-Object {-not(Test-Path -LiteralPath (Join-Path $rootDir $_)) -and -not(Test-Path -LiteralPath (Join-Path $sysDir $_))})
            if($lack.Count){
                $reasons+=@('这条路线（'+$selected.Id+'）的 033 核心要用微软 Visual C++ 2015–2022 x64 运行库，可游戏目录和系统目录（'+$sysDir+'）里都没有 '+($lack -join '、')+'。缺了它挂上去游戏会直接起不来，所以什么都没写。到微软官网装一个「Visual C++ 可再发行程序包 x64」再回来装就行。')
                $decision='refuse'
            }
        }
        # Use the exact plan resolver after mount selection, before inspecting
        # components/proxy conflicts, so Survey describes the actual file plan.
        $companionOldState=$null
        if($Survey.Existing.Kind -eq 'managed' -and $Survey.Existing.Managed -and $Survey.Existing.Managed.State){$companionOldState=$Survey.Existing.Managed.State}
        $companionRoot=(Get-033InstallRoot $Survey.Exe.Path).Root
        $resolvedCompanions=Add-033ApiCompanions $selected $Package $Survey.Exe.Path $companionRoot $companionOldState
        $apiCompanions=@($resolvedCompanions.Decisions);$warnings+=@($resolvedCompanions.Warnings);$selected=$resolvedCompanions.Profile
        if($selected.ContainsKey('Components')){
            # Same per-machine decision the plan makes; later warnings see the files that will really be written.
            $oldTarget=$null
            if($Survey.Existing.Kind -eq 'managed' -and $Survey.Existing.Managed -and $Survey.Existing.Managed.State){$oldTarget=@($Survey.Existing.Managed.State.Targets|Where-Object {$_.Exe -ieq $Survey.Exe.Path})|Select-Object -First 1}
            $resolvedComponents=Resolve-033ManagedComponents $selected $Survey.Directory $Survey $oldTarget
            $components=@($resolvedComponents.Decisions);$selected=$resolvedComponents.Profile
            if(@($components|Where-Object {$_.Installed}).Count){
                # The bridge loader imports both Intel XeSS libraries by name: a game's own copies are backed up and replaced.
                $gameXess=@($Survey.Upscalers.Items|Where-Object {$_.Name -in @('libxess_fg.dll','libxell.dll') -and $_.Owner -ne '033-managed'})
                if($gameXess.Count){$warnings+=@('游戏目录里已有 '+(($gameXess|ForEach-Object {$_.Name+'（'+$(if($_.Version){$_.Version}else{'版本未知'})+'）'}) -join '、')+'：20/30 转接件的加载器离不开这两个 Intel 库，安装时会备份后换成转接件附带的版本，卸载时原样还原；装着转接件期间，游戏自己的 XeSS 帧生成选项可能不可用。')}
            }
        }
        $suit=$null;if($selected.ContainsKey('Suitability')){$suit=$selected.Suitability}
        $routeApis=@();if($suit -and $suit.ContainsKey('Apis')){$routeApis=@($suit.Apis)}
        $rtRoute=$(if($suit -and $suit.ContainsKey('Dx12Runtime')){[string]$suit.Dx12Runtime}else{'unknown'})
        if($Survey.Dx12Signals.Count -and -not $Survey.Dx12Static -and $routeApis.Count -and ($routeApis -notcontains 'dx12') -and ($rtRoute -notin @('present','any'))){
            $warnings+=@('目录里有 DX12 迹象（'+($Survey.Dx12Signals -join '、')+'）但主程序没有静态导入 d3d12.dll；如果游戏实际以 DX12 运行，本路线（'+($routeApis -join '/')+'）不适用。')
        }
        $native=@($Survey.Upscalers.Items|Where-Object {$_.Owner -ne '033-managed' -and $_.Kind -in @('dlss-sr','streamline','streamline-dlss','dlss-fg','streamline-fg')})
        $deep=$(if($Survey.PSObject.Properties['NativeDlss']){$Survey.NativeDlss}else{$null})
        $deepPresent=[bool]($deep -and $(if($deep.PSObject.Properties['EffectivePresent']){$deep.EffectivePresent}else{$deep.Present}))
        $filesUnused=[bool]($deep -and $deep.PSObject.Properties['FilesUnused'] -and $deep.FilesUnused)
        if($suit -and $suit.ContainsKey('NativeUpscaler') -and $suit.NativeUpscaler -eq 'present'){
            # V3.1 mainline route: it rides the game's own DLSS, so that DLSS must exist.
            $hint=if($suit.ContainsKey('AlternateProductHint')){[string]$suit.AlternateProductHint}else{''}
            if(-not $deepPresent -and -not $Survey.Upscalers.NativeDlss -and -not $Survey.Upscalers.Streamline){$noDlssText='本路线只挂游戏自带的 DLSS，但目录里没找到 DLSS/Streamline 文件，程序里也没有 NGX 字样。';if($Manual){$warnings+=@('手动模式：'+$noDlssText+'装上大概率没效果。')}else{$reasons+=@($noDlssText+$hint);$decision='refuse'}}
            elseif($deep -and $deep.Kind -eq 'needle'){$warnings+=@('只在程序里看到 NGX/DLSS 字样（'+($deep.Evidence -join '、')+'），目录里没有 DLSS 文件；如果游戏画面设置里没有 DLSS 选项，这条路线不会起作用。')}
        }
        if($suit -and $suit.ContainsKey('NativeUpscaler') -and $suit.NativeUpscaler -eq 'absent' -and -not $filesUnused -and ($Survey.Upscalers.NativeDlss -or $Survey.Upscalers.Streamline -or $deepPresent)){
            $list=($native|ForEach-Object {$_.Name+'('+$(if($_.Version){$_.Version}else{'?'})+'，'+$_.Owner+')'}) -join '、'
            if(-not $list -and $deep){$list=($deep.Evidence -join '、')}
            $hint=if($suit.ContainsKey('AlternateProductHint')){[string]$suit.AlternateProductHint}else{''}
            $text='游戏带有自己的 DLSS/Streamline：'+$list+'。本路线会在游戏进程里另建一套 DLSS，两者互相冲突（审判之眼 2026-09-10 实测启动崩溃）。'+$hint
            if($AllowNativeDlss -or $Manual){$warnings+=@($text+$(if($Manual){'【手动模式，按你选的路线装，后果自负；进不去游戏就用安装器卸载】'}else{'【已按 AllowNativeDlss 覆盖，后果自负】'}))}else{$reasons+=@($text);$needs+=@('AllowNativeDlss');$decision='refuse'}
        }
        if($filesUnused){$warnings+=@('目录里有 DLSS/Streamline 文件，但这个游戏的主程序和最大的几个二进制里都没有 NGX/Streamline 字样（查了 '+$deep.BinariesChecked+' 个），判为游戏不使用它们；按"没有自带 DLSS"选路。')}
        $rt=$(if($Survey.PSObject.Properties['Dx12Runtime']){$Survey.Dx12Runtime}else{$null})
        if($rtRoute -eq 'present' -and $rt -and -not $rt.Present){$warnings+=@('本路线按"游戏实际以 D3D12 运行"设计，但目录里没找到 D3D12 运行时证据；如果游戏确实只跑 DX11，神经渲染不会起作用（缺 DX11 桥），不会崩。')}
        $eng=$(if($Survey.PSObject.Properties['Engine']){$Survey.Engine}else{$null})
        $seedTargets=@($selected.Files|Where-Object {$_.Policy -eq 'seed'}|ForEach-Object {[string]$_.Target})
        if($eng -and $eng.ReEngine){
            # RE Engine: a REFramework-based dinput8.dll must load first (Capcom's
            # anti-modification check); ReShade enters through dxgi.dll. A route that seeds
            # praydog REFramework never overwrites an existing dinput8.dll; a route that ships
            # 033's RE adapter with Policy=replace (P1) backs the old one up and restores it on
            # uninstall, and the report has to say that instead.
            $d8=@($Survey.Proxies|Where-Object {$_.Alias -ieq 'dinput8.dll' -and -not $_.Disabled})
            $shipsRef=(@($selected.Files|Where-Object {[string]$_.Target -ieq 'dinput8.dll'}).Count -gt 0)
            $replacesD8=(@($selected.Files|Where-Object {[string]$_.Target -ieq 'dinput8.dll' -and [string]$_.Policy -eq 'replace'}).Count -gt 0)
            if($replacesD8){$warnings+=@('这是 RE Engine（re_chunk_000.pak）。本路线放入 033 的 RE 适配器 dinput8.dll（基于 REFramework，给核心提供场景输入）'+$(if($d8.Count){'；现有的 dinput8.dll（'+$(if($d8[0].Kind -eq 'reframework'){'REFramework'}else{[string]$d8[0].Kind})+'）先备份，卸载时原样放回'}else{''})+'；ReShade 从 dxgi.dll 进入。')}
            elseif($d8.Count -and $d8[0].Kind -ne 'reframework'){$warnings+=@('这是 RE Engine（re_chunk_000.pak）。它需要 praydog REFramework 的 dinput8.dll 先加载并处理 Capcom 的反修改检查；现有 dinput8.dll 身份不明（'+$d8[0].Kind+'），本安装器保持原样不覆盖，如果游戏起不来请自行换成 REFramework。')}
            elseif($d8.Count){$warnings+=@('这是 RE Engine（re_chunk_000.pak）。已有 REFramework（dinput8.dll，'+$(if($d8[0].FileVersion){$d8[0].FileVersion}else{'版本未知'})+'）保持原样；ReShade 从 dxgi.dll 进入。')}
            elseif($shipsRef){$warnings+=@('这是 RE Engine（re_chunk_000.pak）。目录里没有 REFramework，本路线会先放入随包的 REFramework dinput8.dll（praydog nightly 01414），再由 dxgi.dll 加载 ReShade；游戏更新后若闪退，先更新 REFramework nightly。')}
            else{$warnings+=@('这是 RE Engine（re_chunk_000.pak），需要 REFramework 的 dinput8.dll 先加载；本路线不带它，请先自行安装 REFramework。')}
        }
        $targets=@($selected.Files|ForEach-Object {[string]$_.Target})
        foreach($p in @($Survey.Proxies|Where-Object {-not $_.Disabled})){
            if($p.Kind -in @('033-fork') -or $Survey.Existing.Kind -eq 'managed'){continue}
            if($seedTargets -icontains $p.Alias){continue}
            if($targets -icontains $p.Alias){
                $what=if($p.Kind -eq 'unknown'){'身份不明的 '+$p.Alias}else{$p.Alias+'（'+$p.Kind+'）'}
                $warnings+=@('现有代理 '+$what+' 会被备份后覆盖，其功能随之停用；卸载时原样恢复。')
            }elseif($p.Kind -in @('optiscaler','dxvk','enb','specialk','reshade')){
                $warnings+=@('同目录的 '+$p.Alias+' 是 '+$p.Kind+'，将与 033 同进程运行，兼容性未验证。')
            }
        }
        if($Survey.ForeignDirectories.Count){$warnings+=@('目录里有其它工具残留（'+($Survey.ForeignDirectories -join '、')+'），本安装器不会动它们。')}
    }
    switch($Survey.Existing.Kind){
        'managed'{ if($decision -ne 'refuse'){$decision=$(if($warnings.Count){'warn-install'}else{'install'});$warnings+=@('本目录已有本安装器管理的 033（'+[string]$Survey.Existing.Managed.State.Version+'），这次是更新；首次原件保留。')} }
        'old-package'{
            $text='检测到旧版 033 一键包（'+(Split-Path -Leaf $Survey.Existing.OldRecord)+'），它自带的卸载器在：'+$Survey.Existing.OldUninstaller+'。'
            # 2026-09-11 回退到 5.0：5.0 安装前自动调旧卸载器、不问。游戏自带的 libxess_fg / libxell / ReShade.ini 先保护起来再调。
            if($decision -ne 'refuse'){$decision='handover-then-install';$warnings+=@($text+'将先用它静默还原（游戏自带的同名文件先备份保护），再安装。')}
        }
        'residue'{
            # 5.0 碰到旧残留照样装：现有文件按原件备份进备份库，卸载时原样放回。
            # 2026-09-12 业主要求 V6 能清掉 V5 及更早的东西，New-033ManagedPlan 装之前会先清场，
            # 所以这句不能再说「其余残留不动」—— 那是改之前的行为，跟现在做的正好相反。
            $warnings+=@('目录里有旧版/手工安装的 033 残留：'+($Survey.Existing.Markers -join '、')+
                '。装之前会先把属于 033 的那些清掉（全部先抄进备份库，卸载时该放回的放回）；别人的模组一个不动。')
        }
    }
    if($decision -eq 'install' -and $warnings.Count){$decision='warn-install'}
    [pscustomobject]@{Decision=$decision;Route=$route;Manual=[bool]$Manual;Reasons=$reasons;Warnings=$warnings;Components=@($components);ApiCompanions=@($apiCompanions);Needs=@($needs|Select-Object -Unique);
        Overrides=@(@($(if($AllowNativeDlss){'AllowNativeDlss'}),$(if($HandoverOld){'HandoverOld'}))|Where-Object {$_})}
}
function Invoke-033OldPackageHandover($Survey,[string]$Exe,[string]$Vault){
    $u=$Survey.Existing.OldUninstaller
    if(-not $u -or -not (Test-033OldPackageUninstaller $u)){throw '没有可用的旧版卸载器，无法自动交接'}
    $uHash=Get-033FileHash $u;$rHash=$(if($Survey.Existing.OldRecord){Get-033FileHash $Survey.Existing.OldRecord}else{$null})
    $psExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if(-not(Test-Path -LiteralPath $psExe)){$psExe='powershell.exe'}
    # 2026-09-11：5.0 卸载器的固定删除清单（dlss5_uninstall.ps1:41-59）不看归属，生化危机9、刺客信条影自带的
    # libxess_fg.dll / libxell.dll 被它删了游戏就进不去。不是 5.0 包里那份字节的，先复制到游戏目录外，卸载后放回。
    $dir=Split-Path -Parent $Exe
    $v5Own=@{'libxess_fg.dll'='EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27';'libxell.dll'='D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7'}
    $keepDir=Join-Path ([IO.Path]::GetTempPath()) ('033-handover-'+[Guid]::NewGuid().ToString('N'))
    $kept=@()
    foreach($name in @('libxess_fg.dll','libxell.dll','ReShade.ini','ReShadePreset.ini','nvngx_dlssnr.dll')){
        $p=Join-Path $dir $name
        if(-not(Test-Path -LiteralPath $p -PathType Leaf)){continue}
        $h=Get-033FileHash $p
        if($v5Own.ContainsKey($name) -and $h -ieq $v5Own[$name]){continue}
        [void][IO.Directory]::CreateDirectory($keepDir);Copy-Item -LiteralPath $p -Destination (Join-Path $keepDir $name) -Force
        $kept+=@(@{Name=$name;Hash=$h})
    }
    # 5.0 卸载器还会把 reshade-shaders 整个递归删掉（dlss5_uninstall.ps1:59,458-461），玩家自己放的着色器也没了。
    # 不是 5.0 自己那几份（DLSS5_Feed.fx、Signature033.fx、LumeniteFX 五件，按字节认）的，先复制出来，删了就放回。
    $v5ShaderHashes=@('955D911D3B567C57F4E0B44E528DAE3F3DF286FD8FD3E775B9F6B5DDD561AA94','17048F3A36DBBCFA51391BEE7326245527D684AAD9CA6FE1B243D5B1E7CA20DE','463D2BADA33FD3058383BC0273ABA74DF9D5F1DB75FE13D8F305A22BF94526E3','4A27B2F3676CD836E460BE3BB7A094CBF7DDD74E29B19D88FF7381066FCB94D8','736E3F39FC0C48A405C4D10B0D158502009DB940E2673FF778E6B919D3C1A55F','8826D613944BE27E14095982B20098FFF6D45E91C7D4028473C2F5B784C80028','709EC414649B74E573CA0F12A5EF25998D332238F7CAB14A3D91053C8D388CAB')
    $shaderRoot=Join-Path $dir 'reshade-shaders';$keptShaders=@()
    if(Test-Path -LiteralPath $shaderRoot -PathType Container){
        foreach($sf in @(Get-ChildItem -LiteralPath $shaderRoot -File -Recurse -Force -ErrorAction SilentlyContinue)){
            if($v5ShaderHashes -contains (Get-033FileHash $sf.FullName)){continue}
            $srel=$sf.FullName.Substring($shaderRoot.Length+1)
            $sdst=Join-Path (Join-Path $keepDir 'reshade-shaders') $srel;[void][IO.Directory]::CreateDirectory((Split-Path -Parent $sdst))
            Copy-Item -LiteralPath $sf.FullName -Destination $sdst -Force;$keptShaders+=@($srel)
        }
    }
    # 业主规矩：日志和崩溃转储不能永久删。5.0 卸载器的固定清单里有 ReShade.log*、dlss5-*.log、dgVoodoo.log、dlssg_to_fsr3.log，
    # 还会整个删掉 host64（宿主日志、崩溃转储在里面）。跑它之前先复制进备份库 handover-evidence\<时间>，按原相对路径放，附哈希清单；复制失败就不跑。
    $dirT=$dir.TrimEnd('\');$logRe='(?i)(\.log\d*|\.dmp|\.mdmp)$'
    $logFiles=@(Get-ChildItem -LiteralPath $dirT -File -Force -ErrorAction SilentlyContinue|Where-Object {$_.Name -match $logRe -and $_.Name -match '(?i)^(ReShade\.log|dlss5-|dgVoodoo\.log|dlssg_to_fsr3\.log)'})
    $hostDir=Join-Path $dirT 'host64'
    if(Test-Path -LiteralPath $hostDir -PathType Container){$logFiles+=@(Get-ChildItem -LiteralPath $hostDir -File -Recurse -Force -ErrorAction SilentlyContinue|Where-Object {$_.Name -match $logRe})}
    $logCopy=$null;$logRows=@()
    if($logFiles.Count){
        $logCopy=Join-Path $(if($Vault){$Vault}else{$keepDir}) ('handover-evidence\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
        foreach($lf in $logFiles){
            $lrel=$lf.FullName.Substring($dirT.Length+1);$ldst=Join-Path $logCopy $lrel
            [void][IO.Directory]::CreateDirectory((Split-Path -Parent $ldst));Copy-Item -LiteralPath $lf.FullName -Destination $ldst -Force -ErrorAction Stop
            if((Get-033FileHash $ldst) -ine (Get-033FileHash $lf.FullName)){throw ('旧版卸载前备份日志没对上：'+$lrel+'，没有运行旧卸载器')}
            $logRows+=@(@{Path=$lrel;SHA256=(Get-033FileHash $ldst);Bytes=$lf.Length})
        }
        Write-033Json (Join-Path $logCopy 'HANDOVER-LOGS.json') @{Game=$Exe;CopiedAt=(Get-Date).ToUniversalTime().ToString('o');Files=@($logRows)}
    }
    $output=''
    try{$output=(& $psExe -NoProfile -ExecutionPolicy Bypass -File $u -GameExe $Exe -Silent -NoSplash 2>&1|Out-String)}catch{$output=$_.Exception.Message}
    if($output.Length -gt 4000){$output=$output.Substring(0,4000)}
    $putBack=@()
    foreach($k in $kept){
        $p=Join-Path $dir $k.Name
        if(-not(Test-Path -LiteralPath $p -PathType Leaf)){Copy-Item -LiteralPath (Join-Path $keepDir $k.Name) -Destination $p;$putBack+=@($k.Name)}
    }
    $shaderBack=0
    foreach($srel in $keptShaders){
        $sp=Join-Path $shaderRoot $srel
        if(-not(Test-Path -LiteralPath $sp -PathType Leaf)){[void][IO.Directory]::CreateDirectory((Split-Path -Parent $sp));Copy-Item -LiteralPath (Join-Path (Join-Path $keepDir 'reshade-shaders') $srel) -Destination $sp;$shaderBack++}
    }
    if($shaderBack){$putBack+=@('reshade-shaders 里你自己的 '+$shaderBack+' 个文件')}
    if($keptShaders.Count){$kept+=@(@{Name='reshade-shaders';Hash=$null})}
    $after=Get-033ExistingInstall $dir $Exe $null
    $left=@($after.Markers|Where-Object {$_ -ne 'dlss5_uninstall.ps1'})
    $notes=@()
    if($putBack.Count){$notes+=@('旧卸载器删掉了游戏自带的 '+($putBack -join '、')+'，已原样放回。')}
    if($logCopy){$notes+=@('旧卸载器会删掉的 '+$logRows.Count+' 个日志 / 崩溃转储已先复制到备份库：'+$logCopy)}
    if($after.OldRecord -or $left.Count){$notes+=@('旧卸载器运行后仍有残留：'+($left -join '、')+'；照样继续安装，会被覆盖的文件先按原件备份。')}
    @{Uninstaller=$u;UninstallerSHA256=$uHash;RecordSHA256=$rHash;Output=$output;CompletedAt=(Get-Date).ToUniversalTime().ToString('o');Notes=$notes;KeptCopy=$(if($kept.Count){$keepDir}else{$null});LogCopy=$logCopy}
}
function Format-033SurveyReport($Survey,$Verdict){
    $sb=[Text.StringBuilder]::new()
    [void]$sb.AppendLine('== 033 安装前侦察 ==')
    [void]$sb.AppendLine('游戏主程序：'+$Survey.Exe.Path)
    $shownApis=@();if($Survey.PSObject.Properties['EffectiveGraphicsApis']){$shownApis=@($Survey.EffectiveGraphicsApis|ForEach-Object {$_})}
    if(-not $shownApis.Count){$shownApis=@($Survey.GraphicsApis|ForEach-Object {$_})}
    $shownApis=@($shownApis|Where-Object {$_ -is [string]}|Sort-Object -Unique)
    $apiNote=''
    if($Survey.PSObject.Properties['ApiEvidence'] -and $Survey.ApiEvidence -ne 'imports' -and $shownApis.Count){$apiNote='（动态加载，程序里有模块名，非静态导入）'}
    [void]$sb.AppendLine('  位数/接口：'+$Survey.Exe.Architecture+' / '+$(if($shownApis.Count){($shownApis -join '+')+$apiNote}elseif($Survey.Apis.Count){'仅 dxgi（接口需运行时判断）'}else{'未见图形接口导入'}))
    if($Survey.PSObject.Properties['UnityOpenGlDemoted'] -and $Survey.UnityOpenGlDemoted){[void]$sb.AppendLine('  Unity 游戏：主程序静态导入的 opengl32 只是可选后端，按 D3D11 处理（5.0 规则）')}
    if($Survey.Dx12Signals.Count){[void]$sb.AppendLine('  DX12 迹象：'+($Survey.Dx12Signals -join '、'))}
    if($Survey.LauncherSuspect){[void]$sb.AppendLine('  ! 像启动器；候选主程序：'+(($Survey.Candidates|ForEach-Object {Split-Path -Leaf $_}) -join '、'))}
    if($Survey.Upscalers.Items.Count){
        [void]$sb.AppendLine('原生超分/插帧文件：')
        foreach($i in $Survey.Upscalers.Items){[void]$sb.AppendLine('  '+$i.Name+'  '+$(if($i.Version){$i.Version}else{'?'})+'  '+$i.Owner+$(if($i.Backups.Count){'（备份：'+($i.Backups -join '、')+'）'}else{''}))}
    }else{[void]$sb.AppendLine('原生超分/插帧文件：无')}
    if($Survey.PSObject.Properties['NativeDlss'] -and $Survey.NativeDlss){
        $nd=$Survey.NativeDlss
        $ndText=switch($nd.Kind){'root-files'{'有（根目录文件）'} 'deep-files'{'有（子目录：'+($nd.Evidence -join '、')+'）'} 'needle'{'疑似（程序内字样：'+($nd.Evidence -join '、')+'）'} default{'没有'}}
        # 文件在、但游戏里没有任何二进制引用 NGX/Streamline：那些文件是死的，按"没有"选路。
        if($nd.PSObject.Properties['FilesUnused'] -and $nd.FilesUnused){$ndText='有文件但游戏不用它（查了 '+$nd.BinariesChecked+' 个二进制，无 NGX/Streamline 字样）→ 按【没有自带 DLSS】选路'}
        [void]$sb.AppendLine('自带 DLSS/Streamline：'+$ndText)
    }
    if($Survey.PSObject.Properties['Dx12Runtime'] -and $Survey.Dx12Runtime){
        $rt=$Survey.Dx12Runtime
        $rtText=switch($rt.Kind){'agility-sdk'{'有（Agility SDK：'+($rt.Evidence -join '、')+'）'} 'dxc-files'{'有（DXC 编译器：'+($rt.Evidence -join '、')+'）'} 'strings'{'有（'+($rt.Evidence -join '、')+'）'} default{'没有'}}
        [void]$sb.AppendLine('D3D12 运行时证据：'+$rtText)
    }
    if($Survey.PSObject.Properties['Engine'] -and $Survey.Engine -and $Survey.Engine.ReEngine){[void]$sb.AppendLine('引擎：RE Engine（re_chunk_000.pak）')}
    if($Survey.PSObject.Properties['AntiCheat'] -and @($Survey.AntiCheat).Count){[void]$sb.AppendLine('反作弊：'+(@($Survey.AntiCheat) -join '、')+'（有封号风险，装不装自己定）')}
    if($Survey.Proxies.Count){
        [void]$sb.AppendLine('代理/注入器：')
        foreach($p in $Survey.Proxies){[void]$sb.AppendLine('  '+$p.Alias+$(if($p.Disabled){'（已停用副本）'}else{''})+'  →  '+$p.Kind+'（'+$p.Confidence+'）'+$(if($p.FileVersion){'  '+$p.FileVersion}else{''}))}
    }else{[void]$sb.AppendLine('代理/注入器：无')}
    $e=$Survey.Existing
    $existingText=switch($e.Kind){'managed'{'本安装器管理的 033（'+[string]$e.Managed.State.Version+'）'} 'old-package'{'旧版 033 一键包（有自带卸载器）'} 'residue'{'旧版/手工残留：'+($e.Markers -join '、')} default{'无'}}
    [void]$sb.AppendLine('既有 033：'+$existingText)
    if($Survey.ForeignDirectories.Count){[void]$sb.AppendLine('其它工具目录：'+($Survey.ForeignDirectories -join '、'))}
    [void]$sb.AppendLine('NVIDIA NGX 运行库文件：'+$(if($Survey.Ngx.ShellPresent){'已定位（能否加载以本局日志为准）'}else{'本次未定位（不代表驱动缺失）'}))
    if($Survey.PSObject.Properties['Gpu'] -and $Survey.Gpu){
        $gpuText=$(if(@($Survey.Gpu.Nvidia).Count){(@($Survey.Gpu.Nvidia|ForEach-Object {$g=Get-033NvidiaGeneration $_;$_+$(if($g){'（'+$g+' 系）'}else{'（代数未知）'})}) -join '、')}else{'没有 NVIDIA 显卡（'+$(if(@($Survey.Gpu.Adapters).Count){@($Survey.Gpu.Adapters) -join '、'}else{'未读到'})+'）'})
        [void]$sb.AppendLine('显卡：'+$gpuText)
    }
    if($Survey.PSObject.Properties['FrameGen'] -and $Survey.FrameGen){[void]$sb.AppendLine('游戏自带 DLSS 帧生成：'+$(if($Survey.FrameGen.Present -and $Survey.FrameGen.PSObject.Properties['Streamline'] -and -not $Survey.FrameGen.Streamline){'没有（目录里只有模型文件 '+(@($Survey.FrameGen.Evidence) -join '、')+'，没有 sl.dlss_g.dll：不算游戏自带帧生成）'}elseif($Survey.FrameGen.Present){'有（'+(@($Survey.FrameGen.Evidence) -join '、')+'）'}else{'没有'}))}
    if($Survey.PSObject.Properties['Gpu'] -and $Survey.Gpu){foreach($line in @(Get-033FrameGenCacheReport $Survey)){[void]$sb.AppendLine($line)}}
    if($Verdict){
        [void]$sb.AppendLine('')
        $d=switch($Verdict.Decision){'install'{'可以安装'} 'update'{'更新'} 'warn-install'{'可以安装，但有提醒'} 'handover-then-install'{'需要先还原旧版再安装'} 'refuse'{'不安装'} default{$Verdict.Decision}}
        [void]$sb.AppendLine('== 判断：'+$d+$(if($Verdict.Route){'（路线 '+$Verdict.Route+$(if($Verdict.PSObject.Properties['Manual'] -and $Verdict.Manual){'，手动选择'}else{''})+'）'}else{''})+' ==')
        foreach($r in $Verdict.Reasons){[void]$sb.AppendLine('  × '+$r)}
        foreach($w in $Verdict.Warnings){[void]$sb.AppendLine('  ! '+$w)}
        if($Verdict.PSObject.Properties['Components']){foreach($c in @($Verdict.Components)){[void]$sb.AppendLine($(if($c.Installed){'  + '+$c.Label+'：会装（入口 '+$c.Loader+'）'}else{'  - '+$c.Label+'：不装（'+(@($c.Reasons) -join '；')+'）'}))}}
    }
    $sb.ToString()
}

# 2026-09-17 Fable（优化流程）：同一次运行里 dlss5_install.ps1 先侦察给人看，Invoke-033ManagedOperation -Action Install 又侦察一遍，
#   大游戏两次各走一分多钟的目录扫描。同一进程内 5 分钟以内、同一个主程序的侦察结果直接复用；旧包交接（会改目录）之后用 -NoCache 重扫。
#   计划和事务写入时仍逐文件核对现场快照，缓存只省侦察，不省核对。
$script:K033SurveyCache=@{}
function Get-033GameSurvey([string]$Exe,[string]$Vault,[switch]$NoCache){
    $key=([IO.Path]::GetFullPath($Exe).ToLowerInvariant()+'|'+[string]$Vault)
    if(-not $NoCache -and $script:K033SurveyCache.ContainsKey($key)){
        $hit=$script:K033SurveyCache[$key]
        if(((Get-Date)-$hit.At).TotalMinutes -lt 5){return $hit.Survey}
    }
    $survey=Get-033GameSurveyUncached $Exe $Vault
    $script:K033SurveyCache[$key]=@{At=(Get-Date);Survey=$survey}
    return $survey
}
# 2026-09-17 Fable（找回 5.0 的并列目录镜像）：巫师3 的 bin\x64 与 bin\x64_dx12、燕云的 Win64r 与 Win64rh 装的是同一个游戏的两个入口，
#   5.0 直挂路线会把同样的东西镜像到并列目录并记 twin=，卸载一起清；6.1.3 只装所选的那一个，另一个入口就没有 033。
#   这里只认「同一个父目录下、另一个目录里有同名主程序、位数相同」这一种布局，不跨目录猜；已被别的安装组登记的入口不动。
function Get-033TwinEntries([string]$Exe,[string]$Vault){
    $full=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $full;$parent=Split-Path -Parent $dir;$name=[IO.Path]::GetFileName($full)
    if(-not $parent -or $parent.Length -le 3){return @()}
    # Same basename/architecture alone is not evidence of one game. In a Steam
    # library it can join two independent installations. Auto-expand only an
    # established in-game binary layout; other targets must be selected explicitly.
    $twinLayout='^(?:x64|win64)(?:[_-]?(?:dx11|dx12|vulkan|r|rh))?$'
    if((Split-Path -Leaf $parent) -notin @('bin','binaries')){return @()}
    if((Split-Path -Leaf $dir) -notmatch $twinLayout){return @()}
    $pe=Get-033PeInfo $full;if($pe.Status -notin @('valid','unknown') -or $pe.Architecture -notin @('x86','x64')){return @()}
    $twins=@()
    foreach($sib in @(Get-ChildItem -LiteralPath $parent -Directory -Force -ErrorAction SilentlyContinue)){
        if($sib.FullName -ieq $dir){continue}
        if($sib.Name -notmatch $twinLayout){continue}
        if($sib.Name -in @($script:FOREIGN_TOOL_DIRS) -or $sib.Name -match '^(_|\.)'){continue}
        $cand=Join-Path $sib.FullName $name
        if(-not (Test-Path -LiteralPath $cand -PathType Leaf)){continue}
        $cpe=Get-033PeInfo $cand;if($cpe.Status -notin @('valid','unknown') -or $cpe.IsDll -or $cpe.Architecture -ne $pe.Architecture){continue}
        if($Vault -and (Test-033VaultKnowsExe $Vault $cand)){continue}
        $twins+=@($cand)
    }
    return @($twins|Sort-Object)
}
