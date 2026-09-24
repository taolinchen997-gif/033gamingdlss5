# Port of the retained 6.0 InstallerDiscovery.cs rules. Read-only, bounded named
# paths only; discovery never grants install authority or runs an executable.
# S39：国际版（Where Winds Meet，主程序 wwm.exe）也找：Steam 记下的每个游戏库、Epic 启动器的安装清单、
# 国际版的常见目录名；国际版客户端目录叫 wwm_standard。
function Get-033SteamGameSeeds([string[]]$SteamRoots=$null){
 # Steam 库可以在任何盘（E:\Steam、D:\SteamLibrary…）：读 Steam 自己的 libraryfolders.vdf，再看每个库里
 # 燕云 / Where Winds Meet 的 appmanifest（国际版 AppID 3564740）。只读，限数量和大小。
 $seeds=[Collections.Generic.List[string]]::new()
 if($null -eq $SteamRoots){
  $SteamRoots=@()
  foreach($spec in @(@('CurrentUser','Software\Valve\Steam','SteamPath'),@('LocalMachine','SOFTWARE\WOW6432Node\Valve\Steam','InstallPath'),@('LocalMachine','SOFTWARE\Valve\Steam','InstallPath'))){
   $root=$null;$key=$null
   try{
    $root=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]$spec[0],[Microsoft.Win32.RegistryView]::Registry64)
    $key=$root.OpenSubKey($spec[1])
    if($key){$value=[string]$key.GetValue($spec[2]);if($value){$SteamRoots+=@($value.Replace('/','\'))}}
   }catch{}finally{if($key){$key.Dispose()};if($root){$root.Dispose()}}
  }
 }
 $libraries=[Collections.Generic.List[string]]::new()
 foreach($steam in @($SteamRoots|Where-Object {$_})){
  $libraries.Add([string]$steam)
  $vdf=Join-Path $steam 'steamapps\libraryfolders.vdf'
  try{if([IO.File]::Exists($vdf) -and [IO.FileInfo]::new($vdf).Length -lt 1048576){
   foreach($match in [regex]::Matches([IO.File]::ReadAllText($vdf),'"path"\s+"((?:[^"\\]|\\.)*)"')){$libraries.Add($match.Groups[1].Value.Replace('\\','\'))}
  }}catch{}
 }
 foreach($library in @($libraries|Sort-Object -Unique|Select-Object -First 64)){
  $apps=Join-Path $library 'steamapps'
  foreach($name in @('Where Winds Meet','燕云十六声')){$seeds.Add((Join-Path $apps ('common\'+$name)))}
  try{if([IO.Directory]::Exists($apps)){
   foreach($manifest in @(Get-ChildItem -LiteralPath $apps -File -Filter 'appmanifest_*.acf' -ErrorAction Stop|Select-Object -First 4096)){
    if($manifest.Length -ge 65536){continue}
    try{
     $text=[IO.File]::ReadAllText($manifest.FullName)
     $dir=[regex]::Match($text,'"installdir"\s+"((?:[^"\\]|\\.)*)"');$title=[regex]::Match($text,'"name"\s+"((?:[^"\\]|\\.)*)"')
     if($dir.Success -and ($manifest.Name -ieq 'appmanifest_3564740.acf' -or ($title.Success -and $title.Groups[1].Value -match '燕云|(?i)Where\s*Winds\s*Meet'))){$seeds.Add((Join-Path $apps ('common\'+$dir.Groups[1].Value.Replace('\\','\'))))}
    }catch{}
   }
  }}catch{}
 }
 return @($seeds)
}
function Get-033EpicGameSeeds([string]$ManifestDirectory=$(if($env:ProgramData){Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'}else{''})){
 # Epic 启动器的安装清单（*.item，JSON）：显示名是燕云 / Where Winds Meet 的，取它的安装位置。
 $seeds=[Collections.Generic.List[string]]::new()
 try{if($ManifestDirectory -and [IO.Directory]::Exists($ManifestDirectory)){
  foreach($item in @(Get-ChildItem -LiteralPath $ManifestDirectory -File -Filter '*.item' -ErrorAction Stop|Select-Object -First 1024)){
   if($item.Length -ge 1048576){continue}
   try{
    $data=ConvertFrom-Json ([IO.File]::ReadAllText($item.FullName))
    if([string]$data.DisplayName -match '燕云|(?i)Where\s*Winds\s*Meet' -and [string]$data.InstallLocation){$seeds.Add([string]$data.InstallLocation)}
   }catch{}
  }
 }}catch{}
 return @($seeds)
}
function Get-033YanYunLocalSeeds([string]$InstallerFolder,[string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer')){
 $seeds=[Collections.Generic.List[string]]::new()
 $current=$InstallerFolder
 for($i=0;$i -lt 5 -and $current;$i++){$seeds.Add($current);$current=[IO.Path]::GetDirectoryName($current.TrimEnd('\'))}
 foreach($drive in [IO.DriveInfo]::GetDrives()){
  try{if($drive.DriveType -eq 'Fixed' -and $drive.IsReady){
   foreach($name in @('yysls','燕云十六声','Games/yysls','Games/燕云十六声','游戏/燕云十六声','Program Files (x86)/Steam/steamapps/common/燕云十六声','SteamLibrary/steamapps/common/燕云十六声',
                      'Where Winds Meet','Games/Where Winds Meet','Program Files/Where Winds Meet','Steam/steamapps/common/Where Winds Meet',
                      'Program Files (x86)/Steam/steamapps/common/Where Winds Meet','SteamLibrary/steamapps/common/Where Winds Meet')){$seeds.Add([IO.Path]::Combine($drive.RootDirectory.FullName,$name))}
  }}catch{}
 }
 foreach($seed in @(Get-033SteamGameSeeds)){$seeds.Add($seed)}
 foreach($seed in @(Get-033EpicGameSeeds)){$seeds.Add($seed)}
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
     if($target.PSObject.Properties['Exe'] -and @(Get-033YanYunExeNames) -icontains [IO.Path]::GetFileName([string]$target.Exe)){$seeds.Add([string]$target.Exe)}
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
   $suffixes=@('','Engine/Binaries','Binaries','yysls_medium/Engine/Binaries','yysls_high/Engine/Binaries','yysls_low/Engine/Binaries')
   foreach($client in @(Get-ChildItem -LiteralPath $seed -Directory -Filter 'wwm_*' -ErrorAction SilentlyContinue|Select-Object -First 8)){$suffixes+=@($client.Name+'/Engine/Binaries')}
   foreach($suffix in $suffixes){
    $dir=if($suffix){Join-Path $seed $suffix}else{$seed}
    foreach($name in @(Get-033YanYunExeNames)){if([IO.File]::Exists((Join-Path $dir $name))){$candidates.Add((Join-Path $dir $name))}}
    if([IO.Path]::GetFileName($dir) -ieq 'Binaries' -and [IO.Directory]::Exists($dir)){
     foreach($child in @(Get-ChildItem -LiteralPath $dir -Directory -Filter 'Win64*' -ErrorAction SilentlyContinue)){
      foreach($name in @(Get-033YanYunExeNames)){$exe=Join-Path $child.FullName $name;if([IO.File]::Exists($exe)){$candidates.Add($exe)}}
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
