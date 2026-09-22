# 033 check-up: read the runtime's own logs and Windows crash events after a
# game session and say, in plain language, what ran and what did not.
# Read-only. Never launches or touches the game.
function Read-033LogTail([string]$Path,[int]$MaxBytes=4194304){
    if(-not(Test-Path -LiteralPath $Path)){return $null}
    try{
        $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try{
            $len=$fs.Length;$start=[Math]::Max(0,$len-$MaxBytes);$fs.Position=$start
            $buf=[byte[]]::new($len-$start);$n=0;while($n -lt $buf.Length){$r=$fs.Read($buf,$n,$buf.Length-$n);if(-not $r){break};$n+=$r}
            return [Text.Encoding]::UTF8.GetString($buf,0,$n)
        }finally{$fs.Dispose()}
    }catch{return $null}
}
function Get-033NrReasonText([int]$Code){
    switch($Code){
        24{'成功'} 11{'模型缺失（运行库/转发器没找到）'} 18{'GPU 租约不可用'} 0{'未开始'}
        default{'代码 '+$Code+'（见 nr_stage_diagnostic.h）'}
    }
}
function Get-033Checkup([string]$Exe){
    $Exe=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $Exe;$exeName=Split-Path -Leaf $Exe
    $findings=[Collections.Generic.List[object]]::new();$facts=[ordered]@{}
    function Add([string]$Level,[string]$Text,[string]$Next=''){$findings.Add([pscustomobject]@{Level=$Level;Text=$Text;Next=$Next})}
    $feedLog=Join-Path $dir '033-runtime\dlss5-feed.log'
    $hostLog=Join-Path $dir '033-runtime\host64\dlss5-feed-host.log'
    $coreLogs=@((Join-Path $dir '033-runtime\host64\dlss5-033.log'),(Join-Path $dir 'dlss5-033.log'),(Join-Path $dir '033-runtime\dlss5-033.log'))
    $scaleLogs=@((Join-Path $dir '033-runtime\host64\dlss5-033-nrscale.log'),(Join-Path $dir 'dlss5-033-nrscale.log'))
    $stateFiles=@((Join-Path $dir '033-runtime\host64\dlss5-033.state'),(Join-Path $dir 'dlss5-033.state'),(Join-Path $dir '033-runtime\dlss5-033.state'))
    $reshadeLog=Join-Path $dir 'ReShade.log';$fgLog=Join-Path $dir '033-framegen.log'
    # 挂载点不一定叫 dxgi.dll: OpenGL 路线装成 opengl32.dll, 安装时也可以用 -Proxy 换名。
    # 2026-09-13 Vulkan 路线：入口是 033 核心本体, 名字取自核心 dllmain 认得的那一组
    # (winmm / version / dbghelp / winhttp / wininet), 而且【没有 033-runtime 目录】——
    # 老名单加老判据会把装好的 Vulkan 游戏报成「这个目录里没有 033」, 那是错的诊断。
    $mountNames=@('dxgi.dll','d3d9.dll','d3d10.dll','d3d10_1.dll','d3d11.dll','d3d12.dll','ddraw.dll','d2d1.dll','dinput8.dll','dinput.dll','opengl32.dll',
                  'winmm.dll','version.dll','dbghelp.dll','winhttp.dll','wininet.dll')
    $mounts=@($mountNames|Where-Object {Test-Path -LiteralPath (Join-Path $dir $_)})
    # 核心旁边那两个文件只有 033 会放, 是「装过」的硬证据 (Vulkan 路线全在游戏根目录)。
    $vulkanLayout=(Test-Path -LiteralPath (Join-Path $dir 'nvngx.dll_033.dll')) -and -not (Test-Path -LiteralPath (Join-Path $dir '033-runtime'))
    $installed=($mounts.Count -gt 0) -or (Test-Path -LiteralPath (Join-Path $dir '033-runtime')) -or $vulkanLayout
    if(-not $installed){Add 'fail' '这个目录里没有 033（没有挂载 DLL，也没有 033-runtime）。' '先用安装器安装，再进一次游戏，再体检。'}
    if($vulkanLayout){Add 'info' '这是 Vulkan 路线：033 核心直接挂在游戏目录里，没有 033-runtime 目录，也不带 ReShade —— 下面「没有 dlss5-feed.log」「没有 ReShade.log」这类提示对这条路线不适用，只看 dlss5-033.log。'}
    # ---- Feeder (client) log ----
    $feed=Read-033LogTail $feedLog
    # 2026-09-17 Fable：直挂路线（游戏自带 DLSS）根本没有 Feeder，没有 feed.log 是正常的；只有 Feeder 路线才该报（9-14 记录的体检误导项之一）。
    $feederRoute=(Test-Path -LiteralPath (Join-Path $dir '033-runtime\dlss5-feed.addon64')) -or (Test-Path -LiteralPath (Join-Path $dir '033-runtime\dlss5-feed.addon32'))
    $facts.Route=$(if($vulkanLayout){'Vulkan'}elseif($feederRoute){'Feeder（033 自带 DLSS）'}else{'直挂（游戏自带 DLSS）'})
    if($null -eq $feed){
        if($feederRoute){Add 'warn' '没有 033-runtime\dlss5-feed.log：游戏还没跑过，或者 ReShade 没加载 033 插件。' '进一次游戏到画面出来再体检；若仍没有，看 ReShade.log 是否加载了 dlss5-feed.addon。'}
        elseif(-not $vulkanLayout){Add 'info' '这是直挂路线（游戏自带 DLSS），没有 Feeder 日志是正常的，看下面的核心记录。'}
    }else{
        $facts.FeedLogTime=(Get-Item -LiteralPath $feedLog).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
        $lines=$feed -split "`r?`n"
        $attached=@($lines|Where-Object {$_ -match 'attached'}).Count
        $hostOk=@($lines|Where-Object {$_ -match 'host connected|session open|session ready'}).Count
        # 2026-09-12 TGAAC 现场：这版 Feeder 打的是「session ready: queue=…」和「feed: session open」，
        #   而这里只认旧的 'shared set ready'，于是会话明明开了、帧也在送，体检却报「会话没建起来」。
        #   诊断工具误报比不报更坏 —— 玩家会照着去追一个不存在的毛病。关键词跟上，并且
        #   「已经有帧送达」本身就是会话建起来了的铁证。
        $built=@($lines|Where-Object {$_ -match 'shared set ready|session ready|session open|feature ready'}).Count
        $delivered=@($lines|Where-Object {$_ -match 'frame \d+ delivered'})
        $timing=@($lines|Where-Object {$_ -match '600 frames: feed CPU'})
        $crash=@($lines|Where-Object {$_ -match 'CRASH RECORDED'})
        $createFail=@($lines|Where-Object {$_ -match 'CreateFeature raised exception|feature create crashed'})
        # 首次初始化早于着色器编译，日志里必然先有一句「DLSS5_Feed.fx is not loaded」——那是正常过程，
        #   下面已经单独判为 ok。这里再当成「插件掉线」报一次纯属吓人，排除掉。
        $disabled=@($lines|Where-Object {$_ -match 'FeedDisable|went away|host.*exited|disabled'})
        $feedFxLater=@($lines|Where-Object {$_ -match 'DLSS5_Feed\.fx technique found'}).Count
        if(-not $feedFxLater){$disabled+=@($lines|Where-Object {$_ -match 'is not loaded'})}
        $async=@($lines|Where-Object {$_ -match 'previous-frame mode'})
        if(-not $attached){Add 'fail' 'dlss5-feed.log 里没有插件附着记录。' '看 ReShade.log 里有没有加载 dlss5-feed.addon 的报错。'}
        if($attached -and -not $hostOk){Add 'fail' '033 插件附着了，但没有连上处理端（host/同设备会话）。' '看同目录 033-runtime\host64\dlss5-feed-host.log（32 位游戏）或本日志前几十行的报错。'}
        if($hostOk -and -not $built -and -not $delivered.Count){Add 'fail' '处理端连上了，但纹理/会话没建起来。' '把 dlss5-feed.log 发出来。'}
        if($crash.Count){
            $last=$crash[-1]
            Add 'fail' ('033 插件记录到进程崩溃：'+($last -replace '^\s*\d\d:\d\d:\d\d\.\d+\s*','')) '这是崩溃现场；下面的事件记录会给出故障模块。'
        }
        if($createFail.Count){
            $hint='Feeder 自建 DLSS 特征时出错。'
            $nrscaleHook=$false
            foreach($sl in $scaleLogs){$t=Read-033LogTail $sl;if($t -and $t -match '钩驱动薄壳'){$nrscaleHook=$true}}
            if($nrscaleHook){$hint+='同时核心钩住了驱动的 NGX 薄壳（dlss5-033-nrscale.log 有"钩驱动薄壳"）——两套 NGX 钩子和 Feeder 自建 DLSS 撞在一个进程里，这是审判之眼 2026-09-10 的崩溃形态。'}
            $nativeSr=Test-Path -LiteralPath (Join-Path $dir 'nvngx_dlss.dll')
            if($nativeSr){$hint+='目录里还有游戏自己的 nvngx_dlss.dll，本包不适合自带 DLSS 的游戏。'}
            Add 'fail' ('DLSS 特征创建失败 '+$createFail.Count+' 次：'+$hint) '自带 DLSS 的游戏请改用主线 033 一键包；老游戏若出现此项，把 dlss5-feed.log 和 dlss5-033-nrscale.log 发出来。'
        }
        if($delivered.Count){$facts.FramesDelivered=($delivered[-1] -replace '^.*frame (\d+) delivered.*$','$1')}
        if($timing.Count){
            $facts.LastTiming=($timing[-1] -replace '^\s*\d\d:\d\d:\d\d\.\d+\s*\[feed(32)?\]\s*','')
            $fpsAll=@($timing|ForEach-Object {if($_ -match '\(([\d.]+) fps\)'){[double]$Matches[1]}})
            if($fpsAll.Count){$facts.FpsRange=('{0:0.#}–{1:0.#} fps' -f ($fpsAll|Measure-Object -Minimum).Minimum,($fpsAll|Measure-Object -Maximum).Maximum)}
            $feedMs=@($timing|ForEach-Object {if($_ -match 'feed CPU ([\d.]+) ms/frame'){[double]$Matches[1]}})
            if($feedMs.Count){$facts.FeedMsRange=('{0:0.#}–{1:0.#} ms/帧' -f ($feedMs|Measure-Object -Minimum).Minimum,($feedMs|Measure-Object -Maximum).Maximum)}
        }
        if($async.Count){$facts.PreviousFrameMode=$(if($async[-1] -match '\bON\b'){'开'}else{'关'})}
        if($built -and $delivered.Count -and -not $crash.Count){Add 'ok' ('帧持续送达处理端（最后一帧 #'+$facts.FramesDelivered+'）。')}
        # ---- 引导图质量：DX11 这条路真正决定画质的地方 ----
        # 2026-09-12 拿 TGAAC（纯 DX11、自己没有 DLSS）的现场日志对出来的：Feeder 会在游戏进程里
        # 自建 D3D12 会话、跨 API 共享纹理，把一份【用 ReShade 着色器现场合成的】DLSS 契约喂进去，
        # 033 再骑在那次求值上做 NR —— 整条链是通的。但深度和运动矢量要是空的，DLSS/NR 就退化成
        # 纯空间处理，表现就是评论区那句「有效果但发糊、拖影」。日志里每 600 帧就打一次探针结论，
        # 而体检以前一个字都没读。按 DLSS5-Feeder 作者给的判据解读并给出确切做法。
        $mvProbe=@($lines|Where-Object {$_ -match 'MV probe'})
        $depthProbe=@($lines|Where-Object {$_ -match 'Depth probe'})
        $fxLine=@($lines|Where-Object {$_ -match '\[feed(32)?\] effects:'})
        if($fxLine.Count){
            $fx=$fxLine[-1]
            $facts.FeedEffects=($fx -replace '^\s*\d\d:\d\d:\d\d\.\d+\s*\[feed(32)?\]\s*effects:\s*','')
            if($fx -match 'DLSS5_Feed\.fx technique MISSING'){
                Add 'fail' 'Feeder 的着色器没加载（DLSS5_Feed.fx 的技术/纹理都找不到）：这条路是靠着色器现场合成 DLSS 输入的，没有它整条路就是空转。' '把 DLSS5_Feed.fx 放进游戏目录的 reshade-shaders\Shaders，进游戏按 Home 打开 ReShade，点一次「Reload」。注意：首次安装后常常要重载一次或重进一次游戏才认得到（日志里第一次初始化是 MISSING，运行时重建之后才 found）。'
            }
            if($fx -match 'DLSS5_MV_PROVIDER=(\d)[^>]*->\s*([^,]+)'){
                $prov=$Matches[1];$provState=$Matches[2].Trim()
                $facts.MvProvider=('模式 '+$prov+' -> '+$provState)
                if($provState -match 'not installed|DISABLED|FAILED TO COMPILE|none'){
                    Add 'fail' ('运动矢量提供者没在工作：DLSS5_MV_PROVIDER='+$prov+'，状态「'+$provState+'」。没有运动矢量，DLSS 只能按静止画面处理，动起来就拖影。') '装 LumeniteFX（着色器放 reshade-shaders\Shaders，贴图放 reshade-shaders\Textures），在 ReShade 里选中 DLSS5_Feed.fx 把预处理定义 DLSS5_MV_PROVIDER 设成 3，重载效果，然后勾上「LUMENITE: Kernel 2.0」并把它排到「DLSS 5 Feed」上面。（旧的 ReshadeMotionEstimation 在 ReShade 6.8 上编译不过，别用。）'
                }
            }
        }
        if($mvProbe.Count -and $mvProbe[-1] -match 'no motion vectors'){
            $pct='';if($mvProbe[-1] -match '([\d.]+)% non-zero'){$pct=$Matches[1]+'%'}
            $facts.MvProbe=($mvProbe[-1] -replace '^.*MV probe','MV probe')
            Add 'warn' ('运动矢量几乎全是 0（非零只有 '+$pct+'）：DLSS 拿不到画面在怎么动，动态画面就会拖影、糊。') '按先后顺序查：① 「LUMENITE: Kernel 2.0」这类运动矢量技术要勾上、而且要排在「DLSS 5 Feed」【上面】，顺序反了就等于没有；② 预处理定义 DLSS5_MV_PROVIDER 要跟你装的那个着色器对上；③ 如果这个游戏本来就是 2D 或文字冒险（画面几乎不动），这一项为空是正常的，不用管。'
        }
        if($depthProbe.Count -and $depthProbe[-1] -match 'flat'){
            $facts.DepthProbe=($depthProbe[-1] -replace '^.*Depth probe','Depth probe')
            Add 'warn' '深度是平的（取样出来全是同一个值）：DLSS 拿不到景深，边缘和远景会糊。' '按先后顺序查：① 进游戏按 Home，在 ReShade 的「Generic Depth」里换一个深度缓冲——游戏通常有好几个，默认那个常常是 UI 或已经被清空的；配合「DLSS 5 Feed – debug view」的深度视图看，能看见场景轮廓才算对；② 还是不行就在 ReShade.ini 的 [DEPTH] 段里调 DepthCopyBeforeClears / DepthCopyAtClearIndex / UseAspectRatioHeuristics；③ 2D 游戏本来就没有深度，这一项为空是正常的。'
        }
        if($disabled.Count -and -not $crash.Count){Add 'warn' ('插件曾停用/掉线：'+(($disabled[-1]) -replace '^\s*\d\d:\d\d:\d\d\.\d+\s*','')) '若只出现一次可忽略；反复出现把日志发出来。'}
    }
    # ---- host (x86 route) ----
    $hostText=Read-033LogTail $hostLog
    if($hostText){
        $facts.HostRoute='32 位隐藏后台'
        if($hostText -match 'exit 0'){Add 'ok' '64 位后台正常退出（exit 0）。'}
        elseif($hostText -match 'pipe closed by the game'){Add 'ok' '64 位后台随游戏关闭。'}
        if($hostText -match 'previous-frame mode'){$facts.PreviousFrameMode='开（后台确认）'}
    }
    # ---- core NR ----
    $coreText=$null;$coreUsed=$null
    foreach($c in $coreLogs){$t=Read-033LogTail $c;if($t){$coreText=$t;$coreUsed=$c;break}}
    if($null -eq $coreText){
        if($feed){Add 'warn' '没有核心日志（dlss5-033.log）：核心没被加载，或路线不同。' '32 位游戏看 033-runtime\host64\，64 位游戏看游戏目录。'}
    }else{
        $facts.CoreLog=$coreUsed
        $stages=@(($coreText -split "`r?`n")|Where-Object {$_ -match '\[033 NR stage\] reason=(\d+)'})
        if($stages.Count){
            $lastStage=$stages[-1];$code=[int]([regex]::Match($lastStage,'reason=(\d+)').Groups[1].Value)
            $pass=[regex]::Match($lastStage,'pass=(\d+)').Groups[1].Value
            $ok=@($stages|Where-Object {$_ -match 'reason=24'}).Count
            if($code -eq 24){Add 'ok' ('神经渲染在跑：最后一次阶段结果=成功，层数='+$pass+'（成功记录 '+$ok+' 条）。')}
            else{Add 'fail' ('神经渲染最后一次阶段结果：'+(Get-033NrReasonText $code)+'。') $(if($code -eq 11){'核心旁边缺 nvngx.dll_033.dll 或 nvngx_dlssnr.dll；重新安装整包。'}else{'把 dlss5-033.log 发出来。'})}
        }else{
            if($coreText -match '已接入'){Add 'warn' '核心接入了 Feeder，但还没有任何神经渲染阶段记录。' '可能没到画面就退出了；再进一次游戏走几秒再体检。'}
            # 2026-09-12 评论区最大的一类「面板一直等待、NR 不生效」（2077 / 红色沙漠 / NBA 2K / 剑星 / 天国拯救2 …）：
            # 核心起来了却一条阶段记录都没有，几乎都是游戏那边没给到它认的输入。按出现频率把三种情况写成人话，
            # 不要只说「把日志发出来」——玩家自己能解决前两种。
            # 2026-09-13 死亡搁浅2：核心 15 秒内看到「超分建了、求值一次没进来」时会在 nrscale 日志写一行 silent-image，
            # 带上两扇门各自钩没钩上。两扇都钩着 = 游戏实际没在用 DLSS；核心门没钩上 = 033 这边的缺口。
            $silent=$null
            foreach($sl in $scaleLogs){
                try{if((Test-Path -LiteralPath $sl) -and (Get-Item -LiteralPath $sl).Length -lt 8MB){
                    $hit=Select-String -LiteralPath $sl -Pattern 'silent-image' -Encoding utf8 | Select-Object -Last 1
                    if($hit){$silent=[string]$hit.Line}}}catch{}
            }
            # 2026-09-17 Fable：DX11 自带 DLSS 的游戏（空之轨迹1st 实测）—— 这一版核心只在 D3D12 求值上做 NR，面板有、NR 永远 0，
            #   ReShade.log 里只有 D3D11CreateDevice。以前这里叫人「打开 DLSS」，那是误导；直说。
            $rsForApi=Read-033LogTail $reshadeLog
            $sawD3D11=[bool]($rsForApi -and $rsForApi -match 'Redirecting D3D11CreateDevice')
            $sawD3D12=[bool]($rsForApi -and $rsForApi -match 'Redirecting D3D12CreateDevice')
            $ticks=@(($coreText -split "`r?`n")|Where-Object {$_ -match '\[033 watchdog\]'}).Count
            if(-not $feederRoute -and $sawD3D11 -and -not $sawD3D12){
                Add 'fail' '游戏跑的是 DX11（ReShade 只记录了 D3D11 设备）：这一版核心只在 D3D12 的 DLSS 求值上做神经渲染，缺 DX11 桥 —— 面板会有、NR 不会出。' '游戏画面设置里有 DX12 选项就切到 DX12 再进；只有 DX11 的游戏这一版做不了，不是你操作的问题。'
            }elseif($silent -and $silent -match '核心钩=1'){
                Add 'warn' '游戏建了 DLSS 超分（或光线重构），但从头到尾没有调用它：画面设置里实际在用的多半是 FSR / XeSS / 原生分辨率。' '把游戏的超分方式改成 DLSS（或 DLAA），退出重进后再体检。'
            }elseif($silent){
                Add 'warn' '游戏建了 DLSS 超分，但 033 只挂上了驱动 NGX 的一扇门，超分的调用走了没挂上的那扇。' '这一版核心会在游戏载入驱动核心后自动补挂；退出重进一次再体检。还是这一条，就把 dlss5-033-nrscale.log 发出来。'
            }elseif($feederRoute){
                Add $(if($ticks -ge 30){'fail'}else{'warn'}) ('核心已加载，但一条神经渲染记录都没有（Feeder 路线，游戏跑了约 '+[int]($ticks/2)+' 秒）：Feeder 自建 DLSS，不需要在游戏里开 DLSS；看上面 dlss5-feed.log 的会话/探针结论。') '先按上面 Feeder 的提示处理（着色器、运动矢量、深度）；都正常还是 0，把 dlss5-feed.log 和 dlss5-033.log 一起发出来。'
            }else{
            Add $(if($ticks -ge 30){'fail'}else{'warn'}) '核心已加载，但一条神经渲染记录都没有：说明游戏还没把它认的画面交过来。' '按顺序试：① 游戏画面设置里把 DLSS 超分打开（选「超分辨率 / Super Resolution」或 DLAA 都行，FSR / XeSS 不算）；② 先把光线重构（Ray Reconstruction）关掉试一次——新版本里光线重构会原样交还给 N 卡运行库、033 照样在它后面工作，但这条组合验证得还不够多，关掉能立刻分清是不是它的问题；③ 都做了还不行，退出游戏重进一次再体检，并把 dlss5-033.log 一起发出来。'
            }
        }
        $life=@(($coreText -split "`r?`n")|Where-Object {$_ -match '\[033 model lifetime\] created=(\d+)'})
        if($life.Count){$facts.ModelsCreated=[regex]::Match($life[-1],'created=(\d+)').Groups[1].Value;$facts.ModelBuildMs=[regex]::Match($life[-1],'last_build_ms=([\d.]+)').Groups[1].Value}
        if($coreText -match 'startup counter cleared'){$facts.SafeModeCounter='已清零'}
    }
    foreach($sf in $stateFiles){
        if(Test-Path -LiteralPath $sf){
            try{$v=[int]([IO.File]::ReadAllText($sf).Trim());$facts.SafeModeState=$v
                if($v -ge 2){Add 'warn' ('安全模式计数='+$v+'：核心认为最近连续 '+$v+' 次没活过启动期，可能已自动关掉神经渲染。') ('若游戏其实没崩，删掉 '+$sf+' 再进游戏。')}
            }catch{}
        }
    }
    # ---- ReShade ----
    # 2026-09-12 评论区：「装完进不去游戏」最大的一类就是这个全局设置被旧版本写成了通用补帧路线。
    # 体检是玩家出事之后第一个会跑的东西，必须在这里直说，别让他们去评论区翻别人转发的解法。
    try{
        $sharedIni=Join-Path $env:LOCALAPPDATA '033Runtime\settings.ini'
        if([IO.File]::Exists($sharedIni)){
            $sharedText=[IO.File]::ReadAllText($sharedIni)
            $facts.SharedSettings=$sharedIni
            if($sharedText -match '(?m)^\s*fg_route\s*=\s*1\s*$'){
                Add 'fail' ('全局设置里 fg_route=1：033 的通用补帧被旧版本设成了默认路线，所有游戏共用这一项。本身没有帧生成的游戏，下次启动会因此进不去或者闪退。') ('把游戏拖到「033安装器.exe」上重装一次，安装器会自动把它改回原生路线，别的设置一个不动。')
            }
        }
    }catch{}
    $rs=Read-033LogTail $reshadeLog
    if($rs){
        $errs=@(($rs -split "`r?`n")|Where-Object {$_ -match '\| ERROR \|'})
        $startupErr=@($errs|Where-Object {$_ -match '033'})
        # 2026-09-12 生化危机9：RE 引擎从 _storage_ 镜像加载宿主，宿主就在自己旁边找核心，找不到就只剩面板。
        # 这条错误有准确的根因和准确的解法，不要让它掉进下面那句「把日志发出来」。
        $loadCore=@($errs|Where-Object {$_ -match 'stage=load_core'})
        if($loadCore.Count){
            $mirrored=$loadCore[-1] -match '(?i)_storage_'
            Add 'fail' ('宿主没找到核心（'+($loadCore[-1] -replace '^.*\| ERROR \|\s*','')+'）：游戏里只会有面板，神经渲染一点都不会跑。') $(if($mirrored){'这是 RE 引擎的镜像目录问题：游戏第一次启动才建 _storage_，老版本安装器赶不上，核心没放进去。用 V6.0.1 或更新的包重装一次就好（先用「一键恢复」卸掉再装）。'}else{'核心文件没在宿主旁边；先用「一键恢复」卸掉，再重新安装整包。'})
        }
        if($startupErr.Count -and -not $loadCore.Count){Add 'fail' ('ReShade 记录了 033 相关错误：'+($startupErr[-1] -replace '^.*\| ERROR \|\s*','')) '把 ReShade.log 发出来。'}
        elseif($errs.Count){Add 'warn' ('ReShade 有 '+$errs.Count+' 条错误（与 033 无关的也算）：'+($errs[-1] -replace '^.*\| ERROR \|\s*','')) ''}
        if($rs -match 'DLSS5_Feed\.fx is not loaded'){
            if($rs -match "Successfully compiled '.*DLSS5_Feed\.fx'"){Add 'ok' 'DLSS5_Feed.fx 先缺后编译成功（正常：第一次初始化早于着色器）。'}
            else{Add 'fail' 'DLSS5_Feed.fx 没有加载。' '检查 reshade-shaders\Shaders 里是否有 DLSS5_Feed.fx；重新安装整包。'}
        }
    }elseif($installed){
        # 2026-09-12 业主：「自动检测太蠢了，很多都检测错误，注入错误」。
        #   没有 ReShade.log = 033 的 DLL 从头到尾没被加载过，也就是【挂载点挑错了】——
        #   评论区最常见的那句「装好了按 Home 没反应」就是这个。以前这里只会含糊地叫人
        #   「确认一下」，玩家只能一个个换着试（剑星那位 8 个全试了）。
        #   现在直接去二进制里查这个游戏真正会加载哪个名字，把答案说出来。
        $proof=$null;try{$proof=Get-033MountEvidence $Exe}catch{$proof=$null}
        $at=$(if($mounts.Count){$mounts -join '、'}else{'没找到挂载 DLL'})
        $best=$null
        if($proof){
            foreach($n in @($script:K033MountCandidates)){
                if($mounts -icontains $n){continue}
                if([string]$proof.Evidence[$n] -ne 'import'){continue}
                $best=$n;break
            }
            $nowOk=$false
            foreach($m in @($mounts)){if([string]$proof.Evidence[([string]$m).ToLowerInvariant()] -eq 'import'){$nowOk=$true}}
            if($best -and -not $nowOk){
                Add 'fail' ('033 装在 '+$at+' 上，但这个游戏从来没加载过它（没有 ReShade.log）。查了主程序和同目录的引擎 DLL：真正会被加载的是 '+$best+'（'+[string]$proof.Where[$best]+' 的导入表里有），而 '+$at+' 在这个游戏里找不到任何痕迹。') ('双击「换挂载点.cmd」，选这个游戏，把挂载点换成 '+$best+'，再进一次游戏。')
            }elseif($nowOk){
                Add 'fail' ('033 装在 '+$at+' 上，这个名字游戏确实会加载，但 ReShade 一次都没启动过。') '多半是：① 你启动的不是这个 exe（很多游戏有启动器，真正的主程序在 Binaries\Win64 之类的子目录里）；② 反作弊或杀毒把它拦了。先确认启动的是同一个 exe。'
            }else{
                Add 'warn' ('没有 ReShade.log：ReShade 没启动过。033 装在 '+$at+' 上。') '双击「换挂载点.cmd」换一个名字试试；也确认一下你启动的就是这个 exe。'
            }
        }else{
            Add 'warn' ('没有 ReShade.log：ReShade 没启动过。033 装在 '+$at+' 上。') '双击「换挂载点.cmd」换一个名字试试；也确认一下你启动的就是这个 exe。'
        }
    }
    # ---- universal FG ----
    $fg=Read-033LogTail $fgLog
    if($fg){
        $ticks=@(($fg -split "`r?`n")|Where-Object {$_ -match '^tick='})
        if($ticks.Count){
            $last=$ticks[-1];$en=[regex]::Match($last,'enabled=(\d)').Groups[1].Value;$gen=[regex]::Match($last,' generated=(\d+)').Groups[1].Value;$real=[regex]::Match($last,' real=(\d+)').Groups[1].Value;$err=[regex]::Match($last,'error=(\d+)').Groups[1].Value
            $facts.FrameGen=('enabled='+$en+' real='+$real+' generated='+$gen+' error='+$err)
            if($en -eq '1' -and [int]$gen -gt 0){Add 'ok' ('通用插帧在生成中间帧（真帧 '+$real+'，中间帧 '+$gen+'）。')}
            elseif($en -eq '1'){Add 'warn' '通用插帧已开但还没有生成中间帧。' '刚开启会先保留真帧几秒；仍为 0 时把 033-framegen.log 发出来。'}
            if($err -ne '0'){Add 'fail' ('通用插帧报错 error='+$err+'。') '把 033-framegen.log 发出来。'}
        }
    }
    # ---- Windows crash events ----
    try{
        $events=@(Get-WinEvent -FilterHashtable @{LogName='Application';Id=@(1000,1001);StartTime=(Get-Date).AddHours(-24)} -ErrorAction Stop|Where-Object {$_.Message -match [regex]::Escape($exeName)}|Select-Object -First 3)
        foreach($ev in $events){
            if($ev.Id -eq 1000){
                $mod=[regex]::Match($ev.Message,'(?:出错模块名称|Faulting module name)[：:]\s*([^，,\r\n]+)').Groups[1].Value
                $off=[regex]::Match($ev.Message,'(?:错误偏移|Fault offset)[：:]\s*([0-9a-fx]+)').Groups[1].Value
                $when=$ev.TimeCreated.ToString('MM-dd HH:mm:ss')
                Add 'fail' ('Windows 记录游戏崩溃（'+$when+'）：故障模块 '+$mod+' 偏移 '+$off+'。') $(if($mod -match 'dbgcore'){'dbgcore 是崩溃转储器，真正的第一现场看上面 033 插件记录的异常位置。'}else{''})
            }
        }
        if(-not $events.Count -and $installed){Add 'ok' '最近 24 小时 Windows 没有记录这个游戏崩溃。'}
    }catch{Add 'warn' '读不到 Windows 应用程序事件日志（权限或服务）。' ''}
    $all=$findings.ToArray()
    $fail=@($all|Where-Object Level -eq 'fail').Count;$warn=@($all|Where-Object Level -eq 'warn').Count
    $verdict=if($fail){'有问题'}elseif($warn){'基本正常，有提醒'}else{'正常'}
    [pscustomobject]@{Exe=$Exe;Verdict=$verdict;Findings=$all;Facts=$facts;CheckedAt=(Get-Date).ToString('yyyy-MM-dd HH:mm:ss')}
}
function Format-033Checkup($Result){
    $sb=[Text.StringBuilder]::new()
    [void]$sb.AppendLine('== 033 体检：'+$Result.Verdict+' ==')
    [void]$sb.AppendLine('游戏：'+$Result.Exe)
    foreach($f in $Result.Findings){
        $mark=switch($f.Level){'ok'{'√'} 'warn'{'!'} default{'×'}}
        [void]$sb.AppendLine('  '+$mark+' '+$f.Text)
        if($f.Next){[void]$sb.AppendLine('      → '+$f.Next)}
    }
    if($Result.Facts.Count){
        [void]$sb.AppendLine('数据：')
        foreach($k in $Result.Facts.Keys){[void]$sb.AppendLine('  '+$k+' = '+[string]$Result.Facts[$k])}
    }
    $sb.ToString()
}
