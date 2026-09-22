# 033 installer v3: survey first, judge second, then one journalled transaction.
# No legacy destructive fallback; refusals explain the way out in plain language.
[CmdletBinding()]
param([string]$GameExe,[string[]]$AdditionalGameExe=@(),
      [ValidateSet('Install','Plan','Restore','Recover','Import','Survey','Checkup')][string]$Action='Install',
      [string]$PackageRoot=$PSScriptRoot,[string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer'),
      [string]$Profile,[string[]]$Receipts=@(),[string[]]$ReceiptHashes=@(),
      [switch]$NoSplash,[switch]$Silent,[switch]$DryRun,[switch]$ForceFeeder,
      [switch]$AllowNativeDlss,[switch]$HandoverOld,[string]$Proxy,[switch]$Manual,[switch]$NoElevate,[string]$ProgressFile,[switch]$Clean)
$ErrorActionPreference='Stop'
# 2026-09-12 评论区（百度下载目录）：「Cannot dot-source this command because it was defined in a different language mode」。
# 系统把 PowerShell 限制在受限语言模式时，安装器用到的 .NET 调用和点加载都跑不了；在任何 .NET 调用之前先说清楚。
$K033ClmText='这台电脑的 PowerShell 被系统限制在「受限语言模式」（ConstrainedLanguage），安装器没法在这种模式下运行，游戏目录一个文件都没动。常见原因：① 系统环境变量里有 __PSLockdownPolicy（部分安全 / 优化软件会加）——打开「系统属性 → 高级 → 环境变量」，在系统变量里删掉它，重启电脑后再运行；② 公司电脑的 AppLocker / WDAC 安全策略——只能请电脑管理员放行 PowerShell 脚本。'
if($ExecutionContext.SessionState.LanguageMode -ne 'FullLanguage'){
    Write-Host ''
    Write-Host ('未完成：'+$K033ClmText+' (033_CONSTRAINED_LANGUAGE)') -ForegroundColor Red
    if(-not $Silent){try{Read-Host '按回车退出'|Out-Null}catch{}}
    exit 3
}
# Dedicated YanYun graphical worker. Progress never changes transaction decisions.
function Publish-033Progress([string]$Stage,[int]$Done=0,[int]$Total=0,[string]$Detail=''){
 if(!$ProgressFile){return}
 try{
  $data=@{Stage=$Stage;Done=$Done;Total=$Total;Detail=$Detail;At=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json -Compress
  [IO.File]::WriteAllText(($ProgressFile+'.tmp'),$data,[Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath ($ProgressFile+'.tmp') -Destination $ProgressFile -Force
 }catch{Write-Verbose ('Progress unavailable: '+$_.Exception.Message)}
}
function Complete-033Progress([bool]$Succeeded,[string]$Message,$Result){
 if(!$ProgressFile){return}
 try{[IO.File]::WriteAllText(($ProgressFile+'.result.json'),(@{Succeeded=$Succeeded;Message=$Message;Result=$Result}|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}catch{}
 Publish-033Progress $(if($Succeeded){'complete'}else{'failed'}) 0 0 $Message
}
# Windows PowerShell 5.1 finds Get-FileHash & co. through PSModulePath; a parent
# process (PowerShell 7 hosts, test harnesses, odd launchers) can leave it stripped.
if($PSVersionTable.PSVersion.Major -le 5){
    # A PowerShell 7 parent leaves its own module directories first; 5.1 then
    # loads the wrong Microsoft.PowerShell.Utility and Get-FileHash vanishes.
    $sysModules=[IO.Path]::Combine($env:SystemRoot,'System32','WindowsPowerShell','v1.0','Modules')
    $keep=@(($env:PSModulePath -split ';')|Where-Object {$_ -and ($_ -ine $sysModules) -and ($_ -notmatch '(?i)\\PowerShell\\(7|Modules)') -and ($_ -notmatch '(?i)pwsh')})
    $env:PSModulePath=(@($sysModules)+$keep) -join ';'
    Import-Module Microsoft.PowerShell.Utility -Force -ErrorAction SilentlyContinue
}
if([string]::IsNullOrWhiteSpace($PackageRoot)){$PackageRoot=$PSScriptRoot}
function Get-033LauncherAction([string]$Action) {
    switch($Action) {
        'install' { return @{ action='install'; force=$false } }
        'restore' { return @{ action='uninstall'; force=$false } }
        'force' { return @{ action='install'; force=$true } }
        default { return @{ action='exit'; force=$false } }
    }
}
# 5.0 的写入权限预检（dlss5_install.ps1 v5.0 821-856）：装在 Program Files 之类受保护目录的游戏，普通权限写不进去，
# 就向 Windows 申请管理员权限、带同样的参数在新窗口里重跑。已经是管理员还写不进去才停。
function Test-033GameDirWritable([string]$Dir){
    try{$p=Join-Path $Dir ('.033-write-test-'+[Guid]::NewGuid().ToString('N')+'.tmp');[IO.File]::WriteAllText($p,'x');[IO.File]::Delete($p);return $true}catch{return $false}
}
function Test-033IsAdmin{
    try{return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}catch{return $false}
}
function Invoke-033Elevated([string[]]$ArgumentList){
    $ps=[IO.Path]::Combine($env:SystemRoot,'System32','WindowsPowerShell','v1.0','powershell.exe');if(-not [IO.File]::Exists($ps)){$ps='powershell.exe'}
    Start-Process -FilePath $ps -Verb RunAs -ArgumentList $ArgumentList|Out-Null
}
function Wait-033Exit{
    # 等急了按下的回车会攒在输入缓冲里，一问「按回车退出」就被吃掉，窗口一闪就关、结果没看到（2026-09-11 评论区）。
    try{$Host.UI.RawUI.FlushInputBuffer()}catch{}
    Read-Host '按回车退出'|Out-Null
}
function Confirm-033([string]$Question){
    if($Silent){return $false}
    try{$Host.UI.RawUI.FlushInputBuffer()}catch{}
    for($round=0;$round -lt 5;$round++){
        $answer=Read-Host ($Question+' [Y/N]')
        if($null -eq $answer){return $false}
        $text=([string]$answer).Trim()
        if($text -match '^(y|yes|是)$'){return $true}
        if($text -match '^(n|no|否)$'){return $false}
        if($text){Write-Host '请输入 Y 或 N。' -ForegroundColor Yellow}
    }
    return $false
}
function Select-033ProxyMount([string]$Exe){
    # 换挂载点（v5.0 的「换挂载方式.cmd」，2026-09-11 评论区要回来）：装好了但按 Home 没反应，多半是游戏压根不加载
    # 我们挑的那个 DLL 名。列出 ReShade 真能顶替的系统 DLL 名、标出现在用的；选好后照常走安装或更新，原件照样备份。
    if($Silent){throw '换挂载点需要在窗口里选，不能静默运行'}
    $current=$null
    try{
        $g=Read-033ManagedGroup $Vault $Exe
        if($g -and $g.State -and $g.State.Status -eq 'installed'){
            $t=@($g.State.Targets|Where-Object {$_.Exe -ieq $Exe})|Select-Object -First 1
            if($t){$current=$(if($t.ContainsKey('Proxy') -and $t.Proxy){[string]$t.Proxy}elseif($t.ContainsKey('Mount') -and $t.Mount){[string]$t.Mount}else{$null})}
        }
    }catch{}
    # 2026-09-13：能换成哪些名字，取决于【这个游戏会走哪条路线】，因为不同路线的入口文件不一样。
    #   ReShade 入口 → ReShade 能顶替的那 11 个；Vulkan 路线入口是 033 核心本体 → 核心 dllmain
    #   认得的那 7 个。拿错名单会把玩家引到一个挂上去就不转发原版的名字上。
    $names=@($K033ProxyNames);$pkgForNames=$null;$routeForNames=$null;$entryForNames=$null   # StrictMode: the try below may fail before assigning these
    try{
        $pkgForNames=Read-033ManagedPackage $PackageRoot
        $routeForNames=Select-033ManagedProfile $pkgForNames $Exe
        $entryForNames=Get-033RouteEntry $routeForNames
        if($entryForNames -and @($entryForNames.Names).Count){$names=@($entryForNames.Names)}
    }catch{}
    # 2026-09-17 Fable（找回 5.0 的四档证据）：菜单按「这个游戏到底会不会加载这个名字」排序并标出来，
    #   导入表里有的排最前（一定会加载），二进制里出现过的其次（可能运行时加载），没证据的垫底；
    #   我们的入口文件顶不住的名字（游戏从它导入的函数导不出）直接标红、不许选 —— 挂上去游戏起不来。
    $proofForNames=$null;try{$proofForNames=Get-033MountEvidence $Exe}catch{$proofForNames=$null}
    $exportsForNames=$null;$importsForNames=$null
    try{if($entryForNames){$exportsForNames=Get-033PeExportNames (Get-033Path (Get-033PackageDataRoot $PackageRoot) ([string]$entryForNames.File.Source))}}catch{$exportsForNames=$null}
    try{$importsForNames=Get-033PeImportedFunctions $Exe}catch{$importsForNames=$null}
    $rankForNames=@{};$standForNames=@{}
    foreach($name in $names){
        $lv=$(if($proofForNames){[string]$proofForNames.Evidence[$name.ToLowerInvariant()]}else{'none'})
        $rankForNames[$name]=Get-033MountEvidenceRank $lv
        $standForNames[$name]=$(if($null -ne $exportsForNames -and $null -ne $importsForNames){Test-033ProxyCanStandIn $name $importsForNames $exportsForNames}else{$null})
    }
    $orderForNames=@($names)
    $names=@($names|Sort-Object @{Expression={-[int]$rankForNames[$_]}},@{Expression={[array]::IndexOf($orderForNames,$_)}})
    $hint=@{'dxgi.dll'='大多数 DX10/11/12 游戏（默认）';'winmm.dll'='Vulkan 游戏常用（多媒体计时）';'version.dll'='几乎所有游戏都会加载';'dbghelp.dll'='不少游戏的崩溃报告会用';'winhttp.dll'='联网的游戏会用';'wininet.dll'='联网的游戏会用';'d3d11.dll'='DX11 游戏';'d3d12.dll'='DX12 游戏';'d3d9.dll'='DX9 游戏';'d3d10.dll'='DX10 游戏';'d3d10_1.dll'='DX10.1 游戏';
            'ReShade64.dll'='只在 OptiScaler 占着 dxgi 时用：由 OptiScaler 按官方协议加载（LoadReshade=true）';'ReShade32.dll'='同上，32 位';
            'dinput8.dll'='几乎所有游戏都会加载（RE 引擎游戏的适配器占着它，那类游戏别选）';'dinput.dll'='老游戏';'ddraw.dll'='很老的游戏';'d2d1.dll'='少数用 Direct2D 的游戏';'opengl32.dll'='OpenGL 游戏'}
    Write-Host ''
    Write-Host '换挂载点：装好了但进游戏按 Home 没反应时用。游戏必须真的会加载这个名字，033 才进得去。' -ForegroundColor Cyan
    if($current){Write-Host ('现在用的是：'+$current) -ForegroundColor Gray}else{Write-Host '这个游戏还没用新安装器装过；选好后会直接按这个挂载点安装。' -ForegroundColor Gray}
    for($i=0;$i -lt $names.Count;$i++){
        $name=$names[$i]
        $ev=$(switch([int]$rankForNames[$name]){2{'✔ 导入表里有，游戏一定会加载'} 1{'△ 二进制里出现过，可能运行时加载'} default{'－ 没有证据，多半不会加载'}})
        $st=$standForNames[$name];$bad=($null -ne $st -and -not $st.Ok)
        Write-Host ('  '+($i+1)+'. '+$name+'  '+$ev+'  '+[string]$hint[$name]+$(if($current -and $current -ieq $name){'   ← 现在用的'}else{''})+$(if($bad){'   ✖ 顶不住：游戏从它导入 '+$st.Needed+' 个函数，我们的入口文件导不出，挂上去游戏起不来'}else{''})) -ForegroundColor $(if($bad){'Red'}elseif([int]$rankForNames[$name] -ge 2){'Green'}elseif([int]$rankForNames[$name] -eq 1){'Yellow'}else{'DarkGray'})
    }
    Write-Host '  0. 不换，退出'
    Write-Host '选了游戏不加载的名字，顶多还是没反应，再换一个就行；卸载照样原样还原。' -ForegroundColor DarkGray
    $n=Read-033Choice '输入序号' $names.Count
    if($n -lt 1){return $null}
    $pickedName=$names[$n-1];$pickedStand=$standForNames[$pickedName]
    if($null -ne $pickedStand -and -not $pickedStand.Ok){throw ('不能挂成 '+$pickedName+'：这个游戏从它导入 '+$pickedStand.Needed+' 个函数（'+((@($pickedStand.Missing)|Select-Object -First 4) -join '、')+'），033 的入口文件导不出这些名字，挂上去游戏会直接起不来（0xc000007b）。没有改任何文件，换一个有证据的名字。')}
    return $pickedName
}
function Select-033GameExeDialog([string]$Title='选择游戏主程序'){
    # 2026-09-12：033体检.cmd 以前双击只会弹一行英文用法，而 Windows 又不允许往控制台窗口里拖文件，
    # 于是「体检」这个我们最需要玩家用的功能，实际上没人用得起来。双击直接弹选文件框。
    try{
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $d=[Windows.Forms.OpenFileDialog]::new()
        $d.Title=$Title;$d.Filter='游戏主程序 (*.exe)|*.exe|所有文件 (*.*)|*.*';$d.CheckFileExists=$true
        if($d.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){return [string]$d.FileName}
    }catch{}
    return $null
}
function Test-033PackageFingerprint([string]$Root){
    # 验真: the same tool (dlss5_verify.ps1) and manifest (工具\署名\文件指纹.txt) as the
    # released one-click packages. A package that cannot show it is the published one is
    # not installed: tool or manifest missing, or any listed file altered or missing.
    $Root=Get-033PackageDataRoot $Root
    $distributionRoot=Get-033PackageDistributionRoot $Root
    $verify=[IO.Path]::Combine($Root,'dlss5_verify.ps1');$manifest=[IO.Path]::Combine($Root,'工具','署名','文件指纹.txt')
    if(-not [IO.File]::Exists($verify) -or -not [IO.File]::Exists($manifest)){
        return [pscustomobject]@{Passed=$false;Status='missing';Files=0;ManifestSha256=$null;Detail='包里缺少验真工具（dlss5_verify.ps1）或文件指纹清单（工具\署名\文件指纹.txt），没法确认这是原版'}
    }
    $psExe=[IO.Path]::Combine($env:SystemRoot,'System32','WindowsPowerShell','v1.0','powershell.exe');if(-not [IO.File]::Exists($psExe)){$psExe='powershell.exe'}
    & $psExe -NoProfile -ExecutionPolicy Bypass -File $verify -Quiet | Out-Null
    $code=$LASTEXITCODE
    $want=@{}
    foreach($line in [IO.File]::ReadAllLines($manifest,[Text.Encoding]::UTF8)){
        $t=$line.Trim();if($t.Length -eq 0 -or $t.StartsWith('#')){continue}
        $sp=$t.IndexOf('  ');if($sp -lt 64){continue}
        $want[$t.Substring($sp+2).Trim()]=$t.Substring(0,$sp).ToUpperInvariant()
    }
    $problems=[Collections.Generic.List[string]]::new()
    if($code -ne 0){
        foreach($rel in @($want.Keys)){
            $full=[IO.Path]::Combine($distributionRoot,$rel)
            if(-not [IO.File]::Exists($full)){$problems.Add('缺失 '+$rel)}
            elseif((Get-033FileHash $full).ToUpperInvariant() -ne $want[$rel]){$problems.Add('内容不对 '+$rel)}
            if($problems.Count -ge 6){break}
        }
    }
    [pscustomobject]@{Passed=($code -eq 0);Status=$(if($code -eq 0){'passed'}else{'failed'});Files=$want.Count;
        ManifestSha256=(Get-033FileHash $manifest).ToUpperInvariant();
        Detail=$(if($code -eq 0){'全部 '+$want.Count+' 个文件对上指纹'}elseif($problems.Count){'文件指纹对不上：'+($problems -join '、')}else{'验真工具报告对不上（退出码 '+$code+'）'})}
}
try {
    try{[Console]::OutputEncoding=[Text.Encoding]::UTF8}catch{}
    . (Join-Path $PSScriptRoot 'managed/managed_transaction.ps1')
    . (Join-Path $PSScriptRoot 'managed/import_history.ps1')
    . (Join-Path $PSScriptRoot 'managed/launcher_support.ps1')
    . (Join-Path $PSScriptRoot '033_launcher_ui.ps1')
    # 只在 Install/Plan/Survey 的那一段里才被赋值，但下面的写入权限预检对 Restore 也会读它们。
    # managed/deployment_transaction.ps1 开着 Set-StrictMode -Version Latest，未赋值就引用会直接抛异常：
    # 2026-09-12 已发布包的「一键恢复」对所有人必崩（「检索不到变量"$look"」，dlss5_install.ps1 第 218 行）。
    $finger=$null;$look=$null;$K033FgFixNote=$null
    if(-not $NoSplash -and -not $DryRun -and $Action -eq 'Install'){
        $act=Show-Launcher $GameExe
        if($act.action -eq 'exit'){return}
        if($act.gameExe){ $GameExe = $act.gameExe }
        if($act.force){ $ForceFeeder = $true; }
        if($act.action -eq 'uninstall'){$Action='Restore'}
    }
    if(-not $GameExe -and -not $Silent -and ($Action -eq 'Checkup' -or $Proxy -eq 'ASK')){
        # 体检 和 换挂载点 这两个入口以前双击都只弹一行英文用法，而 Windows 不允许往控制台窗口里拖文件，
        # 于是玩家要么「没反应」要么把 .cmd 拖到游戏上（反而启动了游戏）。双击直接弹选文件框，
        # 选完就进各自的流程 —— 换挂载点直接给挂载名菜单，不绕安装器窗口。
        $what=$(if($Action -eq 'Checkup'){'要体检的'}else{'要换挂载点的'})
        Write-Host ('请选择'+$what+'游戏主程序（.exe）。') -ForegroundColor Cyan
        $GameExe=Select-033GameExeDialog ('选择'+$what+'游戏主程序')
        if(-not $GameExe){Write-Host '没有选择游戏，什么都没做。' -ForegroundColor Gray;Wait-033Exit;exit 0}
    }
    if(-not $GameExe){throw '请先在安装器中选择燕云主程序'}
    if($ForceFeeder -or $Manual -or $Proxy -or ($Profile -and $Profile -ne 'yanyun-exclusive-x64')){throw '燕云专版自动选择所需组件，不接受其他游戏路线或强制挂载选项'}
    # 2026-09-12 换挂载点.cmd 不再带 -NoSplash（双击只弹英文命令行、拖不进去，评论区四个人报「没反应」），
    # 于是它现在也会先开安装器那个窗口 —— 玩家在窗口里可能点的是「一键恢复」。恢复不需要挂载名，别问。
    if($Proxy -eq 'ASK' -and $Action -ne 'Install'){$Proxy=$null}
    if($Proxy -eq 'ASK'){
        # 换挂载点.cmd 从这里进：先定游戏，再问挂载名，然后照常安装或更新。
        if(Test-Path -LiteralPath $GameExe -PathType Container){$GameExe=(Resolve-033GameExe $GameExe $Vault).Exe}
        $Proxy=Select-033ProxyMount ([IO.Path]::GetFullPath($GameExe))
        if(-not $Proxy){Write-Host '没换，未改任何文件。' -ForegroundColor Gray;if(-not $Silent){Wait-033Exit};exit 0}
    }
    # 业主 2026-09-12：「安装器给他提权吧，默认就是最高权限」。游戏选好之后（选游戏的窗口要能把游戏拖进去，提权后的窗口 Windows 不让从资源管理器拖）
    # 就向 Windows 申请管理员，带同样的参数在新窗口里接着装；玩家点了「否」就按普通权限继续，写不进去时下面那道检查再说怎么办。
    if(-not $Silent -and -not $DryRun -and -not $NoElevate -and $Action -in @('Install','Restore') -and -not @($AdditionalGameExe|Where-Object {$_}).Count -and -not (Test-033IsAdmin)){
        $elevExe=[string]$GameExe;try{$elevExe=[IO.Path]::GetFullPath($GameExe)}catch{}
        $elevArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-GameExe',('"'+$elevExe.TrimEnd([char]92)+'"'),'-Action',$Action,'-Vault',('"'+$Vault.TrimEnd([char]92)+'"'),'-PackageRoot',('"'+$PackageRoot.TrimEnd([char]92)+'"'),'-NoSplash','-NoElevate')
        if($Profile){$elevArgs+=@('-Profile',$Profile)}
        if($Manual){$elevArgs+='-Manual'}
        if($Clean){$elevArgs+='-Clean'}
        if($ForceFeeder){$elevArgs+='-ForceFeeder'}
        if($AllowNativeDlss){$elevArgs+='-AllowNativeDlss'}
        if($Proxy){$elevArgs+=@('-Proxy',$Proxy)}
        Write-Host '正在申请管理员权限（默认用最高权限装，写游戏目录最稳）——弹出的确认框请点「是」。' -ForegroundColor Yellow
        $elevated=$false
        try{Invoke-033Elevated $elevArgs;$elevated=$true}catch{Write-Host '没拿到管理员权限（确认框点了「否」或被拦了），按普通权限继续。' -ForegroundColor Yellow}
        if($elevated){Write-Host '已在新窗口里以管理员身份继续，这个窗口会自己关掉。' -ForegroundColor Green;exit 0}
    }
    if($DryRun -and $Action -notin @('Import','Survey','Checkup')){$Action='Plan'}
    if($Action -in @('Checkup','Import') -and (Test-Path -LiteralPath $GameExe -PathType Container)){
        # V3.1: a game folder is accepted; Install/Plan/Survey/Restore resolve inside the transaction and report it.
        $resolved=Resolve-033GameExe $GameExe $Vault
        if(-not $Silent){Write-Host (Format-033Resolution $resolved)}
        $GameExe=$resolved.Exe
    }
    if($Action -eq 'Checkup'){
        $check=Get-033Checkup $GameExe;$text=Format-033Checkup $check
        if($Silent){$check|Select-Object Exe,Verdict,Findings,Facts|ConvertTo-Json -Depth 6}else{Write-Host $text}
        try{$reportDir=Join-Path $Vault 'checkups';[void][IO.Directory]::CreateDirectory($reportDir);[IO.File]::WriteAllText((Join-Path $reportDir ((Get-Date).ToString('yyyyMMdd-HHmmss')+'-体检.txt')),$text,[Text.UTF8Encoding]::new($true))}catch{}
        # 2026-09-12 业主：「033 要弄个诊断日志，我们才好知道具体原因吧？不然都不知道」。
        # 体检本来就把所有日志读了一遍，但结论只打在窗口里 —— 玩家只能拍屏幕，作者什么也看不到。
        # 现在顺手把现场汇成【桌面上的一个 txt】，玩家把这个文件发过来就行。出不来也不影响体检本身。
        $diag=$null;try{$diag=Export-033DiagnosticReport $GameExe $Vault $text $PackageRoot}catch{$diag=$null}
        if(-not $Silent){
            if($diag){
                Write-Host ''
                Write-Host '诊断报告已经生成，把这个文件发给作者就能定位问题：' -ForegroundColor Green
                Write-Host ('    '+$diag) -ForegroundColor White
                Write-Host '（里面没有账号密码存档；路径里会带你的用户名和游戏位置，介意可以先打开看看）' -ForegroundColor DarkGray
            }else{
                Write-Host '这次没能生成诊断报告文件（不影响上面的体检结论）。' -ForegroundColor Yellow
            }
        }
        if(-not $Silent){Wait-033Exit}
        exit $(if($check.Verdict -eq '有问题'){2}else{0})
    }
    $targets=@($GameExe)+@($AdditionalGameExe)
    if($Action -eq 'Import'){
        $result=Import-033ManagedHistory -GameExe $targets -Receipts $Receipts -ReceiptHashes $ReceiptHashes -Vault $Vault -PlanOnly:$DryRun
    }else{
        if($Action -in @('Install','Plan','Survey')){
            Publish-033Progress 'verify'
            if(-not $Silent){Write-Host '正在检查燕云安装包…' -ForegroundColor DarkGray}
            $finger=Test-033PackageFingerprint $PackageRoot
            if(!$finger.Passed){throw ('安装包不完整：'+$finger.Detail+' (033_VERIFY_'+$finger.Status.ToUpperInvariant()+')')}
            # The transaction performs one full survey, route decision and ownership check.
            # Do not repeat that work merely to print a generic-game report first.
            if($Action -eq 'Survey'){
                $look=Invoke-033ManagedOperation -Action Survey -GameExe $targets -PackageRoot $PackageRoot -Vault $Vault
                if($Silent){$look|ConvertTo-Json -Depth 8}else{Write-Host $look.Report;Wait-033Exit}
                exit 0
            }
            Publish-033Progress 'check'
        }
        if(-not $Silent -and -not $DryRun -and $Action -in @('Install','Restore')){
            # 5.0 的写入权限预检：写不进游戏目录又不是管理员，就带同样的参数申请管理员权限、在新窗口里重跑。
            $writeExe=$null
            if($look -and $look.Survey){$writeExe=[string]$look.Survey.Exe.Path}
            elseif(Test-Path -LiteralPath $GameExe -PathType Container){try{$writeExe=(Resolve-033GameExe $GameExe $Vault).Exe}catch{}}
            elseif(Test-Path -LiteralPath $GameExe -PathType Leaf){$writeExe=[IO.Path]::GetFullPath($GameExe)}
            if($writeExe -and -not (Test-033GameDirWritable (Split-Path -Parent $writeExe))){
                if(Test-033IsAdmin){throw ('游戏目录写不进去，而且已经是管理员了：'+(Split-Path -Parent $writeExe)+'。可能是目录被设成只读，或杀毒软件在拦；把游戏目录加进杀软信任区后再试。')}
                Write-Host ''
                Write-Host '这个游戏装在受保护的目录里（常见于 C 盘 Program Files 下的游戏），普通权限写不进去。正在向 Windows 申请管理员权限重新运行——弹出的确认框请点「是」。' -ForegroundColor Yellow
                $elevArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-GameExe',('"'+$writeExe+'"'),'-Action',$Action,'-Vault',('"'+$Vault+'"'),'-PackageRoot',('"'+$PackageRoot+'"'),'-NoSplash')
                if($Profile){$elevArgs+=@('-Profile',$Profile)}
                if($Manual){$elevArgs+='-Manual'}
                if($Clean){$elevArgs+='-Clean'}
                if($ForceFeeder){$elevArgs+='-ForceFeeder'}
                if($AllowNativeDlss){$elevArgs+='-AllowNativeDlss'}
                if($Proxy -and $Proxy -ne 'ASK'){$elevArgs+=@('-Proxy',$Proxy)}
                try{Invoke-033Elevated $elevArgs}catch{throw '没拿到管理员权限（确认框点了「否」或被拦了）。手动办法：右键「033安装器.exe」选「以管理员身份运行」，再选这个游戏。'}
                Write-Host '已在新窗口里以管理员身份继续，这个窗口可以关了。' -ForegroundColor Green
                if(-not $Silent){Wait-033Exit}
                exit 0
            }
            Write-Host ''
            Write-Host '正在写入文件，请不要关闭这个窗口（万一中途关了，下次运行会先自动撤销没写完的部分）。' -ForegroundColor Cyan
        }
        $result=Invoke-033ManagedOperation -Action $Action -GameExe $targets -PackageRoot $PackageRoot -Vault $Vault -Profile $Profile -ForceFeeder:$ForceFeeder -AllowNativeDlss:$AllowNativeDlss -HandoverOld:$HandoverOld -Proxy $Proxy -Manual:$Manual -Clean:$Clean
    }
    $completionText=if($result -and $result.PSObject.Properties['Message']){[string]$result.Message}elseif($Action -eq 'Plan'){'安装方案检查完成'}else{'操作完成'}
    Complete-033Progress $true $completionText $result
    # U1 uses the current FG runtime and per-game settings. Do not apply the
    # obsolete pre-runtime global FG reset outside the install transaction.
    if($Silent -or $Action -eq 'Plan'){$result|ConvertTo-Json -Depth 20}
    if($Action -ne 'Plan' -and -not $DryRun){
        Write-Host ''
        Write-Host $result.Message -ForegroundColor Green
        if($result.Route){Write-Host ('路线：'+$result.Route+$(if($result.Manual){'（手动选择；以后更新沿用这条）'}else{''})) -ForegroundColor Gray}
        if(@($result.Mount).Count){Write-Host ('挂载点：'+(@($result.Mount) -join '、')+'（游戏靠这个文件加载 033）') -ForegroundColor Gray}
        foreach($w in @($result.Warnings)){Write-Host ('提醒：'+$w) -ForegroundColor Yellow}
        if($K033FgFixNote){Write-Host ('已顺手修好：'+$K033FgFixNote) -ForegroundColor Green}
        if($result.Handover){Write-Host '旧版一键包已用它自带的卸载器还原，记录在回执里。' -ForegroundColor Gray}
        if($result.Receipt){Write-Host ('回执：'+$result.Receipt) -ForegroundColor DarkGray}
        if($Action -eq 'Install'){Write-Host '安装完成。Home 打开面板，F11 切换 NR，Shift+F9 开关监控。' -ForegroundColor Gray}
        if(-not $Silent){Wait-033Exit}
    }
    exit 0
}catch{
    Write-Verbose $_.ScriptStackTrace
    Write-Host ''
    $failText=$_.Exception.Message
    if(($_.FullyQualifiedErrorId -match 'DotSourceNotSupported') -or ($failText -match '(?i)language mode')){$failText=$K033ClmText+' (033_CONSTRAINED_LANGUAGE)'}
    Complete-033Progress $false $failText $null
    Write-Host ('未完成：'+$failText) -ForegroundColor Red
    if($_.InvocationInfo -and $_.InvocationInfo.ScriptLineNumber){Write-Host ('  位置：'+(Split-Path -Leaf $_.InvocationInfo.ScriptName)+' 第 '+$_.InvocationInfo.ScriptLineNumber+' 行') -ForegroundColor DarkGray}
    Write-Host '没有宣告安装或恢复成功。已有原件、事务与失败信息会保留。' -ForegroundColor Yellow
    if(-not $Silent){try{Wait-033Exit}catch{}}
    exit 1
}
