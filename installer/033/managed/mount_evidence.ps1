# =====================================================================
#  挂载点证据 —— 2026-09-12 业主：「安装器自动检测是不是太蠢了，很多都检测错误，注入错误」
#
#  033 是靠顶替一个【游戏真的会加载的系统 DLL 名】进去的。以前挑这个名字靠三样东西：
#  逐游戏规则表、路线默认值、避开别人的模组 —— 三样都没问过最要紧的那个问题：
#  「这个游戏到底会不会加载这个名字？」猜错的结果就是评论区那句「装好了按 Home 没反应」，
#  而玩家只能一个一个换着试（榮耀再临：剑星 8 个选项全试了都装成功但按不出菜单）。
#
#  这个答案其实写在二进制里，查得到：
#    · 硬证据：主程序或它的引擎 DLL 在【导入表】里直接导入了这个名字（静态或延迟导入）——
#      这种一定会加载。
#    · 软证据：名字以字符串形式出现在二进制里 —— 说明代码里可能 LoadLibrary("xxx.dll")，
#      动态加载看不到导入表，但字符串躲不掉（窄字符和宽字符都查）。
#    · 没证据：两样都没有，挂上去大概率白挂。
#
#  只读、有预算上限、不加载也不执行任何东西。
# =====================================================================

# ReShade 能顶替的系统 DLL 名。顺序 = 同等证据下的偏好顺序（越靠前越常见、越安全）。
# dbghelp / winhttp / wininet 是 2026-09-13 为 Vulkan 路线加的：Vulkan 游戏不加载 dxgi/d3d11 那一套，
# 033 核心挂进去只能用它 dllmain 认得的这几个名字，所以这几个也要有证据可查。
$script:K033MountCandidates=@('dxgi.dll','d3d12.dll','d3d11.dll','d3d10.dll','d3d10_1.dll','d3d9.dll',
                              'd3d8.dll','ddraw.dll','d2d1.dll','opengl32.dll','dinput8.dll','dinput.dll',
                              'winmm.dll','version.dll','dbghelp.dll','winhttp.dll','wininet.dll')

function Get-033MountEvidence([string]$Exe,[int]$MaxBinaries=6,[long]$MaxBytesEach=67108864){
    $Exe=[IO.Path]::GetFullPath($Exe)
    $dir=Split-Path -Parent $Exe
    $evidence=[ordered]@{}
    foreach($n in $script:K033MountCandidates){$evidence[$n]='none'}
    $notes=@{}

    # ---- 1) 导入表：只读文件头，便宜，而且是硬证据 ----
    # 主程序，加上同目录里的 DLL（引擎 DLL 常常才是真正 import dxgi 的那个）。
    # ★必须先把「不是游戏的东西」剔出去★，否则就是循环证据：
    #   · 名字本身就是候选挂载名的文件（dxgi.dll / dinput8.dll …）—— 那是代理或别人的模组，
    #     033 自己装上去的那份也在里面。拿它的导入表当「游戏会加载 dxgi」是自己证明自己。
    #   · 033 自己的运行时文件、以及被挪开的 .dlss5-off 备份。
    $ownish=@('033-engine.dll','nvngx.dll_033.dll','nvngx_dlssnr.dll','033-framegen-provider.dll',
              'dlss5-033.addon64','dlss5-feed.addon64','dlss5-feed.addon32','renodx-dlss5.addon64',
              'ReShade32.dll','ReShade64.dll')
    $pool=@()
    try{$pool=@(Get-ChildItem -LiteralPath $dir -File -Force -ErrorAction Stop|
        Where-Object {$_.Extension -in @('.exe','.dll') -and
                      ($script:K033MountCandidates -notcontains $_.Name.ToLowerInvariant()) -and
                      ($ownish -notcontains $_.Name) -and
                      ($_.Name -notmatch '(?i)\.dlss5-off$')}|
        Sort-Object Length -Descending|Select-Object -First 40)}catch{}
    $scan=@([IO.FileInfo]::new($Exe))+@($pool|Where-Object {$_.FullName -ine $Exe})
    foreach($f in $scan){
        $pe=$null;try{$pe=Get-033PeInfo $f.FullName}catch{continue}
        if(-not $pe -or $pe.Status -ne 'valid'){continue}
        foreach($imp in (@($pe.Imports)+@($pe.DelayImports))){
            $k=([string]$imp).ToLowerInvariant()
            if($evidence.Contains($k) -and $evidence[$k] -ne 'import'){
                $evidence[$k]='import';$notes[$k]=$(if($f.FullName -ieq $Exe){'主程序'}else{$f.Name})
            }
        }
    }

    # ---- 2) 字符串：抓 LoadLibrary 这种动态加载。只查还没有硬证据的名字，且只查几个大件 ----
    $unknown=@($script:K033MountCandidates|Where-Object {$evidence[$_] -eq 'none'})
    if($unknown.Count){
        $strScan=@([IO.FileInfo]::new($Exe))+@($pool|Where-Object {$_.FullName -ine $Exe}|Select-Object -First $MaxBinaries)
        foreach($f in $strScan){
            if(-not $unknown.Count){break}
            $hits=@()
            try{$hits=@(Test-033FileNeedles $f.FullName $unknown -Wide -MaxBytes $MaxBytesEach)}catch{}
            foreach($h in $hits){
                if($evidence[$h] -eq 'none'){$evidence[$h]='string';$notes[$h]=$f.Name}
            }
            $unknown=@($unknown|Where-Object {$evidence[$_] -eq 'none'})
        }
    }
    return [pscustomobject]@{Evidence=$evidence;Where=$notes}
}

function Get-033MountEvidenceRank([string]$Level){
    switch($Level){'import'{2} 'string'{1} default{0}}
}
# 2026-09-17 Fable：Format-033MountEvidence 从未被调用，删除；体检和换挂载点各自直接读 Get-033MountEvidence。
