# =====================================================================
#  033 插件 · 古墓丽影暗影 无人值守测试
#
#  一条命令: 部署 → (可选)挪开 renodx → 启动 → 把启动器提到前台再点开始游戏 →
#            轮询到游戏真进画面 → 等够帧 → 杀进程 → 收日志 → 还原
#  教训: ①启动后别切窗口(前台锁抢不回来); ②启动器必须先 SetForegroundWindow 再点,
#        否则点空、进程停在启动器上、日志空; ③别盲等, 看 ReShade.log 的 runtime 行判断真进画面。
#
#  用法:  .\run_sottr_autoprobe.ps1 [-KeepRenodx] [-Play 90] [-Tag v03a] [-Cfg "carrier=1`nwork=75"]
# =====================================================================
param(
    [switch]$KeepRenodx,
    [int]$Play = 90,              # 真进画面后再跑多少秒
    [string]$Tag = "run",
    [string]$Cfg = "autoprobe=1"
)
$ErrorActionPreference = "Continue"
$s     = "E:\Steam\steamapps\common\Shadow of the Tomb Raider"
$build = "E:\033插件\build\dlss5-033.addon64"
$out   = "E:\033插件\test\logs"
New-Item -ItemType Directory -Force $out | Out-Null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  // 用 AttachThreadInput 打通输入队列, 绕过 Windows 前台锁, 把 hwnd 真正提到前台
  public static void ForceForeground(IntPtr hwnd) {
    ShowWindow(hwnd, 9); // SW_RESTORE
    uint fg = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
    uint me = GetCurrentThreadId();
    uint tgt = GetWindowThreadProcessId(hwnd, IntPtr.Zero);
    AttachThreadInput(me, fg, true); AttachThreadInput(me, tgt, true);
    BringWindowToTop(hwnd); SetForegroundWindow(hwnd);
    AttachThreadInput(me, fg, false); AttachThreadInput(me, tgt, false);
  }
}
"@

Stop-Process -Name SOTTR -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3

if (-not $KeepRenodx -and (Test-Path "$s\renodx-dlss5.addon64")) {
    Rename-Item "$s\renodx-dlss5.addon64" "renodx-dlss5.addon64.off" -Force; "renodx 已挪开"
} elseif ($KeepRenodx) { "renodx 保留(测共存)" }

Copy-Item $build "$s\" -Force
Set-Content "$s\dlss5-033.cfg" -Value $Cfg -Encoding ASCII
Remove-Item "$s\dlss5-033.log" -Force -ErrorAction SilentlyContinue
$rsLogStart = if (Test-Path "$s\ReShade.log") { (Get-Item "$s\ReShade.log").Length } else { 0 }
"部署 " + (Get-Item "$s\dlss5-033.addon64").Length + " 字节, cfg: " + ($Cfg -replace "`n", " | ")

Start-Process "steam://rungameid/750920"

# 等启动器窗口出现(最多 60s), 提到前台, 点开始游戏
$launcher = $null
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    $p = Get-Process SOTTR -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { $launcher = $p; break }
}
if (-not $launcher) { "★启动器 60s 没出现★"; return }
Start-Sleep -Seconds 4
[Win]::ForceForeground($launcher.MainWindowHandle)
Start-Sleep -Milliseconds 700
$fgOk = ([Win]::GetForegroundWindow() -eq $launcher.MainWindowHandle)
"启动器已在前台: $fgOk"
# 点两次: 第一次万一只是激活窗口, 第二次落在按钮上
foreach ($n in 1..2) {
    [void][Win]::SetCursorPos(1856, 1061); Start-Sleep -Milliseconds 250
    [Win]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80; [Win]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
}
"已点开始游戏 (启动器 PID $($launcher.Id))"

# 轮询: 033 日志出现, 或 ReShade.log 新增了 runtime 行, 视为真进画面 (最多 150s)
$entered = $false
for ($i = 0; $i -lt 75; $i++) {
    Start-Sleep -Seconds 2
    if (Test-Path "$s\dlss5-033.log") { $entered = $true; "033 日志已出现 (第 $($i*2) 秒)"; break }
    if ((Test-Path "$s\ReShade.log") -and ((Get-Item "$s\ReShade.log").Length -gt $rsLogStart)) {
        $tail = Get-Content "$s\ReShade.log" -Tail 40 -ErrorAction SilentlyContinue
        if ($tail -match 'Recreated runtime environment|Successfully compiled') { $entered = $true; "游戏已进画面 (第 $($i*2) 秒)"; break }
    }
    if (-not (Get-Process SOTTR -ErrorAction SilentlyContinue)) { "★进程消失 (第 $($i*2) 秒)★"; break }
}
if (-not $entered) {
    "★150s 内没进画面★"
    # 取证: 所有 SOTTR 进程的窗口标题 + 一张截图(不切焦点, 只拍屏)
    Get-Process SOTTR -ErrorAction SilentlyContinue | ForEach-Object { "  PID $($_.Id)  窗口='$($_.MainWindowTitle)'  内存 $([int]($_.WorkingSet64/1MB))MB  响应=$($_.Responding)" }
    try {
        Add-Type -AssemblyName System.Windows.Forms, System.Drawing
        $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
        $gr = [System.Drawing.Graphics]::FromImage($bmp); $gr.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size); $gr.Dispose()
        $small = New-Object System.Drawing.Bitmap ([int]($b.Width/2)), ([int]($b.Height/2))
        $g2 = [System.Drawing.Graphics]::FromImage($small); $g2.DrawImage($bmp, 0, 0, $small.Width, $small.Height); $g2.Dispose()
        $small.Save("$out\$Tag.stuck.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); $small.Dispose()
        "  截图 -> $out\$Tag.stuck.png"
    } catch { "  截图失败: $_" }
}
else { "再跑 $Play 秒"; Start-Sleep -Seconds $Play }

$alive = [bool](Get-Process SOTTR -ErrorAction SilentlyContinue)
"游戏存活: $alive"
Stop-Process -Name SOTTR -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 4

if (Test-Path "$s\renodx-dlss5.addon64.off") { Rename-Item "$s\renodx-dlss5.addon64.off" "renodx-dlss5.addon64" -Force; "renodx 已还原" }
Set-Content "$s\dlss5-033.cfg" -Value "autoprobe=0" -Encoding ASCII

$dst = "$out\$Tag.log"
if (Test-Path "$s\dlss5-033.log") {
    Copy-Item "$s\dlss5-033.log" $dst -Force
    "日志 -> $dst"; "-----"; Get-Content $dst -Encoding UTF8
} else { "★没有 033 日志★"; "--- ReShade.log 尾 ---"; Get-Content "$s\ReShade.log" -Tail 12 -ErrorAction SilentlyContinue | ForEach-Object { $_.Substring(0, [Math]::Min(150, $_.Length)) } }
