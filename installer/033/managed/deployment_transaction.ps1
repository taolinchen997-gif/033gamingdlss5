# Exact file transactions. No process launch, heuristic rollback or forced overwrite.
Set-StrictMode -Version Latest
function Get-033Path([string]$Root,[string]$Relative) {
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':')){throw "Invalid relative path: $Relative"}
    foreach($part in ($Relative -split '[\\/]')){
        if(-not $part -or $part -in @('.','..') -or $part -match '[. ]$' -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)'){throw "Invalid path component: $Relative"}
    }
    $path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
    if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escaped target root'}
    $cursor=$path
    while($cursor.Length -ge $base.Length){
        if(Test-Path -LiteralPath $cursor){$item=Get-Item -LiteralPath $cursor -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point unsupported: $cursor"}}
        if($cursor -eq $base){break};$cursor=Split-Path -Parent $cursor
    }
    return $path
}
function Get-033FileHash([string]$Path){
    if(Test-Path -LiteralPath $Path -PathType Container){throw "A directory occupies file path: $Path"}
    if(Test-Path -LiteralPath $Path -PathType Leaf){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash}
    return $null
}
function Test-033Member($Value,[string]$Name){
    if($Value -is [Collections.IDictionary]){return $Value.Contains($Name)}
    return $null -ne $Value.PSObject.Properties[$Name]
}
function ConvertTo-033UtcTime($Value){
    # PS7 may deserialize JSON dates as DateTime; stringifying those would
    # discard their UTC kind and apply the local offset a second time.
    if($Value -is [DateTime]){return $Value.ToUniversalTime()}
    return [DateTime]::Parse($Value,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
# 文件属性只认能设置、有意义的几位：只读、隐藏、系统、存档、临时、不索引（0x2127）。2026-09-11 公开包：部分解压工具
# 把 zip 条目里的 Unix 权限高位（0x81B60000）原样当 Windows 属性写，NTFS 留下 NoScrubData|Unpinned，属性成了 1179680；
# PowerShell 把它转成 [IO.FileAttributes] 直接抛异常，仁王3、FF7 重制版、无限暖暖等一装就撤销。ReFS 和云同步目录也会
# 带这类位。其余位一律不设、不记、不比。
function Get-033SettableAttributes($Value){return ([long]$Value -band 0x2127)}
function ConvertTo-033FileAttributes($Value){
    $bits=[int](Get-033SettableAttributes $Value)
    if(-not $bits){$bits=[int][IO.FileAttributes]::Normal}
    return [Enum]::ToObject([IO.FileAttributes],$bits)
}
function Write-033Json([string]$Path,$Value){
    $temp=Join-Path (Split-Path -Parent $Path) ([Guid]::NewGuid().ToString('N').Substring(0,16)+'.tmp')
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 20))
    $stream=[IO.FileStream]::new($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    # A journal replacement can itself meet a short-lived sharing lock.
    # Retry only failures that leave both files in place, preserving the
    # original record and durable staged bytes. Never delete/copy over it.
    $original=Get-033FileHash $Path
    $delays=@(120,250,500,900,1500)
    for($attempt=0;;$attempt++){
        try{
            if([IO.File]::Exists($Path)){[IO.File]::Replace($temp,$Path,[NullString]::Value)}else{[IO.File]::Move($temp,$Path)}
            return
        }catch [IO.IOException]{
            $code=$_.Exception.GetBaseException().HResult -band 0xffff
            if($code -notin @(32,33,1175) -or $attempt -ge $delays.Count){throw}
            Start-Sleep -Milliseconds $delays[$attempt]
            if(-not [IO.File]::Exists($temp) -or (Get-033FileHash $Path) -cne $original){throw}
        }
    }
}
function Enter-033TransactionLock([string]$Root){
    $sha=[Security.Cryptography.SHA256]::Create()
    try{$id=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($Root).TrimEnd('\').ToLowerInvariant())))).Replace('-','')}finally{$sha.Dispose()}
    $mutex=[Threading.Mutex]::new($false,('Local\033_Deployment_'+$id))
    try{try{$owned=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$owned=$true};if(-not $owned){throw 'Another 033 transaction owns this directory'};return $mutex}catch{$mutex.Dispose();throw}
}
# No-op observation seam, overridden only by offline fault-injection tests.
function Invoke-033TransactionStep([string]$Stage,[string]$Receipt,[string]$Path) {}
function Set-033TransactionFile([string]$Target,[string]$Source,[string]$ExpectedHash){
    if($Source){
        New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
        $temp=Join-Path (Split-Path -Parent $Target) ('.033-'+[Guid]::NewGuid().ToString('N').Substring(0,16)+'.tmp')
        [IO.File]::Copy($Source,$temp,$false)
        if((Get-033FileHash $temp) -ne $ExpectedHash){throw "Staged bytes changed: $Source"}
        [IO.File]::SetAttributes($temp,[IO.FileAttributes]::Normal)
        $stream=[IO.FileStream]::new($temp,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try{$stream.Flush($true)}finally{$stream.Dispose()}
        if([IO.File]::Exists($Target)){
            $attributes=[IO.File]::GetAttributes($Target)
            [IO.File]::SetAttributes($Target,($attributes -band (-bnot [IO.FileAttributes]::ReadOnly)))
            try{[IO.File]::Replace($temp,$Target,[NullString]::Value)}catch{[IO.File]::SetAttributes($Target,$attributes);throw}
        }else{[IO.File]::Move($temp,$Target)}
    }elseif([IO.File]::Exists($Target)){[IO.File]::SetAttributes($Target,[IO.FileAttributes]::Normal);[IO.File]::Delete($Target)}
    if([string](Get-033FileHash $Target) -ne $ExpectedHash){throw "Result hash mismatch: $Target"}
}
function Restore-033Transaction([string]$Receipt,[switch]$Recover) {
    $receiptPath=[IO.Path]::GetFullPath($Receipt)
    $state=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if($state.Version -notin @(1,2) -or $state.Status -notin @('prepared','applying','installed','restoring','restored')){throw 'Unknown transaction state'}
    $backupRoot=Split-Path -Parent $receiptPath
    $expectedPrefix=(Get-033Path $state.Root '_033transactions')+'\'
    if(-not $receiptPath.StartsWith($expectedPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Receipt is outside the recorded transaction root'}
    $mutex=Enter-033TransactionLock $state.Root
    try{
        # -Recover never disables conflicts, including on legacy v1 receipts.
        $interrupted=$state.Status -in @('prepared','applying','restoring')
        $alreadyRestored=$state.Status -eq 'restored';$seen=@{}
        foreach($entry in $state.Files){
            $target=Get-033Path $state.Root $entry.Path
            if($seen.ContainsKey($target)){throw 'Duplicate receipt target'};$seen[$target]=$true
            if($entry.Existed){
                if((Get-033FileHash (Get-033Path $backupRoot $entry.Backup)) -ne $entry.BeforeHash -or -not $entry.BeforeHash){throw "Backup changed: $($entry.Path)"}
                [void](ConvertTo-033UtcTime $entry.BeforeTime);[void][long]$entry.BeforeAttributes
            }elseif($entry.BeforeHash){throw 'Invalid absent-file backup hash'}
            $current=Get-033FileHash $target
            $ok=if($alreadyRestored){$current -eq $entry.BeforeHash}elseif($interrupted){$current -eq $entry.BeforeHash -or $current -eq $entry.AfterHash}else{$current -eq $entry.AfterHash}
            if(-not $ok){throw "External modification blocks restore: $($entry.Path)"}
        }
        $state.Status='restoring';Write-033Json $receiptPath $state
        foreach($entry in $state.Files){
            $target=Get-033Path $state.Root $entry.Path
            Invoke-033TransactionStep 'before-restore' $receiptPath $entry.Path
            $current=Get-033FileHash $target
            if($current -ne $entry.BeforeHash -and $current -ne $entry.AfterHash){throw "External modification during restore: $($entry.Path)"}
            if($current -ne $entry.BeforeHash){
                $source=if($entry.Existed){Get-033Path $backupRoot $entry.Backup}else{$null}
                Set-033TransactionFile $target $source $entry.BeforeHash
            }
            if($entry.Existed){
                $beforeAttributes=ConvertTo-033FileAttributes $entry.BeforeAttributes
                [IO.File]::SetAttributes($target,($beforeAttributes -band (-bnot [IO.FileAttributes]::ReadOnly)))
                try{[IO.File]::SetLastWriteTimeUtc($target,(ConvertTo-033UtcTime $entry.BeforeTime))}
                finally{[IO.File]::SetAttributes($target,$beforeAttributes)}
            }
            Invoke-033TransactionStep 'after-restore' $receiptPath $entry.Path
            if($state.Version -eq 2){$entry.Phase='restored';Write-033Json $receiptPath $state}
        }
        $state.Status='restored';$state.RestoredAt=[DateTime]::UtcNow.ToString('o');Write-033Json $receiptPath $state
        return $state
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
function Invoke-033Transaction([string]$Root,[object[]]$Plan,[string]$Label='candidate') {
    $Root=[IO.Path]::GetFullPath($Root)
    if(-not (Test-Path -LiteralPath $Root -PathType Container)){throw 'Target root missing'}
    $mutex=Enter-033TransactionLock $Root
    try{
        $txRoot=Get-033Path $Root '_033transactions'
        if(Test-Path -LiteralPath $txRoot){
            foreach($folder in (Get-ChildItem -LiteralPath $txRoot -Directory -Force)){
                $old=Get-033Path $txRoot ($folder.Name+'\receipt.json')
                if(Test-Path -LiteralPath $old){
                    $pending=Get-Content -LiteralPath $old -Raw -Encoding UTF8|ConvertFrom-Json
                    if($pending.Status -notin @('installed','restored')){throw "Unfinished transaction; recover this exact receipt first: $old"}
                }
            }
        }
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($action in $Plan){
            $target=Get-033Path $Root $action.Path
            if(-not $seen.Add($target)){throw "Duplicate target: $target"}
            if($action.Source -and -not (Test-Path -LiteralPath $action.Source -PathType Leaf)){throw "Source missing: $($action.Source)"}
            [void](Get-033FileHash $target)
        }
        # Creation time is recorded in JSON; avoid duplicating it in a long
        # folder name on Windows PowerShell 5.1 / legacy MAX_PATH systems.
        $relative='_033transactions\'+[Guid]::NewGuid().ToString('N')
        $backupRoot=Get-033Path $Root $relative;New-Item -ItemType Directory -Path $backupRoot | Out-Null
        $files=@();$index=0
        foreach($action in $Plan){
            $target=Get-033Path $Root $action.Path;$before=Get-033FileHash $target
            if((Test-033Member $action 'ExpectedBeforeHash') -and [string]$before -ne [string]$action.ExpectedBeforeHash){throw 'Target changed after planning'}
            $entry=[ordered]@{Path=$action.Path;Existed=($null -ne $before);Backup=('{0:D4}.bin' -f $index);Payload=('{0:D4}.after' -f $index);BeforeHash=$before;BeforeTime=$null;BeforeAttributes=0;AfterHash=$null;AfterTime=$null;AfterAttributes=0;Phase='pending'}
            if($entry.Existed){
                $item=Get-Item -LiteralPath $target -Force;$entry.BeforeTime=$item.LastWriteTimeUtc.ToString('o');$entry.BeforeAttributes=[int](Get-033SettableAttributes $item.Attributes)
                [IO.File]::Copy($target,(Join-Path $backupRoot $entry.Backup),$false)
                if((Get-033FileHash (Join-Path $backupRoot $entry.Backup)) -ne $before){throw 'Backup verification failed'}
            }
            if($action.Source){
                $entry.AfterHash=Get-033FileHash $action.Source
                if((Test-033Member $action 'ExpectedAfterHash') -and $entry.AfterHash -ne $action.ExpectedAfterHash){throw 'Source differs from the pinned plan hash'}
                $payloadItem=Get-Item -LiteralPath $action.Source -Force
                $entry.AfterTime=$payloadItem.LastWriteTimeUtc.ToString('o');$entry.AfterAttributes=[int](Get-033SettableAttributes $payloadItem.Attributes)
                if(Test-033Member $action 'Metadata'){
                    $entry.AfterTime=$action.Metadata.Time;$entry.AfterAttributes=$action.Metadata.Attributes
                    [void](ConvertTo-033UtcTime $entry.AfterTime);[void][long]$entry.AfterAttributes
                }
                [IO.File]::Copy($action.Source,(Join-Path $backupRoot $entry.Payload),$false)
                if((Get-033FileHash (Join-Path $backupRoot $entry.Payload)) -ne $entry.AfterHash){throw 'Payload changed while staging'}
            }
            $files+=[pscustomobject]$entry;$index++
        }
        $receipt=Join-Path $backupRoot 'receipt.json'
        $state=[ordered]@{Version=2;Root=$Root;Label=$Label;Status='prepared';CreatedAt=[DateTime]::UtcNow.ToString('o');RestoredAt=$null;Files=$files}
        Write-033Json $receipt $state
        try{
            $state.Status='applying';Write-033Json $receipt $state
            foreach($entry in $files){
                $target=Get-033Path $Root $entry.Path
                if((Get-033FileHash $target) -ne $entry.BeforeHash){throw "External modification before installation: $($entry.Path)"}
                $entry.Phase='writing';Write-033Json $receipt $state
                Invoke-033TransactionStep 'before-write' $receipt $entry.Path
                if((Get-033FileHash $target) -ne $entry.BeforeHash){throw "External modification at write: $($entry.Path)"}
                $source=if($entry.AfterHash){Join-Path $backupRoot $entry.Payload}else{$null}
                Set-033TransactionFile $target $source $entry.AfterHash
                if($entry.AfterHash){[IO.File]::SetLastWriteTimeUtc($target,(ConvertTo-033UtcTime $entry.AfterTime));[IO.File]::SetAttributes($target,(ConvertTo-033FileAttributes $entry.AfterAttributes))}
                Invoke-033TransactionStep 'after-write' $receipt $entry.Path
                $entry.Phase='installed';Write-033Json $receipt $state
                Invoke-033TransactionStep 'after-record' $receipt $entry.Path
            }
            foreach($entry in $files){if((Get-033FileHash (Get-033Path $Root $entry.Path)) -ne $entry.AfterHash){throw 'Installed hash mismatch'}}
            $state.Status='installed';Write-033Json $receipt $state
        }catch{
            $failure=$_.Exception.Message
            try{Restore-033Transaction $receipt -Recover | Out-Null}catch{throw "Install failed: $failure. Recovery blocked: $($_.Exception.Message). Receipt: $receipt"}
            throw "Install failed and exact files restored: $failure. Receipt: $receipt"
        }
        return $receipt
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
