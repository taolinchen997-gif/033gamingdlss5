param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop';$repo=Split-Path -Parent $PSScriptRoot
$root=Join-Path $repo ('build\universal-installer-tests\'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force|Out-Null
$exe=Join-Path $root 'fixture.exe'
Copy-Item -LiteralPath (Join-Path $repo 'build\033-integrated-candidate_20260906\smoke-dlss-real-nr-proxy\core_backend_smoke_test.exe') -Destination $exe
$original=@{
 'dlss5-033.cfg'="inject=0`r`ncarrier=0`r`nwork=80`r`npasses=4`r`npasswork=85`r`nprewarmth=7`r`nunknown=keep`r`n";
 'ReShade.ini'="[RenoDX.DLSS5]`r`nEnableHooks=`r`n[RenoDX.MFGUnlock]`r`nForceMultiplier=4`r`n";
 'dlss5-033.addon64'='old 033';'dlss5-feed.addon64'='existing Feeder';'dinput8.dll'='REFramework';'dxgi.dll'='ReShade';'dlss5-033.state'='1'
}
foreach($n in $original.Keys){[IO.File]::WriteAllText((Join-Path $root $n),$original[$n],[Text.UTF8Encoding]::new($false))}
$manifest=Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
$repair=$manifest.PSObject.Properties['FeederQueueLifetimeRepair']
if($repair){Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fixtures\feeder_before_0120.bin') -Destination (Join-Path $root 'dlss5-feed.addon64')}
Copy-Item -LiteralPath (Join-Path $repo '分发包\DLSS5一键包 v5.0\工具\运行时\renodx-dlss5.addon64') -Destination $root
$before=@{};Get-ChildItem -LiteralPath $root -File|ForEach-Object{$before[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
$checks=0;function Check($ok,$label){$script:checks++;if(-not $ok){throw "FAIL $label"};"PASS $label"}
$installer=Join-Path $PackageRoot 'tools\integrated_install.ps1'
$plan=& $installer -Action Plan -GameExe $exe -ImageFrameGen|ConvertFrom-Json
Check ($plan.CoreMount -eq 'winmm.dll' -and $plan.ModelPasses -eq 'preserve') 'vacant imported mount and preserved model plan'
$result=& $installer -Action Install -GameExe $exe -ImageFrameGen
$cfg=[IO.File]::ReadAllText((Join-Path $root 'dlss5-033.cfg'))
foreach($line in ($original['dlss5-033.cfg'] -split "`r`n"|Where-Object{$_})){Check ($cfg.Contains($line)) "preserved $line"}
Check ($cfg -match '(?m)^engine=1\r?$' -and $cfg -match '(?m)^imagefg=1\r?$') 'unified engine and image-FG preparation wired'
foreach($n in @('dlss5-feed.addon64','dinput8.dll','dxgi.dll')){$expected=if($n -eq 'dlss5-feed.addon64' -and $repair){$repair.Value.SHA256}else{$before[$n]};Check ((Get-FileHash -LiteralPath (Join-Path $root $n)).Hash -eq $expected) "expected payload or preserved file $n"}
$rs=[IO.File]::ReadAllText((Join-Path $root 'ReShade.ini'));Check ($rs -match 'ForceMultiplier=4' -and $rs -match 'EnableHooks=0') 'native multiplier preserved and old RenoDX hook disabled'
Check (-not(Test-Path -LiteralPath (Join-Path $root 'renodx-dlss5.addon64'))) 'reviewed old renderer removed from active set'
$opti=[IO.File]::ReadAllText((Join-Path $root 'OptiScaler.ini'));Check ($opti -match 'FGInput=nofg' -and $opti -match 'FGOutput=nofg') 'other frame generation remains off'
& $installer -Action Restore -Receipt $result.Receipt|Out-Null
foreach($n in $before.Keys){Check ((Get-FileHash -LiteralPath (Join-Path $root $n)).Hash -eq $before[$n]) "exact original restored $n"}
Check (-not(Test-Path -LiteralPath (Join-Path $root 'winmm.dll')) -and -not(Test-Path -LiteralPath (Join-Path $root 'OptiScaler.ini'))) 'original absent files remain absent after rollback'
$highlightPlan=& $installer -Action Plan -GameExe $exe -ImageFrameGen -PreHighlightsPercent 12|ConvertFrom-Json
Check ($highlightPlan.Pregrade -like '*highlights=12%*') 'optional input highlight preset is visible before installation'
$highlightInstall=& $installer -Action Install -GameExe $exe -ImageFrameGen -PreHighlightsPercent 12
$cfg=[IO.File]::ReadAllText((Join-Path $root 'dlss5-033.cfg'))
Check ($cfg -match '(?m)^prehighlights=12\r?$' -and $cfg -match '(?m)^pregrade=1\r?$' -and $cfg.Contains('passes=4') -and $cfg.Contains('prewarmth=7')) 'highlight preset changes input highlights while retaining layers and other grading'
& $installer -Action Restore -Receipt $highlightInstall.Receipt|Out-Null
Check ((Get-FileHash -LiteralPath (Join-Path $root 'dlss5-033.cfg')).Hash -eq $before['dlss5-033.cfg']) 'highlight preset restores exact original configuration'
if($repair){
 [IO.File]::WriteAllText((Join-Path $root 'dlss5-feed.addon64'),'unreviewed replacement')
 $blocked=$false;try{& $installer -Action Plan -GameExe $exe -ImageFrameGen|Out-Null}catch{$blocked=$true}
 Check ($blocked -and -not(Test-Path -LiteralPath (Join-Path $root 'winmm.dll'))) 'unknown Feeder is rejected before any installation mutation'
}
"Universal FG installer: $checks checks, 0 failures. No game or driver profile was run or changed."
