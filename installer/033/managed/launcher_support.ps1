function Enable-DpiAware {
    if((Get-Variable dpiDone -Scope Script -ErrorAction SilentlyContinue) -and $script:dpiDone){ return }
    $script:dpiDone = $true
    try {
        Add-Type -Namespace W -Name Dpi -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
'@ -EA Stop
        # -4 = PER_MONITOR_AWARE_V2 (Win10 1703+); 老系统退回进程级
        if(-not [W.Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4))){ [void][W.Dpi]::SetProcessDPIAware() }
    } catch { try { [void][W.Dpi]::SetProcessDPIAware() } catch {} }
}

function Set-ConsoleVisible([bool]$show){
    try {
        if(-not ('W.Con' -as [type])){
            Add-Type -Namespace W -Name Con -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
[DllImport("user32.dll")]   public static extern bool ShowWindow(IntPtr h, int n);
'@
        }
        $h = [W.Con]::GetConsoleWindow()
        if($h -ne [IntPtr]::Zero){ [void][W.Con]::ShowWindow($h, $(if($show){ 5 } else { 0 })) }   # 5=SHOW 0=HIDE
    } catch {}
}

# 2026-09-11 评论区（FTL，业主自己也碰到）：侦察、验真要跑一阵子，等急了按下的回车攒在输入缓冲里，菜单一问就被吃掉，
# 还没选就退出了。问之前先清掉攒下的按键；空回车不算选择，再问一次；输错了说清楚再问。
function Clear-033PendingKeys{try{$Host.UI.RawUI.FlushInputBuffer()}catch{}}
function Read-033Choice([string]$Prompt,[int]$Max){
    Clear-033PendingKeys
    for($round=0;$round -lt 5;$round++){
        $answer=Read-Host $Prompt
        if($null -eq $answer){return -1}
        $text=([string]$answer).Trim()
        if(-not $text){continue}
        $n=0
        if([int]::TryParse($text,[ref]$n) -and $n -ge 0 -and $n -le $Max){return $n}
        Write-Host ('请输入列表里的序号（0 到 '+$Max+'）。') -ForegroundColor Yellow
    }
    return -1
}

# V3.4 manual route (owner 2026-09-11: 装不上的弄个手动模式): when automatic matching refuses a game, offer the package's
# routes of the same architecture in plain words; the person picks one or leaves. Nothing here writes or installs.
function Get-033RouteLabel($Route){
    $s=$(if($Route.ContainsKey('Suitability') -and $Route.Suitability){$Route.Suitability}else{@{}})
    $bits=$(if($Route.Architecture -eq 'x86'){'32 位'}else{'64 位'})
    $names=@{ddraw='DirectDraw/D3D7';glide='Glide(3dfx)';dx8='DX8';dx9='DX9';dx10='DX10';dx11='DX11';dx12='DX12';opengl='OpenGL';vulkan='Vulkan'}
    # 包清单读出来是严格模式下的字典，缺的键不能直接点出来（2026-09-11 测试里没写 Dx12Runtime 的路线在这里抛了）。
    $val={param($k) if($s.ContainsKey($k)){$s[$k]}else{$null}}
    $apis=@(@(& $val 'Apis')|Where-Object {$_ -and $names.ContainsKey([string]$_)}|ForEach-Object {$names[[string]$_]})
    $re=($s.ContainsKey('EngineMarkers') -and (@($s.EngineMarkers) -contains 're_chunk_000.pak'))
    $text=$bits+' '+$(if($re){'RE 引擎'}elseif($apis.Count){$apis -join '/'}else{'其它'})
    $text+=$(switch([string](& $val 'NativeUpscaler')){'present'{'（游戏自带 DLSS'+$(if([string](& $val 'Dx12Runtime') -eq 'present'){'，实际跑 D3D12'}else{''})+'）'} 'absent'{'（游戏不带 DLSS）'} default{''}})
    if(-not $re -and $Route.Architecture -eq 'x64' -and [string](& $val 'ProductLine') -eq 'legacy' -and ($apis -contains 'DX11')){$text+='  ← Unity 游戏一般选这个'}
    return $text
}
function Get-033ManualRouteChoices($Look,[string]$PackageRoot){
    $survey=$Look.Survey
    if(-not $survey -or [string]$survey.Exe.Architecture -notin @('x86','x64') -or $survey.Exe.IsDll){return @()}
    # 2026-09-11 回退到 5.0：旧版残留、找不到 NGX 运行库都只是提醒，手动菜单照样给。
    $package=Read-033ManagedPackage $PackageRoot
    # A route that names its game's executable (燕云 yysls.exe) is only offered for that executable, and says so.
    # 2026-09-11 评论区（仁王1 给了 8 条，RE 引擎、游戏自带 DLSS 的都在里面）：引擎标记文件不在的路线不列；目录和程序里都
    # 没有 DLSS 迹象时，「游戏自带 DLSS」的路线也不列。接口对得上的排前面并标出来。
    $markers=@();if($survey.PSObject.Properties['Engine'] -and $survey.Engine){$markers=@($survey.Engine.Markers)}
    $hasDlss=$false;if($survey.PSObject.Properties['NativeDlss'] -and $survey.NativeDlss){$hasDlss=[bool]($survey.NativeDlss.Present -or $survey.NativeDlss.BinaryUsesNgx)}
    # 确认在用自带 DLSS（文件在、程序里也有 NGX 字样，或 Streamline 文件在）：和选路用的是同一个判据。
    $realDlss=$false;if($survey.PSObject.Properties['NativeDlss'] -and $survey.NativeDlss -and $survey.NativeDlss.PSObject.Properties['EffectivePresent']){$realDlss=[bool]$survey.NativeDlss.EffectivePresent}
    $apis=@();if($survey.PSObject.Properties['EffectiveApis']){$apis=@($survey.EffectiveApis|ForEach-Object {[string]$_})}
    if(@($apis|Where-Object {$_ -in @('dx10','dx11','dx12')}).Count){$apis=@($apis|Where-Object {$_ -notin @('dx9','dx8','ddraw')})}
    $rows=@();$ownRows=@()
    foreach($route in @($package.Data.Profiles)){
        if($route.Architecture -ne $survey.Exe.Architecture){continue}
        if($route.ContainsKey('ExecutableNames') -and @($route.ExecutableNames).Count -and @($route.ExecutableNames) -inotcontains [string]$survey.Exe.Name){continue}
        $s=$(if($route.ContainsKey('Suitability') -and $route.Suitability){$route.Suitability}else{@{}})
        if($s.ContainsKey('EngineMarkers') -and @(@($s.EngineMarkers)|Where-Object {$markers -notcontains $_}).Count){continue}
        if($s.ContainsKey('NativeUpscaler') -and [string]$s.NativeUpscaler -eq 'present' -and -not $hasDlss){continue}
        $fits=$($s.ContainsKey('Apis') -and [bool]@(@($s.Apis)|Where-Object {$apis -contains [string]$_}).Count)
        $label=(Get-033RouteLabel $route)+$(if($route.ContainsKey('ExecutableNames') -and @($route.ExecutableNames).Count){'（专用：'+(@($route.ExecutableNames) -join '、')+'）'}else{''})+$(if($fits){'  ← 接口对得上'}else{''})
        $row=[pscustomobject]@{Id=[string]$route.Id;Label=$label;Fits=$fits}
        # 2026-09-12 业主「游戏自己带 DLSS，为什么会弹出要我们选 033 自带 DLSS 的」：确认在用自带 DLSS 的游戏，不列 033 自带 DLSS 的路线
        # （两套 DLSS 会打架闪退，审判之眼）。同位数里一条别的都没有时才列出来，免得菜单空着。
        if($realDlss -and $s.ContainsKey('NativeUpscaler') -and [string]$s.NativeUpscaler -eq 'absent'){$ownRows+=@($row)}else{$rows+=@($row)}
    }
    if(-not $rows.Count){$rows=$ownRows}
    return @(@($rows|Where-Object {$_.Fits})+@($rows|Where-Object {-not $_.Fits}))
}
function Select-033ManualRoute($Look,[string]$PackageRoot){
    $choices=@(Get-033ManualRouteChoices $Look $PackageRoot)
    if(-not $choices.Count){return $null}
    Write-Host ''
    Write-Host '可以手动选一条路线试试（只列跟这个游戏对得上的，排在前面的先试）：' -ForegroundColor Cyan
    for($i=0;$i -lt $choices.Count;$i++){Write-Host ('  '+($i+1)+'. '+$choices[$i].Label+'   ['+$choices[$i].Id+']')}
    Write-Host '  0. 不装，退出'
    Write-Host '手动模式照样先备份原件、记账本，卸载能原样还原；选错了顶多没效果或进不去游戏，用安装器卸载就行。' -ForegroundColor DarkGray
    $n=Read-033Choice '输入序号' $choices.Count
    if($n -lt 1){return $null}
    return $choices[$n-1].Id
}
