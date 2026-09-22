# =====================================================================
#  诊断报告 —— 2026-09-12 业主：「033 要弄个诊断日志，我们才好知道具体原因吧？不然都不知道」
#
#  日志其实一直在写（dlss5-033.log / ReShade.log / 033-framegen.log …），体检也早就会读它们。
#  真正缺的是【拿不到现场】：玩家不知道这些文件在哪、更不会一个个翻出来发，
#  于是评论区收到的全是手机拍的控制台照片，十条里定位不了一条。
#
#  所以这里不加任何新的检测逻辑，只做一件事：体检跑完，把这一局所有跟 033 有关的东西
#  汇成【桌面上的一个 txt】，玩家直接把这个文件发过来。每份日志只取最后 200 行，
#  整份文件小到能直接贴、也能直接传。
# =====================================================================
function Get-033DiagTail([string]$Path,[int]$Lines=200){
    if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    try{
        $item=Get-Item -LiteralPath $Path
        $text=''
        # 日志可能正被游戏开着写，必须允许共享读写，否则一个都读不到。
        $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        try{
            if($fs.Length -gt 1048576){[void]$fs.Seek($fs.Length-1048576,[IO.SeekOrigin]::Begin)}
            $sr=[IO.StreamReader]::new($fs,[Text.Encoding]::UTF8,$true)
            $text=$sr.ReadToEnd()
        }finally{$fs.Dispose()}
        $all=@($text -split "`r?`n")
        $tail=if($all.Count -gt $Lines){$all[($all.Count-$Lines)..($all.Count-1)]}else{$all}
        return [pscustomobject]@{Size=$item.Length;Written=$item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss');Total=$all.Count;Text=($tail -join "`r`n")}
    }catch{return $null}
}

function Export-033DiagnosticReport([string]$Exe,[string]$Vault,[string]$CheckupText,[string]$PackageRoot){
    $Exe=[IO.Path]::GetFullPath($Exe);$dir=Split-Path -Parent $Exe;$exeName=Split-Path -Leaf $Exe
    $s=[Text.StringBuilder]::new()
    function W([string]$t=''){[void]$s.AppendLine($t)}
    function Section([string]$t){W '';W ('======== '+$t+' ========')}

    W '================ 033 诊断报告 ================'
    W '这份文件由 033 体检自动生成，发给作者用来定位问题。'
    W '里面没有账号、密码、存档；但路径里会带你的 Windows 用户名和游戏安装位置。'
    W ''
    W ('生成时间：'+(Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
    W ('游戏主程序：'+$Exe)
    try{$ei=Get-Item -LiteralPath $Exe;W ('    '+$ei.Length+' 字节，改动时间 '+$ei.LastWriteTime.ToString('yyyy-MM-dd HH:mm'))}catch{}
    try{foreach($g in @((Get-033GpuFacts).Adapters)){W ('显卡：'+$g)}}catch{}   # 2026-09-13: 以前读 .Names(不存在), 严格模式抛错被吞, 显卡名一直印不出来
    try{foreach($x in @(Get-CimInstance Win32_VideoController -ErrorAction Stop|Select-Object -First 4)){W ('显卡驱动：'+$x.Name+'   '+$x.DriverVersion+'   '+$x.DriverDate)}}catch{}
    $winver='';try{$winver=[string](Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop).DisplayVersion}catch{}
    W ('Windows：'+[Environment]::OSVersion.VersionString+'   '+$winver)
    W ('PowerShell：'+$PSVersionTable.PSVersion+'   语言模式：'+$ExecutionContext.SessionState.LanguageMode)
    $admin='未知';try{$admin=[string][bool](Test-033IsAdmin)}catch{}
    W ('管理员身份运行：'+$admin)
    try{
        $pj=Join-Path $PackageRoot '033-package.json'
        if(Test-Path -LiteralPath $pj){W ('安装包版本：'+[string](Read-033ManagedJson $pj).Version)}
        $fp=Join-Path $PackageRoot '工具\署名\文件指纹.txt'
        if(Test-Path -LiteralPath $fp){W ('包指纹清单：'+(Get-FileHash -LiteralPath $fp -Algorithm SHA256).Hash)}
    }catch{}

    Section '体检结论'
    W ([string]$CheckupText).TrimEnd()

    Section '游戏目录里有什么'
    foreach($scope in @(@{T='根目录（只列文件）';P=$dir;R=$false},@{T='033-runtime（含子目录）';P=(Join-Path $dir '033-runtime');R=$true})){
        W ''
        W ('-- '+$scope.T+' --')
        if(-not (Test-Path -LiteralPath $scope.P -PathType Container)){W '    （没有这个目录）';continue}
        $items=@()
        try{$items=@(Get-ChildItem -LiteralPath $scope.P -File -Force -Recurse:$scope.R -ErrorAction Stop|Select-Object -First 300)}catch{}
        foreach($f in $items){
            $v='';try{$fv=[Diagnostics.FileVersionInfo]::GetVersionInfo($f.FullName);if($fv.FileVersion){$v='   v'+$fv.FileVersion}}catch{}
            W ('    '+$f.FullName.Substring($scope.P.Length).TrimStart([char]92)+'   '+$f.Length+' 字节   '+$f.LastWriteTime.ToString('yyyy-MM-dd HH:mm')+$v)
        }
        if($items.Count -ge 300){W '    （只列了前 300 个）'}
    }

    Section '所有游戏共用的那份设置 settings.ini'
    $gs=Get-033DiagTail (Join-Path $env:LOCALAPPDATA '033Runtime\settings.ini') 200
    if($gs){W $gs.Text}else{W '（没有这个文件）'}

    Section '安装账本：这次放了哪些文件、原件备份在不在'
    try{
        $idx=Get-033ManagedIndex $Vault $Exe
        if(-not (Test-Path -LiteralPath $idx)){W '（这个游戏没有本安装器的安装记录）'}
        else{
            $gid=[string](Read-033ManagedJson $idx).Group
            $gf=Get-033Path $Vault ('groups/'+$gid)
            $st=Join-Path $gf 'state.json'
            if(-not (Test-Path -LiteralPath $st)){W ('（记录目录里没有 state.json：'+$gf+'）')}
            else{
                $state=Read-033ManagedJson $st
                W ('状态：'+[string]$state.Status+'   包版本：'+[string]$state.Version)
                W ('记录目录：'+$gf)
                foreach($t in @($state.Targets)){
                    W ('入口：'+[string]$t.Exe+'   路线：'+[string]$t.Profile+$(if($t.Mount){'   挂载点：'+[string]$t.Mount}else{''}))
                }
                W ('文件条目：'+@($state.Entries).Count+' 个')
                foreach($e in @($state.Entries)){
                    $orig='装之前没有这个文件'
                    if($e.Original.Exists){$orig=$(if($e.OriginalBlob){'有原件备份'}else{'★原件备份缺失'})}
                    W ('    '+[string]$e.Path+'   '+$orig)
                }
            }
            $pend=Join-Path $gf 'pending.json'
            if(Test-Path -LiteralPath $pend){$pendStatus=[string](Read-033ManagedJson $pend).Status;if($pendStatus -notin @('committed','recovered')){W ('未完成事务：有，状态 '+$pendStatus)}else{W ('上次事务：已完成（'+$pendStatus+'）')}}
        }
    }catch{W ('（读安装账本时出错：'+$_.Exception.Message+'）')}

    Section '日志（核心/RF/nrscale 取最后 400 行，其余 200 行）'
    $logs=[ordered]@{}
    foreach($n in @('ReShade.log','dlss5-033.log','dlss5-033-nrscale.log','033-framegen.log','033-gpu-fault.log','033-present-status.log','OptiScaler.log','dlss5-feed.log','re2_framework_log.txt','reframework\log.txt')){
        $logs[$n]=Join-Path $dir $n
    }
    foreach($sub in @('033-runtime','033-runtime\host64')){
        foreach($n in @('dlss5-033.log','dlss5-033-nrscale.log','dlss5-feed.log','dlss5-feed-host.log','ReShade.log')){
            $logs[($sub+'\'+$n)]=Join-Path $dir ($sub+'\'+$n)
        }
    }
    $any=$false
    foreach($k in @($logs.Keys)){
        $t=Get-033DiagTail $logs[$k] $(if($k -match 'dlss5-033\.log|re2_framework_log|nrscale'){400}else{200})
        if(-not $t){continue}
        $any=$true
        W ''
        W ('-- '+$k+'   共 '+$t.Total+' 行 / '+$t.Size+' 字节   最后写于 '+$t.Written+' --')
        W $t.Text
    }
    if(-not $any){W '（一个日志都没有：多半是游戏还没跑过，或者 033 根本没被加载）'}

    Section '033 的设置文件'
    foreach($n in @('dlss5-033.cfg','dlssg_to_fsr3.ini','ReShade.ini')){
        $t=Get-033DiagTail (Join-Path $dir $n) 120
        if(-not $t){continue}
        W ''
        W ('-- '+$n+' --')
        W $t.Text
    }

    Section 'Windows 记下的崩溃事件（最近 7 天）'
    try{
        $ev=@(Get-WinEvent -FilterHashtable @{LogName='Application';ProviderName=@('Application Error','Windows Error Reporting','.NET Runtime');StartTime=(Get-Date).AddDays(-7)} -MaxEvents 150 -ErrorAction Stop |
            Where-Object {$_.Message -match [regex]::Escape($exeName)} | Select-Object -First 10)
        if(-not $ev.Count){W '（最近 7 天没有这个程序的崩溃事件）'}
        foreach($e in $ev){
            W ''
            W ('---- '+$e.TimeCreated.ToString('yyyy-MM-dd HH:mm:ss')+'   '+$e.ProviderName+' ----')
            W ([string]$e.Message).TrimEnd()
        }
    }catch{W ('（读事件日志失败：'+$_.Exception.Message+'）')}

    W ''
    W '================ 报告结束 ================'

    $safe=(($exeName -replace '\.exe$','') -replace '[\\/:*?"<>|]','_')
    $name='033诊断-'+$safe+'-'+(Get-Date).ToString('yyyyMMdd-HHmmss')+'.txt'
    foreach($base in @([Environment]::GetFolderPath('Desktop'),$Vault,[IO.Path]::GetTempPath())){
        if(-not $base -or -not (Test-Path -LiteralPath $base -PathType Container)){continue}
        try{
            $p=Join-Path $base $name
            [IO.File]::WriteAllText($p,$s.ToString(),[Text.UTF8Encoding]::new($true))
            return $p
        }catch{continue}
    }
    return $null
}
