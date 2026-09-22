# S31 净化后安装（业主 2026-09-22：「完全把燕云恢复成完全干净的目录，然后打上。不仅仅是针对033，是针对所有」；
# 又问「我要是签名呢？」）。只在玩家点「净化后安装」（-Clean）时用；普通安装、升级、还原都不看这里。
# 范围：这次安装的每个入口目录（燕云的 Win64r / Win64rh）和它们的子目录；游戏数据、存档、启动器不碰。
# 挑出「不是游戏的」：
#  1 程序文件（dll / exe / asi / addon …）没有签名，或签名者不是燕云自己用到的厂商。
#    2026-09-22 实测：Win64r / Win64rh 里游戏自己的 300 多个程序文件全部由网易、NVIDIA、微软、Intel、AMD、
#    网心科技、OBS 签名；没签名的只有 033 自己的 5 个。别人给自己的模组签了名也一样挑出来：签名者不在名单里。
#    签名者是这些厂商、但签名验不过（文件被改过、损坏、证书链不通）或读不出签名的，不移走——那多半是游戏
#    自己的文件，移走游戏就起不来，也没有干净的原版可以换上；只在结果里点名，让玩家用游戏启动器的「修复」。
#  2 常见模组放在入口目录里的配置、日志、着色器目录（ReShade / OptiScaler / ENB / Special K / DLSSTweaks /
#    dlssg-to-fsr3 …），以及旧版 033 留下的配置和日志。
#  3 和 1 里某个程序文件同名的配置、日志（xxx.dll / xxx.asi 旁边的 xxx.ini、xxx.log …）。
# 游戏自己的非程序文件（ReadMe、netease.data、Built.version …）不按名字猜，一律不动。
# 033 自己的文件（本包要装的、账本里登记的、运行产物、上面已清的旧 033 残留）由调用方放进 Skip；
# 033-runtime、_033transactions、_DLSS5_备份（5.0 存原件的目录）整个不进来。
# 挑出的文件在同一个安装事务里移进备份库，和 033 的改动记在同一本账上；「还原安装前」原样放回。
# 签名证书的 CN 逐字比对（整个 CN，或第一个逗号前那段）；网易有多个主体，以 NetEase 开头的都算。名单取自 2026-09-22 的实测。
$script:K033GamePublishers=@('NVIDIA Corporation','Microsoft Corporation','Microsoft Windows','Microsoft Windows Publisher',
    'Microsoft Windows Software Compatibility Publisher','Microsoft Windows Hardware Compatibility Publisher',
    'Intel Corporation','Advanced Micro Devices','深圳市网心科技有限公司','OBS Project')
$script:K033CleanProgramExtensions=@('.dll','.exe','.asi','.addon','.addon32','.addon64','.drv','.ax','.ocx','.sys')
$script:K033CleanModFilePatterns=@(
    '^reshade.*\.(ini|log\d*|txt)$','^optiscaler.*\.(ini|log)$','^nvngx\.ini$','^dlssg[_-]to[_-]fsr3.*\.(ini|log)$',
    '^dlsstweaks.*\.(ini|log)$','^(enblocal|enbseries)\.ini$','^enb.*\.log$','^specialk.*\.(ini|log)$',
    '^(dxgi|d3d8|d3d9|d3d10|d3d11|d3d12|dinput8|version|winmm|winhttp|dsound|opengl32|ddraw)\.(ini|log)$',
    '^(dlss5-|033-).*\.(ini|cfg|log\d*|state|txt|json)$')
$script:K033CleanModDirectories=@('reshade-shaders','reshade-presets','reshade-addons','ReShade','OptiScaler','enbseries','SpecialK')
$script:K033CleanNeverTouch=@('033-runtime','_033transactions','_DLSS5_备份')
$script:K033CleanCompanionExtensions=@('.ini','.log','.toml','.json','.cfg','.txt')
function Get-033CleanCommonName([string]$Subject){
    if([string]::IsNullOrWhiteSpace($Subject)){return ''}
    if($Subject -match '(?:^|,\s*)CN="([^"]+)"'){return $Matches[1].Trim()}
    if($Subject -match '(?:^|,\s*)CN=([^,]+)'){return $Matches[1].Trim()}
    return ''
}
function Test-033GamePublisher([string]$Subject){
    $cn=Get-033CleanCommonName $Subject
    if(-not $cn){return $false}
    foreach($name in @($cn,($cn -split ',')[0].Trim())){
        if($name.StartsWith('NetEase',[StringComparison]::OrdinalIgnoreCase)){return $true}
        foreach($p in $script:K033GamePublishers){if($name -ieq $p){return $true}}
    }
    return $false
}
function Get-033CleanSigner([string]$Path){
    # Authenticode（含目录签名）。签名坏了、证书链不通、被改过，都不算有效签名；读不出来记 Error。
    $sig=$null
    try{$sig=Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop}catch{return @{Valid=$false;Status='Error';Subject='';Signer=''}}
    if(-not $sig){return @{Valid=$false;Status='Error';Subject='';Signer=''}}
    $status=[string]$sig.Status;$subject=''
    $cert=$null;try{$cert=$sig.SignerCertificate}catch{$cert=$null}
    if($cert){try{$subject=[string]$cert.Subject}catch{$subject=''}}
    return @{Valid=($status -eq 'Valid');Status=$status;Subject=$subject;Signer=(Get-033CleanCommonName $subject)}
}
function Test-033CleanRemoves([string]$Path){
    # 净化会不会把这个程序文件移走（没签名，或签名者不在名单；带厂商名字但验不过、读不出签名的不动）。
    # 给 20/30 系转接件挑加载器名时用：净化反正要移走的别人的 version.dll，不算占着名字。
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){return $false}
    if($script:K033CleanProgramExtensions -notcontains [IO.Path]::GetExtension($Path).ToLowerInvariant()){return $false}
    $sig=Get-033CleanSigner $Path
    return -not($sig.Status -eq 'Error' -or (Test-033GamePublisher $sig.Subject))
}
function Assert-033CleanSignatureCheck{
    # 净化全靠签名分辨游戏文件。Windows 自带的签名检查要是在这台电脑上不好用（模块加载不了、加密服务停了），
    # 所有游戏文件都会被当成「没签名」——那就一个都不能动，整次安装停下，不写游戏。
    $probe=Join-Path $env:SystemRoot 'System32\kernel32.dll'
    $sig=Get-033CleanSigner $probe
    if(-not($sig.Valid -and (Test-033GamePublisher $sig.Subject))){
        throw ('净化后安装要靠 Windows 自带的签名检查分辨游戏文件，可这台电脑上它现在不好用（系统文件 kernel32.dll 验出来是 '+$sig.Status+'）。这次什么都没改；可以先用普通的「安装 / 升级」。')
    }
}
function Get-033CleanCandidates([string]$Root,$Skip,[string]$Exe=''){
    # 只读，不改任何文件。返回 @{Found=@(@{Path='相对路径（/ 分隔）';Reason='为什么不是游戏的'});Unverified=@(@{Path;Signer;Status})}，
    # 都按路径排序。事务层绝不改写的路径（游戏主程序、存档、资源包，见 Assert-033ManagedEntry）这里也不挑。
    Assert-033CleanSignatureCheck
    $rootFull=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $exeFull=$(if($Exe){[IO.Path]::GetFullPath($Exe)}else{''})
    $found=[ordered]@{};$unverified=[ordered]@{}
    $relOf={param([string]$Full)return $Full.Substring($rootFull.Length+1).Replace('\','/')}
    $eligible={param([string]$Full)
        if($exeFull -and $Full -ieq $exeFull){return $false}
        $rel=& $relOf $Full
        if($rel -match '[*?]' -or $rel -match '(?i)(^|/)(?:saves?|savegames?|_033transactions|_DLSS5_备份)(/|$)' -or $rel -match '(?i)\.(sav|pak|vpk|pck|guard)$'){return $false}
        if($Skip -and $Skip.ContainsKey($rel.ToLowerInvariant())){return $false}
        return $true
    }
    $add={param([string]$Full,[string]$Why)
        if(-not(& $eligible $Full)){return}
        $rel=& $relOf $Full;$key=$rel.ToLowerInvariant()
        if(-not $found.Contains($key)){$found[$key]=@{Path=$rel;Reason=$Why}}
    }
    $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($rootFull)
    while($queue.Count){
        $dir=$queue.Dequeue();$atRoot=($dir -ieq $rootFull)
        foreach($child in @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)){
            # 连接点 / 符号链接既不跟进也不动（Assert-033ManagedEntry 会拒绝它们）。
            if([int]$child.Attributes -band [int][IO.FileAttributes]::ReparsePoint){continue}
            if($atRoot -and ($script:K033CleanNeverTouch -icontains [string]$child.Name)){continue}
            if($child.PSIsContainer){
                if($atRoot -and ($script:K033CleanModDirectories -icontains [string]$child.Name)){
                    foreach($f in @(Get-ChildItem -LiteralPath $child.FullName -Force -File -Recurse -ErrorAction SilentlyContinue)){
                        if([int]$f.Attributes -band [int][IO.FileAttributes]::ReparsePoint){continue}
                        & $add $f.FullName ('常见模组目录 '+$child.Name)
                    }
                    continue
                }
                $queue.Enqueue($child.FullName);continue
            }
            $ext=[IO.Path]::GetExtension([string]$child.Name).ToLowerInvariant()
            if($script:K033CleanProgramExtensions -contains $ext){
                if(-not(& $eligible $child.FullName)){continue}
                $sig=Get-033CleanSigner $child.FullName
                if($sig.Status -eq 'Error' -or (Test-033GamePublisher $sig.Subject)){
                    if(-not $sig.Valid){$rel=& $relOf $child.FullName;$unverified[$rel.ToLowerInvariant()]=@{Path=$rel;Signer=$sig.Signer;Status=$sig.Status}}
                    continue
                }
                & $add $child.FullName $(if($sig.Subject){'签名者不是燕云用到的厂商：'+$(if($sig.Signer){$sig.Signer}else{$sig.Subject})}else{'程序文件没有签名'})
                $base=[IO.Path]::GetFileNameWithoutExtension([string]$child.Name)
                foreach($cext in $script:K033CleanCompanionExtensions){
                    $companion=Join-Path $dir ($base+$cext)
                    if(Test-Path -LiteralPath $companion -PathType Leaf){& $add $companion ('和 '+$child.Name+' 同名的配置或日志')}
                }
                continue
            }
            if($atRoot){
                foreach($pattern in $script:K033CleanModFilePatterns){
                    if([string]$child.Name -imatch $pattern){& $add $child.FullName '常见模组或旧版 033 的配置 / 日志';break}
                }
            }
        }
    }
    return @{Found=@($found.Values|Sort-Object {[string]$_.Path});Unverified=@($unverified.Values|Sort-Object {[string]$_.Path})}
}
function Update-033CleanVerdictText($Verdict,$Survey){
    # 侦察结论里有两句是按「普通安装不动别人的东西」写的；净化后安装时改成这次真正会做的事，报告不自相矛盾。
    if(-not $Verdict -or -not $Verdict.PSObject.Properties['Warnings']){return}
    $foreign=@();if($Survey -and $Survey.PSObject.Properties['ForeignDirectories']){$foreign=@($Survey.ForeignDirectories|Where-Object {$_})}
    $out=@()
    foreach($w in @($Verdict.Warnings)){
        $text=[string]$w
        if($foreign.Count -and $text -match '^目录里有其它工具残留（.*），本安装器不会动它们。$'){
            $moved=@($foreign|Where-Object {$script:K033CleanModDirectories -icontains [string]$_})
            $kept=@($foreign|Where-Object {$script:K033CleanModDirectories -inotcontains [string]$_})
            $parts=@()
            if($moved.Count){$parts+=@('净化后安装会把 '+($moved -join '、')+' 移进备份库（「还原安装前」放回）')}
            if($kept.Count){$parts+=@(($kept -join '、')+' 不动')}
            $text='目录里有其它工具残留（'+($foreign -join '、')+'）：'+($parts -join '；')+'。'
        }elseif($text -match '^同目录的 (.+) 是 (.+)，将与 033 同进程运行，兼容性未验证。$'){
            $text='同目录的 '+$Matches[1]+' 是 '+$Matches[2]+'：净化后安装按签名判断，不是游戏厂商签名的就移进备份库（「还原安装前」放回）。'
        }elseif($text.EndsWith('；别人的模组一个不动。')){
            $text=$text.Substring(0,$text.Length-'；别人的模组一个不动。'.Length)+'；别人的模组这次按净化的规则一起移进备份库（「还原安装前」放回）。'
        }
        $out+=@($text)
    }
    $Verdict.Warnings=$out
}
function Get-033CleanableDirectories([string]$Root,[string[]]$Cleaned){
    # 只读。清掉 $Cleaned（相对路径）之后会整棵变空的目录：子树里至少有一个被清的文件，除此之外没有别的文件、
    # 连接点或读不了的地方。整棵的每一层（包括里面本来就空的子目录）都列出来，深的在前。
    # 本来就空、跟这次净化无关的目录不列；入口目录本身和 033-runtime 这些不列。
    # 提交后按这份清单删空目录；「还原安装前」先按它原样建回目录，再放回文件。
    $rootFull=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $set=@{};foreach($c in @($Cleaned)){if($c){$set[([string]$c).Replace('\','/').ToLowerInvariant()]=$true}}
    if(-not $set.Count){return @()}
    $out=[Collections.Generic.List[string]]::new()
    $visit=$null
    $visit={param([string]$Dir,[bool]$AtRoot)
        # 返回 @{Other=子树里有不清的东西;Hit=子树里有被清的文件;Dirs=还没交出去的子树目录（相对路径）}
        $other=$false;$hit=$false;$dirs=[Collections.Generic.List[string]]::new();$children=$null
        try{$children=@(Get-ChildItem -LiteralPath $Dir -Force -ErrorAction Stop)}catch{return @{Other=$true;Hit=$false;Dirs=$dirs}}
        foreach($child in $children){
            if([int]$child.Attributes -band [int][IO.FileAttributes]::ReparsePoint){$other=$true;continue}
            if($AtRoot -and ($script:K033CleanNeverTouch -icontains [string]$child.Name)){$other=$true;continue}
            if($child.PSIsContainer){
                $r=& $visit $child.FullName $false
                if($r.Other){$other=$true};if($r.Hit){$hit=$true}
                if(-not $r.Other -and $r.Hit){foreach($d in $r.Dirs){$out.Add($d)}}
                elseif(-not $r.Other){foreach($d in $r.Dirs){$dirs.Add($d)}}
                continue
            }
            $rel=$child.FullName.Substring($rootFull.Length+1).Replace('\','/')
            if($set.ContainsKey($rel.ToLowerInvariant())){$hit=$true}else{$other=$true}
        }
        if(-not $AtRoot){$dirs.Add($Dir.Substring($rootFull.Length+1).Replace('\','/'))}
        return @{Other=$other;Hit=$hit;Dirs=$dirs}
    }
    [void](& $visit $rootFull $true)
    return @($out|Select-Object -Unique|Sort-Object @{Expression={([string]$_).Split('/').Count};Descending=$true},@{Expression={[string]$_};Descending=$false})
}
