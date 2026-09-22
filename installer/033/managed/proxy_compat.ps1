# =====================================================================
#  代理顶替能力 —— 2026-09-12 业主：「我要的是安装器能把他们正确安装好了，别到处出事」
#
#  033 是把自己的 DLL 改名成一个系统 DLL 名放进游戏目录（dxgi.dll / d3d11.dll …），
#  让游戏加载我们而不是系统那个。这件事有个硬条件常被忽略：
#      ★游戏从那个名字导入的每一个函数，我们的文件都必须导出★
#  少一个，游戏启动当场失败（0xc000007b / 找不到程序入口点）—— 也就是评论区
#  「装完游戏就打不开了」那一整类。
#
#  实测（2026-09-12，拿发布包的入口文件对真实游戏）：我们的文件导出 494 个名字，
#  dxgi / d3d11 / d3d12 / d3d9 / d3d10 / ddraw / d2d1 / opengl32 / dinput 都顶得住，
#  但 winmm.dll 一个函数都没有、version.dll 也一个都没有。而巫师3 的主程序
#  【确实从 winmm 导入 6 个】—— 真挂过去必崩。所以挑挂载名之前先在这里过一道。
#
#  不写死名单：拿【这次真要装的那个文件】的导出表跟【这个游戏真正导入的函数名】比，
#  换了入口文件结论自动跟着变。
#
#  ★只读需要的那一段★ 现代游戏主程序动辄几百 MB（鬼武者 603 MB），整个读进内存
#  又慢又占。这里只把「装着导入表/导出表的那个节」读出来。
#  ★读不到就当不能用★ 解析失败返回 $null，调用方把「不确定」当成「不要用这个名字」——
#  猜错的代价是游戏起不来，宁可不换。
# =====================================================================

function Get-033PeSectionSlice([string]$Path,[int]$DirectoryIndex,[long]$MaxSection=134217728){
    # 返回 @{Bytes=节的字节;Base=节的 RVA;DirRva=目录 RVA;Size=目录大小}；任何异常返回 $null。
    $fs=$null
    try{
        if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
        $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        if($fs.Length -lt 512){return $null}
        $hdr=[byte[]]::new(4096)
        if($fs.Read($hdr,0,4096) -lt 512){return $null}
        if($hdr[0] -ne 0x4D -or $hdr[1] -ne 0x5A){return $null}
        $pe=[BitConverter]::ToInt32($hdr,0x3C)
        if($pe -le 0 -or $pe+264 -ge 4096){return $null}
        if([BitConverter]::ToUInt32($hdr,$pe) -ne 0x4550){return $null}
        $nsec=[BitConverter]::ToUInt16($hdr,$pe+6);$osz=[BitConverter]::ToUInt16($hdr,$pe+20);$opt=$pe+24
        if($nsec -le 0 -or $nsec -gt 96 -or $osz -lt 96){return $null}
        $magic=[BitConverter]::ToUInt16($hdr,$opt)
        $dd=$opt+$(if($magic -eq 0x20b){112}elseif($magic -eq 0x10b){96}else{return $null})
        if($dd+8*($DirectoryIndex+1) -gt 4096){return $null}
        $dirRva=[BitConverter]::ToUInt32($hdr,$dd+8*$DirectoryIndex)
        $dirSize=[BitConverter]::ToUInt32($hdr,$dd+8*$DirectoryIndex+4)
        if(-not $dirRva){return @{Bytes=$null;Base=0;DirRva=0;Size=0;Wide=($magic -eq 0x20b)}}
        $secTab=$pe+24+$osz
        if($secTab+40*$nsec -gt 4096){return $null}
        for($i=0;$i -lt $nsec;$i++){
            $o=$secTab+40*$i
            $va=[BitConverter]::ToUInt32($hdr,$o+12);$vs=[BitConverter]::ToUInt32($hdr,$o+8)
            $raw=[BitConverter]::ToUInt32($hdr,$o+20);$rawSize=[BitConverter]::ToUInt32($hdr,$o+16)
            $span=[Math]::Max($vs,$rawSize)
            if($dirRva -ge $va -and $dirRva -lt $va+$span){
                if($rawSize -le 0 -or $rawSize -gt $MaxSection){return $null}
                if([long]$raw+$rawSize -gt $fs.Length){return $null}
                $buf=[byte[]]::new($rawSize)
                [void]$fs.Seek([long]$raw,[IO.SeekOrigin]::Begin)
                $got=0
                while($got -lt $rawSize){
                    $n=$fs.Read($buf,$got,$rawSize-$got)
                    if($n -le 0){break}
                    $got+=$n
                }
                if($got -lt $rawSize){return $null}
                return @{Bytes=$buf;Base=[long]$va;DirRva=[long]$dirRva;Size=[long]$dirSize;Wide=($magic -eq 0x20b)}
            }
        }
        return $null
    }catch{return $null}
    finally{ if($fs){$fs.Dispose()} }
}

function Get-033PeExportNames([string]$Path){
    $s=Get-033PeSectionSlice $Path 0
    if($null -eq $s){return $null}
    $out=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    # ★逗号不能省★ PowerShell 会把返回的集合在管道上展开：空集合展开成「什么都没有」，
    #   调用方拿到的就是 $null —— 而 $null 在上面的判定里表示「读不到」，跟「导出 0 个」
    #   是相反的结论。仓库里 Get-033FileMarkers 的 `return ,$found` 就是防这个。
    if(-not $s.Bytes -or -not $s.DirRva){return ,$out}   # 没有导出表 = 一个都不导出
    try{
        $b=$s.Bytes;$base=$s.Base
        $at={param([long]$rva) $o=$rva-$base; if($o -lt 0 -or $o -ge $b.Length){return -1}; return [int]$o}
        $e=& $at $s.DirRva
        if($e -lt 0 -or $e+40 -gt $b.Length){return $null}
        $count=[BitConverter]::ToUInt32($b,$e+24);$namesRva=[BitConverter]::ToUInt32($b,$e+32)
        if($count -gt 65536){return $null}
        $tbl=& $at $namesRva
        if($tbl -lt 0){return $null}
        for($i=0;$i -lt $count;$i++){
            $p=$tbl+4*$i
            if($p+4 -gt $b.Length){break}
            $q=& $at ([BitConverter]::ToUInt32($b,$p))
            if($q -lt 0){continue}
            $z=$q;while($z -lt $b.Length -and $b[$z] -ne 0){$z++}
            [void]$out.Add([Text.Encoding]::ASCII.GetString($b,$q,$z-$q))
        }
        return ,$out
    }catch{return $null}
}

function Get-033PeImportedFunctions([string]$Path){
    # @{ 'dxgi.dll' = @('CreateDXGIFactory2', ...) }；序号导入记成 '#123'。解析不了返回 $null。
    $s=Get-033PeSectionSlice $Path 1
    if($null -eq $s){return $null}
    if(-not $s.Bytes -or -not $s.DirRva){return @{}}    # 没有导入表
    try{
        $b=$s.Bytes;$base=$s.Base;$wide=[bool]$s.Wide
        $at={param([long]$rva) $o=$rva-$base; if($o -lt 0 -or $o -ge $b.Length){return -1}; return [int]$o}
        $t=& $at $s.DirRva
        if($t -lt 0){return $null}
        # ★只要有一处解析不出来就整份判为「不确定」★ 2026-09-12 鬼武者（603 MB）实测：
        #   它的导入表跨节，只读一个节时后面的名字落在切片外。以前这种情况返回空表，
        #   而空表在调用方眼里等于「这个游戏没从这个名字导入东西」= 顶得住 —— 失败却判成通过，
        #   正是最危险的方向。现在一律返回 $null，调用方把不确定当成「不要用这个名字」。
        $res=@{};$guard=0;$complete=$true
        while($true){
            if(++$guard -gt 4096){return $null}
            if($t+20 -gt $b.Length){$complete=$false;break}
            $oft=[BitConverter]::ToUInt32($b,$t);$nameRva=[BitConverter]::ToUInt32($b,$t+12);$first=[BitConverter]::ToUInt32($b,$t+16)
            if(-not $nameRva){break}
            $q=& $at $nameRva
            if($q -lt 0){$complete=$false;break}
            $z=$q;while($z -lt $b.Length -and $b[$z] -ne 0){$z++}
            $dll=([Text.Encoding]::ASCII.GetString($b,$q,$z-$q)).ToLowerInvariant()
            $thunk=& $at $(if($oft){$oft}else{$first})
            if($thunk -lt 0){$complete=$false;break}
            $fns=@();$step=$(if($wide){8}else{4});$inner=0
            while($thunk -ge 0 -and $thunk+$step -le $b.Length){
                if(++$inner -gt 16384){break}
                $v=$(if($wide){[BitConverter]::ToUInt64($b,$thunk)}else{[uint64][BitConverter]::ToUInt32($b,$thunk)})
                if($v -eq 0){break}
                $high=$(if($wide){[uint64]9223372036854775808}else{[uint64]2147483648})
                if(($v -band $high) -ne 0){$fns+=@('#'+([int]($v -band 0xFFFF)))}
                else{
                    $p=& $at ([long]($v -band 0x7FFFFFFF))
                    if($p -ge 0 -and $p+2 -lt $b.Length){
                        $z2=$p+2;while($z2 -lt $b.Length -and $b[$z2] -ne 0){$z2++}
                        $fns+=@([Text.Encoding]::ASCII.GetString($b,$p+2,$z2-$p-2))
                    }else{$complete=$false}
                }
                $thunk+=$step
            }
            if($res.ContainsKey($dll)){$res[$dll]=@($res[$dll]+$fns|Select-Object -Unique)}else{$res[$dll]=@($fns|Select-Object -Unique)}
            $t+=20
        }
        if(-not $complete){return $null}
        return $res
    }catch{return $null}
}

function Test-033ProxyCanStandIn([string]$MountName,$GameImports,$ProxyExports){
    # @{Ok;Missing;Needed;Reason}
    # ★读不到就判不能用★：猜错的代价是游戏起不来，宁可不换挂载点。
    if($null -eq $ProxyExports){return @{Ok=$false;Missing=@();Needed=0;Reason='读不到我们这个文件的导出表'}}
    if($null -eq $GameImports){return @{Ok=$false;Missing=@();Needed=0;Reason='读不到这个游戏的导入表'}}
    $key=$MountName.ToLowerInvariant()
    $need=@()
    if($GameImports.ContainsKey($key)){$need=@($GameImports[$key]|Where-Object {$_ -and -not $_.StartsWith('#')})}
    if(-not $need.Count){return @{Ok=$true;Missing=@();Needed=0;Reason='这个游戏没有从这个名字按名导入函数'}}
    $missing=@($need|Where-Object {-not $ProxyExports.Contains($_)})
    return @{Ok=($missing.Count -eq 0);Missing=$missing;Needed=$need.Count;Reason=$null}
}
