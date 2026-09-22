# A release is an exact reviewed set of recipes. Per-game partial repacks must
# update the central catalog as a whole; a valid DLL hash alone is insufficient.
function Assert-033ReleaseValue($Actual,$Expected,[string]$At){
 if($Expected -is [Collections.IDictionary]){
  if($Actual -isnot [Collections.IDictionary] -or $Actual.Count -ne $Expected.Count){throw "整包组件不配套: $At"}
  foreach($key in $Expected.Keys){if(-not $Actual.ContainsKey($key)){throw "整包组件缺失: $At/$key"};Assert-033ReleaseValue $Actual[$key] $Expected[$key] ($At+'/'+$key)}
 }elseif($Expected -is [Collections.IList]){
  if($Actual -isnot [Collections.IList] -or $Actual.Count -ne $Expected.Count){throw "整包组件数量不符: $At"}
  for($i=0;$i -lt $Expected.Count;$i++){Assert-033ReleaseValue $Actual[$i] $Expected[$i] ($At+'/'+$i)}
 }elseif(($null -eq $Actual) -ne ($null -eq $Expected) -or [string]$Actual -cne [string]$Expected){throw "整包组件版本不一致: $At"}
}
function Assert-033ReleaseContract([string]$Root,$Manifest){
 $present=Test-Path -LiteralPath (Get-033Path $Root '033-components.json') -PathType Leaf
 if(-not $Manifest.ContainsKey('ReleaseContract')){
  if($present){throw '整包组件目录存在但清单绑定丢失，未改游戏'}
  return # Historical packages remain readable for exact upgrade/restore.
 }
 $binding=$Manifest.ReleaseContract
 if($binding.Schema -ne 1 -or $binding.Source -cne '033-components.json' -or $binding.Hash -cnotmatch '^[a-f0-9]{64}$'){throw '整包组件目录绑定无效'}
 $path=Get-033Path $Root $binding.Source
 if((Get-033FileHash $path) -ine $binding.Hash){throw '整包组件目录缺失或已变化，未改游戏'}
 $catalog=Read-033ManagedJson $path
 if($catalog.Schema -ne 1 -or $catalog.Release -cne $Manifest.Version -or $catalog.Profiles.Count -ne $Manifest.Profiles.Count){throw '整包组件目录与版本/路线不一致'}
 foreach($profile in $Manifest.Profiles){
  if(-not $catalog.Profiles.ContainsKey($profile.Id)){throw "整包缺少路线: $($profile.Id)"}
  $entry=$catalog.Profiles[$profile.Id]
  if(-not $catalog.Families.ContainsKey($entry.Family)){throw '整包缺少配套组件家族'}
  foreach($field in @('Files','Components','RuntimeFiles','RuntimeDirectories')){
   $actual=if($profile.ContainsKey($field)){$profile[$field]}elseif($field -eq 'Components'){@{}}else{@()}
   # Avoid PowerShell's pipeline unrolling of empty / singleton arrays.
   if($field -ne 'Components'){$actual=@($actual)}
   Assert-033ReleaseValue $actual $entry[$field] ($profile.Id+'/'+$field)
  }
 }
}
