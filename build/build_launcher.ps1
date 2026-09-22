param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
# 033安装器.exe：只做一件事，用管理员权限启动 033\dlss5_install.ps1。用 .NET Framework 4 自带的 csc.exe 编译。
$repo=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $repo 'out\installer'}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$source=Join-Path $repo 'installer\launcher'
$csc=Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(!(Test-Path -LiteralPath $csc)){throw ('找不到 '+$csc)}
& $csc /nologo /target:winexe /platform:anycpu /optimize+ /reference:System.Windows.Forms.dll ('/out:'+(Join-Path $OutputDirectory '033安装器.exe')) ('/win32manifest:'+(Join-Path $source 'Launcher.manifest')) (Join-Path $source 'Launcher.cs')
if($LASTEXITCODE){throw '启动器编译失败'}
Get-FileHash -LiteralPath (Join-Path $OutputDirectory '033安装器.exe')|Select-Object Path,Hash|Format-List
