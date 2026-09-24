# Dedicated product scope; the generic transaction and external vault stay shared.
# S39（业主「国际版，也就是steam目前安装器不认，只认国服，这是个确定要修掉！」）：国服主程序 yysls.exe，
# 国际版（Steam、国际版启动器，Where Winds Meet）主程序 wwm.exe，目录结构相同：...\Engine\Binaries\Win64r（或 Win64rh）\<主程序>。
function Get-033YanYunExeNames{return @('yysls.exe','wwm.exe')}
function Get-033YanYunTargets([string[]]$Executables,[switch]$IncludeSiblings){
 $result=[Collections.Generic.List[string]]::new()
 foreach($exe in $Executables){
  $full=[IO.Path]::GetFullPath($exe);$dir=Split-Path -Parent $full;$binaries=Split-Path -Parent $dir;$leaf=[IO.Path]::GetFileName($full)
  if(@(Get-033YanYunExeNames) -inotcontains $leaf -or [IO.Path]::GetFileName($dir) -notmatch '^Win64[^\\/]*$' -or [IO.Path]::GetFileName($binaries) -ine 'Binaries' -or [IO.Path]::GetFileName((Split-Path -Parent $binaries)) -ine 'Engine'){
   throw '这是燕云独立定制版。请选择燕云目录 Engine\Binaries\Win64r（或 Win64rh）中的 yysls.exe（国服）或 wwm.exe（国际版 / Steam）。'
  }
  $result.Add($full)
  # 同一个游戏的另一个入口：找同名主程序（国服找 yysls.exe，国际版找 wwm.exe）。
  if($IncludeSiblings){foreach($sibling in @(Get-ChildItem -LiteralPath $binaries -Directory -Filter 'Win64*')){
   $candidate=Join-Path $sibling.FullName $leaf;if(Test-Path -LiteralPath $candidate -PathType Leaf){$result.Add($candidate)}
  }}
 }
 return @($result|Sort-Object -Unique)
}
# S39：「净化后安装」的厂商名单只在国服目录上核对过；国际版目录里有国服没有的文件（比如 Steam 自己的
# steam_api64.dll），净化可能把游戏要用的文件移走，所以国际版先不开放，安装 / 升级 / 还原照常。
function Assert-033YanYunCleanSupported([string[]]$Executables){
 foreach($exe in $Executables){
  if([IO.Path]::GetFileName([string]$exe) -ieq 'wwm.exe'){
   throw '国际版（wwm.exe）暂时不能用「净化后安装」：国际版目录里有国服没有的文件（比如 Steam 自己的组件），还没逐个核对，净化可能误移。请点「安装 / 升级」，033 的功能一样；没有改动游戏。'
  }
 }
}
