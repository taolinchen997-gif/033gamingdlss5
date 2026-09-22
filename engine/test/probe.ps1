# =====================================================================
#  033 · NGX 探针无人值守跑 (古墓丽影启动器背景就够, 不用进游戏)
#  发现: 强制启动器窗口置顶 → 它的 D3D12 背景开始渲染 → autoprobe 触发。
#  用法: .\probe.ps1 [-Cfg "autoprobe=1`nsnr_patch=1"] [-Tag 名字] [-KeepRenodx]
# =====================================================================
param([string]$Cfg = "autoprobe=1`nsnr_patch=1", [string]$Tag = "probe", [switch]$KeepRenodx)
$s = "E:\Steam\steamapps\common\Shadow of the Tomb Raider"
$out = "E:\033插件\test\logs"; New-Item -ItemType Directory -Force $out | Out-Null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class TP {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
}
"@
Stop-Process -Name SOTTR -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 3
if (-not $KeepRenodx -and (Test-Path "$s\renodx-dlss5.addon64")) { Rename-Item "$s\renodx-dlss5.addon64" "renodx-dlss5.addon64.off" -Force; "renodx 挪开" }
elseif ($KeepRenodx) { "renodx 保留" }
Copy-Item "E:\033插件\build\dlss5-033.addon64" "$s\" -Force
Set-Content "$s\dlss5-033.cfg" -Value $Cfg -Encoding ASCII
Remove-Item "$s\dlss5-033.log" -Force -ErrorAction SilentlyContinue
"部署, cfg: " + ($Cfg -replace "`n"," | ")
Start-Process "steam://rungameid/750920"

# 等启动器出现, 每次都强制置顶(触发它的 D3D12 背景渲染), 轮询日志
$done = $false
for ($i=0; $i -lt 60; $i++) {
  Start-Sleep -Seconds 2
  $p = Get-Process SOTTR -EA SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($p) {
    [void][TP]::ShowWindow($p.MainWindowHandle, 9)
    [void][TP]::SetWindowPos($p.MainWindowHandle, [IntPtr]::new(-1), 0,0,0,0, 0x43)  # TOPMOST
  }
  if (Test-Path "$s\dlss5-033.log") {
    $c = Get-Content "$s\dlss5-033.log" -Raw -EA SilentlyContinue
    if ($c -match 'autoprobe 结束') { $done = $true; "日志已完整 (第 $($i*2) 秒)"; break }
  }
}
Stop-Process -Name SOTTR -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 3
if (Test-Path "$s\renodx-dlss5.addon64.off") { Rename-Item "$s\renodx-dlss5.addon64.off" "renodx-dlss5.addon64" -Force }
Set-Content "$s\dlss5-033.cfg" -Value "autoprobe=0" -Encoding ASCII

if (Test-Path "$s\dlss5-033.log") {
  Copy-Item "$s\dlss5-033.log" "$out\$Tag.log" -Force
  "===== 日志 ====="; Get-Content "$out\$Tag.log" -Encoding UTF8
} else { "★没日志★"; Get-Content "$s\ReShade.log" -Tail 6 -EA SilentlyContinue | ForEach-Object { $_.Substring(0,[Math]::Min(140,$_.Length)) } }
