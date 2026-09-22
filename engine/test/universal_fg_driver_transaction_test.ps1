param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop';$repo=Split-Path -Parent $PSScriptRoot
$testRoot=Join-Path $repo ('build\driver-transaction-tests\'+[Guid]::NewGuid().ToString('N'));$pkg=Join-Path $testRoot 'package';$game=Join-Path $testRoot 'game'
New-Item -ItemType Directory -Path $pkg,$game -Force|Out-Null
# Immutable package data can share disk extents. The test helper and manifest
# are separate files; modifying them must never modify the real package.
foreach($f in Get-ChildItem -LiteralPath $PackageRoot -File -Recurse){
 $rel=$f.FullName.Substring($PackageRoot.TrimEnd('\').Length+1);$dst=Join-Path $pkg $rel
 New-Item -ItemType Directory -Path (Split-Path -Parent $dst) -Force|Out-Null
 if($rel -eq 'manifest.json'){Copy-Item -LiteralPath $f.FullName -Destination $dst}
 elseif($rel -eq 'tools\universal_fg_install.ps1'){Copy-Item -LiteralPath (Join-Path $repo 'deploy\universal_fg_install.ps1') -Destination $dst}
 elseif($rel -ne 'tools\smooth_motion_profile.exe'){New-Item -ItemType HardLink -Path $dst -Target $f.FullName|Out-Null}
}
$msvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$sdk='C:\Program Files (x86)\Windows Kits\10';$v='10.0.26100.0'
$env:INCLUDE="$msvc\include;$sdk\Include\$v\ucrt;$sdk\Include\$v\um;$sdk\Include\$v\shared"
$env:LIB="$msvc\lib\x64;$sdk\Lib\$v\ucrt\x64;$sdk\Lib\$v\um\x64"
$helper=Join-Path $pkg 'tools\smooth_motion_profile.exe'
Push-Location $testRoot
try{& "$msvc\bin\Hostx64\x64\cl.exe" /nologo /O2 /MD /EHsc /utf-8 "$PSScriptRoot\smooth_motion_profile_stub.cpp" /link "/OUT:$helper";if($LASTEXITCODE){throw 'Mock compilation failed'}}finally{Pop-Location}
$manifestPath=Join-Path $pkg 'manifest.json';$m=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
$entry=$m.Files|Where-Object Path -eq 'tools/smooth_motion_profile.exe';$entry.SHA256=(Get-FileHash -LiteralPath $helper).Hash;$entry.Bytes=(Get-Item -LiteralPath $helper).Length
$scriptEntry=$m.Files|Where-Object Path -eq 'tools/universal_fg_install.ps1';$scriptPath=Join-Path $pkg 'tools\universal_fg_install.ps1';$scriptEntry.SHA256=(Get-FileHash -LiteralPath $scriptPath).Hash;$scriptEntry.Bytes=(Get-Item -LiteralPath $scriptPath).Length
[IO.File]::WriteAllText($manifestPath,($m|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
$exe=Join-Path $game 'fixture.exe';Copy-Item -LiteralPath (Join-Path $repo 'build\033-integrated-candidate_20260906\smoke-dlss-real-nr-proxy\core_backend_smoke_test.exe') -Destination $exe
$original=@{'dlss5-033.cfg'="inject=0`r`ncarrier=0`r`nwork=80`r`n";'dlss5-033.addon64'='previous';'ReShade.ini'="[RenoDX.DLSS5]`r`nEnableHooks=`r`n";'dxgi.dll'='keep'}
foreach($n in $original.Keys){[IO.File]::WriteAllText((Join-Path $game $n),$original[$n],[Text.UTF8Encoding]::new($false))}
$checks=0;function Check($ok,$label){$script:checks++;if(-not $ok){throw "FAIL $label"};"PASS $label"}
function ReadMock{$r=& $helper read fixture.exe;if($LASTEXITCODE){throw 'Mock read failed'};return ($r|ConvertFrom-Json).value}
$install=Join-Path $pkg 'tools\universal_fg_install.ps1'
$result=& $install -Action Install -GameExe $exe -PreHighlightsPercent 12
Check ([IO.File]::ReadAllText((Join-Path $game 'dlss5-033.cfg')) -match '(?m)^prehighlights=12\r?$') 'universal wrapper forwards the explicit input highlight preset'
Check ((ReadMock) -eq 0) 'application driver disabled after file install'
Check ($result.SmoothMotionBefore -eq 7 -and $result.SmoothMotionAfter -eq 0) 'exact previous driver value recorded'
[IO.File]::AppendAllText((Join-Path $game 'dlss5-033.cfg'),"test-edit=99`r`n")
& $install -Action Restore -Receipt $result.Receipt -ArchiveChangedSettings|Out-Null
Check ((ReadMock) -eq 7) 'exact driver value restored, not a guessed default'
foreach($n in $original.Keys){Check ([IO.File]::ReadAllText((Join-Path $game $n)) -eq $original[$n]) "restored original $n"}
$saved=Get-ChildItem -LiteralPath (Split-Path -Parent $result.Receipt) -Directory -Filter 'test-settings-*'
Check ([IO.File]::ReadAllText((Join-Path $saved.FullName 'dlss5-033.cfg')) -match 'test-edit=99') 'manual settings edits archived before rollback'
# A Home-fix upgrade has a second receipt and starts from driver value zero.
# Undo newest first, then the original installation, to recover custom value 7.
$first=& $install -Action Install -GameExe $exe
$firstCore=(Get-FileHash -LiteralPath (Join-Path $game $first.CoreMount)).Hash
$upgrade=& $install -Action Install -GameExe $exe
Check ($upgrade.SmoothMotionBefore -eq 0 -and $upgrade.SmoothMotionAfter -eq 0) 'upgrade preserves the already disabled application driver'
& $install -Action Restore -Receipt $upgrade.Receipt -ArchiveChangedSettings|Out-Null
Check ((ReadMock) -eq 0 -and (Get-FileHash -LiteralPath (Join-Path $game $first.CoreMount)).Hash -eq $firstCore) 'newest receipt restores the previous engine and its driver value'
& $install -Action Restore -Receipt $first.Receipt -ArchiveChangedSettings|Out-Null
Check ((ReadMock) -eq 7) 'two receipt rollback restores the original custom driver value'
foreach($n in $original.Keys){Check ([IO.File]::ReadAllText((Join-Path $game $n)) -eq $original[$n]) "two receipt rollback restores $n"}
$pending=& $install -Action Install -GameExe $exe
$readFailure=$helper+'.state.fail-read';[IO.File]::WriteAllText($readFailure,'unavailable NVAPI mock')
$recovered=& $install -Action Restore -Receipt $pending.Receipt
Check ($recovered.Status -eq 'files-restored-driver-pending' -and $recovered.Recovery.Files -eq 'restored') 'unavailable driver does not block file restoration'
foreach($n in $original.Keys){Check ([IO.File]::ReadAllText((Join-Path $game $n)) -eq $original[$n]) "driver failure still restores $n"}
Check (-not(Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'driver failure still removes the installed mount'
$cfg=Join-Path $game 'dlss5-033.cfg';[IO.File]::WriteAllText($cfg,'post-recovery user setting')
Remove-Item -LiteralPath $readFailure
$retried=& $install -Action Restore -Receipt $pending.Receipt
Check ($retried.Status -eq 'restored' -and (ReadMock) -eq 7) 'driver-only retry restores custom 7 and final status'
Check ([IO.File]::ReadAllText($cfg) -eq 'post-recovery user setting') 'driver-only retry does not overwrite recovered file edits'
[IO.File]::WriteAllText($cfg,$original['dlss5-033.cfg'])
$filesOnly=& $install -Action Install -GameExe $exe
[IO.File]::WriteAllText($readFailure,'read must not run')
$readsBefore=(Get-Item -LiteralPath ($helper+'.state.reads')).Length
$recovered=& $install -Action Restore -Receipt $filesOnly.Receipt -FilesOnly
Check ($recovered.Status -eq 'files-restored-driver-pending') 'explicit files-only recovery does not call the driver'
Check ((Get-Item -LiteralPath ($helper+'.state.reads')).Length -eq $readsBefore) 'files-only recovery made zero helper read calls'
Remove-Item -LiteralPath $readFailure
Check ((ReadMock) -eq 0) 'files-only recovery leaves driver unchanged'
& $install -Action Restore -Receipt $filesOnly.Receipt | Out-Null
$noHelper=& $install -Action Install -GameExe $exe
Move-Item -LiteralPath $helper -Destination ($helper+'.unavailable')
try{$recovered=& $install -Action Restore -Receipt $noHelper.Receipt
 Check ($recovered.Status -eq 'files-restored-driver-pending' -and -not(Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'missing driver helper does not block file restoration'
}finally{Move-Item -LiteralPath ($helper+'.unavailable') -Destination $helper}
& $install -Action Restore -Receipt $noHelper.Receipt | Out-Null
$edited=& $install -Action Install -GameExe $exe
& $helper set fixture.exe 9 | Out-Null
$recovered=& $install -Action Restore -Receipt $edited.Receipt
Check ($recovered.Status -eq 'files-restored-driver-pending' -and (ReadMock) -eq 9) 'intervening driver edit preserved while plugin files restored'
& $helper set fixture.exe 7 | Out-Null
& $install -Action Restore -Receipt $edited.Receipt | Out-Null
[IO.File]::WriteAllText(($helper+'.state.fail-zero'),'fail after setting zero')
$failed=$false;try{& $install -Action Install -GameExe $exe|Out-Null}catch{$failed=$true}
Check $failed 'uncertain driver write is reported'
Check ((ReadMock) -eq 7) 'uncertain driver write restores exact original driver value'
Check (-not(Test-Path -LiteralPath (Join-Path $game 'winmm.dll'))) 'failed installation rolls back the core mount'
foreach($n in $original.Keys){Check ([IO.File]::ReadAllText((Join-Path $game $n)) -eq $original[$n]) "failed install preserved $n"}
"Universal FG driver transaction: $checks checks, 0 failures. File-backed mock only; real driver untouched."
