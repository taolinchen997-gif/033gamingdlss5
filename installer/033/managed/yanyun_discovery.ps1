# Port of the retained 6.0 InstallerDiscovery.cs rules. Read-only, bounded named
# paths only; discovery never grants install authority or runs an executable.
function Get-033YanYunLocalSeeds([string]$InstallerFolder,[string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer')){
 $seeds=[Collections.Generic.List[string]]::new()
 $current=$InstallerFolder
 for($i=0;$i -lt 5 -and $current;$i++){$seeds.Add($current);$current=[IO.Path]::GetDirectoryName($current.TrimEnd('\'))}
 foreach($drive in [IO.DriveInfo]::GetDrives()){
  try{if($drive.DriveType -eq 'Fixed' -and $drive.IsReady){
   foreach($name in @('yysls','燕云十六声','Games/yysls','Games/燕云十六声','游戏/燕云十六声','Program Files (x86)/Steam/steamapps/common/燕云十六声','SteamLibrary/steamapps/common/燕云十六声')){$seeds.Add([IO.Path]::Combine($drive.RootDirectory.FullName,$name))}
  }}catch{}
 }
 foreach($hive in @([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryHive]::LocalMachine)){
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry64,[Microsoft.Win32.RegistryView]::Registry32)){
   $root=$null;$uninstall=$null
   try{
    $root=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,$view);$uninstall=$root.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')
    if($uninstall){foreach($name in $uninstall.GetSubKeyNames()){
     $app=$null;try{$app=$uninstall.OpenSubKey($name);if($app -and [string]$app.GetValue('DisplayName') -match '燕云|(?i)Where\s*Winds\s*Meet'){
      $location=[string]$app.GetValue('InstallLocation');if($location){$seeds.Add($location.Trim('"'))}
     }}finally{if($app){$app.Dispose()}}
    }}
   }catch{}finally{if($uninstall){$uninstall.Dispose()};if($root){$root.Dispose()}}
  }
 }
 foreach($rel in @('YanYunCompiled/last-location.txt','YanYunExclusive/last-location.txt')){
  $path=Join-Path $Vault $rel
  try{if([IO.File]::Exists($path) -and [IO.FileInfo]::new($path).Length -lt 32768){$seeds.Add([IO.File]::ReadAllText($path).Trim())}}catch{}
 }
 # Current shared installer ledger, including a restored group. Paths are only
 # hints; Find repeats filesystem/layout/PE validation and removes stale paths.
 $groups=Join-Path $Vault 'groups'
 if([IO.Directory]::Exists($groups)){
  foreach($group in @(Get-ChildItem -LiteralPath $groups -Directory -ErrorAction SilentlyContinue|Select-Object -First 256)){
   $path=Join-Path $group.FullName 'state.json'
   try{if([IO.File]::Exists($path) -and [IO.FileInfo]::new($path).Length -lt 2097152){
    $state=ConvertFrom-Json ([IO.File]::ReadAllText($path))
    if($state.PSObject.Properties['Targets']){foreach($target in @($state.Targets)){
     if($target.PSObject.Properties['Exe'] -and [IO.Path]::GetFileName([string]$target.Exe) -ieq 'yysls.exe'){$seeds.Add([string]$target.Exe)}
    }}
   }}catch{}
  }
 }
 return @($seeds|Where-Object {$_}|Sort-Object -Unique)
}
function Find-033YanYunInstallations([string[]]$Seeds){
 $found=@{};$seen=@{}
 foreach($raw in @($Seeds)){
  if([string]::IsNullOrWhiteSpace($raw)){continue}
  try{$seed=[IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($raw.Trim().Trim('"')))}catch{continue}
  if($seen.ContainsKey($seed)){continue};$seen[$seed]=$true
  $candidates=[Collections.Generic.List[string]]::new()
  if([IO.File]::Exists($seed)){$candidates.Add($seed)}
  elseif([IO.Directory]::Exists($seed)){
   foreach($suffix in @('','Engine/Binaries','Binaries','yysls_medium/Engine/Binaries','yysls_high/Engine/Binaries','yysls_low/Engine/Binaries')){
    $dir=if($suffix){Join-Path $seed $suffix}else{$seed}
    if([IO.File]::Exists((Join-Path $dir 'yysls.exe'))){$candidates.Add((Join-Path $dir 'yysls.exe'))}
    if([IO.Path]::GetFileName($dir) -ieq 'Binaries' -and [IO.Directory]::Exists($dir)){
     foreach($child in @(Get-ChildItem -LiteralPath $dir -Directory -Filter 'Win64*' -ErrorAction SilentlyContinue)){
      $exe=Join-Path $child.FullName 'yysls.exe';if([IO.File]::Exists($exe)){$candidates.Add($exe)}
     }
    }
   }
  }
  foreach($exe in $candidates){
   try{
    [void](Get-033YanYunTargets @($exe))
    $pe=Get-033PeInfo $exe;if($pe.Status -ne 'valid' -or $pe.Architecture -ne 'x64' -or $pe.IsDll){continue}
    $key=Split-Path -Parent (Split-Path -Parent $exe)
    if(!$found.ContainsKey($key) -or [string]::Compare([IO.Path]::GetFileName((Split-Path -Parent $exe)),[IO.Path]::GetFileName((Split-Path -Parent $found[$key])),[StringComparison]::OrdinalIgnoreCase) -lt 0){$found[$key]=$exe}
   }catch{}
  }
 }
 return @($found.Values|Sort-Object)
}
function Get-033YanYunLaunchSelection([string]$ExplicitPath,[string[]]$Detected){
 $choices=@($Detected|Sort-Object -Unique)
 if($ExplicitPath){return @{Exe=$ExplicitPath;Choices=$choices;Status='使用指定的燕云路径；安装前请退出游戏'}}
 if($choices.Count -eq 1){return @{Exe=$choices[0];Choices=$choices;Status='已自动识别燕云；退出游戏后点击安装 / 升级'}}
 if($choices.Count -gt 1){return @{Exe='';Choices=$choices;Status='发现多份燕云，请在路径下拉框中选择要安装的一份'}}
 return @{Exe='';Choices=@();Status='未找到燕云安装位置，请选择主程序或游戏文件夹'}
}
