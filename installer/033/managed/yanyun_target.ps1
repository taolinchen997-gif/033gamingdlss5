# Dedicated product scope; the generic transaction and external vault stay shared.
function Get-033YanYunTargets([string[]]$Executables,[switch]$IncludeSiblings){
 $result=[Collections.Generic.List[string]]::new()
 foreach($exe in $Executables){
  $full=[IO.Path]::GetFullPath($exe);$dir=Split-Path -Parent $full;$binaries=Split-Path -Parent $dir
  if([IO.Path]::GetFileName($full) -ine 'yysls.exe' -or [IO.Path]::GetFileName($dir) -notmatch '^Win64[^\\/]*$' -or [IO.Path]::GetFileName($binaries) -ine 'Binaries' -or [IO.Path]::GetFileName((Split-Path -Parent $binaries)) -ine 'Engine'){
   throw '这是燕云独立定制版。请选择燕云目录 Engine\Binaries\Win64r（或 Win64rh）中的 yysls.exe。'
  }
  $result.Add($full)
  if($IncludeSiblings){foreach($sibling in @(Get-ChildItem -LiteralPath $binaries -Directory -Filter 'Win64*')){
   $candidate=Join-Path $sibling.FullName 'yysls.exe';if(Test-Path -LiteralPath $candidate -PathType Leaf){$result.Add($candidate)}
  }}
 }
 return @($result|Sort-Object -Unique)
}
