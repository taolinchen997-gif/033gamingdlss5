# =====================================================================
#  DLSS5 一键包 · 验真                    整理: B站 @热心网友033
#
#  免费的东西最容易被人塞私货再转发。这个脚本把包里每个文件的 SHA256
#  跟「工具\署名\文件指纹.txt」对一遍:
#     全绿 = 你手上这份跟我发出去的一模一样
#     有红 = 被人动过, 别用
#
#  ★整个脚本只用 .NET, 一个需要自动加载的 cmdlet 都不用★
#  实测踩过: 某些机器上 powershell 起来之后 Get-FileHash 直接"找不到命令"
#  (模块自动加载被组策略/杀毒/超长 PSModulePath 卡住)。验真工具要是自己
#  跑不起来, 就等于没有 —— 所以哈希、遍历、路径全走 .NET, 不依赖模块。
# =====================================================================
param([switch]$Quiet)

$dataRoot = [IO.Path]::GetDirectoryName($MyInvocation.MyCommand.Path)
$root = $dataRoot
if([IO.Path]::GetFileName($dataRoot) -ceq '033'){
    $parent=[IO.Path]::GetDirectoryName($dataRoot)
    if($parent -and [IO.File]::Exists([IO.Path]::Combine($parent,'033安装器.exe'))){$root=$parent}
}
$manifest = [IO.Path]::Combine($dataRoot, '工具\署名\文件指纹.txt')

function Say($m, $c = 'Gray'){ if(-not $Quiet){ Write-Host $m -ForegroundColor $c } }

function Sha256([string]$path){
    $sha = [Security.Cryptography.SHA256]::Create()
    $fs  = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try   { return [BitConverter]::ToString($sha.ComputeHash($fs)).Replace('-','') }
    finally { $fs.Dispose(); $sha.Dispose() }
}

if(-not [IO.File]::Exists($manifest)){
    Say ''
    Say '  找不到 工具\署名\文件指纹.txt' Red
    Say '  这个包没完整解压, 或者根本不是原版。' Red
    Say '  去 B站 @热心网友033 那儿重新下载。' Yellow
    if(-not $Quiet){ Write-Host ''; try { Read-Host '按回车关闭' | Out-Null } catch { } }
    exit 2
}

if(-not $Quiet){
    Write-Host ''
    Write-Host '  ╔══════════════════════════════════════════════╗' -ForegroundColor Magenta
    Write-Host '  ║   DLSS5 一键包 · 验真                        ║' -ForegroundColor Magenta
    Write-Host '  ║   整理: B站 @热心网友033                     ║' -ForegroundColor Magenta
    Write-Host '  ╚══════════════════════════════════════════════╝' -ForegroundColor Magenta
    Write-Host ''
}

# ---------- 读清单 ----------
$want = New-Object 'System.Collections.Generic.Dictionary[string,string]'
foreach($line in [IO.File]::ReadAllLines($manifest, [Text.Encoding]::UTF8)){
    $t = $line.Trim()
    if($t.Length -eq 0 -or $t[0] -eq '#'){ continue }
    $sp = $t.IndexOf('  ')
    if($sp -lt 64){ continue }
    $want[$t.Substring($sp + 2).Trim()] = $t.Substring(0, $sp).ToUpper()
}

Say ("  清单里有 " + $want.Count + " 个文件, 正在逐个对指纹...") Gray

# ---------- 逐个对 ----------
$bad = New-Object 'System.Collections.Generic.List[string]'
$missing = New-Object 'System.Collections.Generic.List[string]'
$i = 0
foreach($rel in $want.Keys){
    $i++
    $full = [IO.Path]::Combine($root, $rel)
    if(-not [IO.File]::Exists($full)){ [void]$missing.Add($rel); continue }
    if((Sha256 $full) -ne $want[$rel]){ [void]$bad.Add($rel) }
    if(-not $Quiet -and ($i % 5 -eq 0)){ Write-Host '.' -NoNewline -ForegroundColor DarkGray }
}
if(-not $Quiet){ Write-Host '' }

# ---------- 多出来的文件 ----------
#   不算错(自己解压时会生成临时文件), 但私货就是这么进来的, 要说一声。
$extra = New-Object 'System.Collections.Generic.List[string]'
foreach($f in [IO.Directory]::GetFiles($root, '*', [IO.SearchOption]::AllDirectories)){
    $rel = $f.Substring($root.Length).TrimStart([char]92)
    if($want.ContainsKey($rel)){ continue }
    if([IO.Path]::GetFileName($f) -eq '文件指纹.txt'){ continue }
    [void]$extra.Add($rel)
}

# ---------- 结论 ----------
Say ''
if($bad.Count -eq 0 -and $missing.Count -eq 0){
    Say '  [OK] 全部对上了 —— 这是原版' Green
} else {
    Say '  [!!] 对不上, 这个包被人动过' Red
    foreach($f in $bad)     { Say ("      内容不对: " + $f) Red }
    foreach($f in $missing) { Say ("      文件缺失: " + $f) Red }
    Say ''
    Say '  别用这份。去 B站 @热心网友033 那儿重新下载。' Yellow
}

if($extra.Count -gt 0){
    Say ''
    Say ('  [?] 包里多出 ' + $extra.Count + ' 个原版没有的文件:') Yellow
    $k = 0
    foreach($f in $extra){ if($k -lt 12){ Say ("      " + $f) Yellow }; $k++ }
    if($extra.Count -gt 12){ Say ('      ... 还有 ' + ($extra.Count - 12) + ' 个') Yellow }
    Say '  (自己解压时生成的临时文件可以无视; 不认识的 exe/dll 千万别运行)' Gray
}

# 清单本身也可能被改 —— 拿这一串跟B站置顶评论里公布的比, 才是完整的一条链。
Say ''
Say '  清单自身指纹(跟B站置顶评论里那串比):' Gray
Say ("      " + (Sha256 $manifest)) Cyan

if(-not $Quiet){
    Write-Host ''
    try { Read-Host '按回车关闭' | Out-Null } catch { }
}
exit ([int](($bad.Count + $missing.Count) -gt 0))
