# Compatibility entry; never invoke the old filename/pattern-based cleanup.
param([string]$GameExe,[string]$GameDir,[switch]$NoSplash,[switch]$Silent,[switch]$NoElevate,
      [string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer'))
$ErrorActionPreference='Stop'
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
try{
    . (Join-Path $PSScriptRoot 'managed/managed_transaction.ps1')
    if(-not $GameExe -and $GameDir){
        $root=Get-033ManagedRoot $GameDir;$index=Get-033Path $Vault 'index';$matches=@()
        if(Test-Path -LiteralPath $index){
            foreach($file in Get-ChildItem -LiteralPath $index -File -Filter '*.json'){
                $record=Read-033ManagedJson $file.FullName
                if((Split-Path -Parent $record.Exe) -ieq $root){$matches+=@($record.Exe)}
            }
        }
        if($matches.Count -ne 1){throw '未找到唯一的新安装记录。不会搜索子目录或猜测要删哪些文件。'}
        $GameExe=$matches[0]
    }
    if(-not $GameExe){throw '请使用新安装器的一键恢复并选择游戏程序'}
    if(Test-Path -LiteralPath $GameExe -PathType Container){$resolved=Resolve-033GameExe $GameExe $Vault;Write-Host (Format-033Resolution $resolved);$GameExe=$resolved.Exe}
    # 业主 2026-09-12：默认用最高权限。选定游戏后申请管理员，带同样的参数在新窗口里接着卸；点了「否」就按普通权限继续。
    if(-not $Silent -and -not $NoElevate){
        $isAdmin=$false;try{$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}catch{}
        if(-not $isAdmin){
            $ps=[IO.Path]::Combine($env:SystemRoot,'System32','WindowsPowerShell','v1.0','powershell.exe');if(-not [IO.File]::Exists($ps)){$ps='powershell.exe'}
            Write-Host '正在申请管理员权限（默认用最高权限卸载，写游戏目录最稳）——弹出的确认框请点「是」。' -ForegroundColor Yellow
            $elevated=$false
            try{Start-Process -FilePath $ps -Verb RunAs -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-GameExe',('"'+([IO.Path]::GetFullPath($GameExe)).TrimEnd([char]92)+'"'),'-Vault',('"'+$Vault.TrimEnd([char]92)+'"'),'-NoElevate')|Out-Null;$elevated=$true}catch{Write-Host '没拿到管理员权限，按普通权限继续卸载。' -ForegroundColor Yellow}
            if($elevated){Write-Host '已在新窗口里以管理员身份继续，这个窗口会自己关掉。' -ForegroundColor Green;exit 0}
        }
    }
    $result=Invoke-033ManagedOperation -Action Restore -GameExe @($GameExe) -Vault $Vault
    $result|ConvertTo-Json -Depth 12;Write-Host $result.Message -ForegroundColor Green
    if(-not $Silent){Read-Host '按回车退出'|Out-Null}
    exit 0
}catch{
    Write-Host ('恢复未完成：'+$_.Exception.Message) -ForegroundColor Red
    if(-not $Silent){try{Read-Host '按回车退出'|Out-Null}catch{}}
    exit 1
}
