# Exact per-file transactions for the candidate installer and remount undo.
# This module never launches games and never chooses a replacement during undo.
Set-StrictMode -Version Latest
function Get-033Path([string]$Root,[string]$Relative) {
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if([IO.Path]::IsPathRooted($Relative) -or ($Relative -split '[\\/]') -contains '..'){throw "Invalid relative path: $Relative"}
    $path=[IO.Path]::GetFullPath((Join-Path $base $Relative))
    if(-not $path.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escaped target root'}
    $cursor=$path
    while($cursor.Length -ge $base.Length){
        if(Test-Path -LiteralPath $cursor){$item=Get-Item -LiteralPath $cursor -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point unsupported: $cursor"}}
        if($cursor -eq $base){break};$cursor=Split-Path -Parent $cursor
    }
    return $path
}
function Write-033Json([string]$Path,$Value){
    $temp=$Path+'.tmp'
    [IO.File]::WriteAllText($temp,($Value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $Path -Force
}
function Restore-033Transaction([string]$Receipt,[switch]$Recover) {
    $receiptPath=[IO.Path]::GetFullPath($Receipt)
    $state=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if($state.Version -ne 1){throw 'Unknown transaction version'}
    if($state.Status -eq 'restored'){return $state}
    $backupRoot=Split-Path -Parent $receiptPath
    # Preflight everything before changing anything; a user's later edit is not
    # overwritten merely because an older candidate once owned that filename.
    foreach($entry in $state.Files){
        $target=Get-033Path $state.Root $entry.Path
        if($entry.Existed){
            $backup=Get-033Path $backupRoot $entry.Backup
            if((Get-FileHash -LiteralPath $backup).Hash -ne $entry.BeforeHash){throw "Backup changed: $($entry.Path)"}
        }
        if(-not $Recover){
            $exists=Test-Path -LiteralPath $target -PathType Leaf
            if($entry.AfterHash){if(-not $exists -or (Get-FileHash -LiteralPath $target).Hash -ne $entry.AfterHash){throw "File changed after installation: $($entry.Path)"}}
            elseif($exists){throw "A new file now occupies: $($entry.Path)"}
        }
    }
    foreach($entry in $state.Files){
        $target=Get-033Path $state.Root $entry.Path
        if($entry.Existed){
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Get-033Path $backupRoot $entry.Backup) -Destination $target -Force
            [IO.File]::SetLastWriteTimeUtc($target,[DateTime]::Parse($entry.BeforeTime).ToUniversalTime())
            [IO.File]::SetAttributes($target,[IO.FileAttributes]$entry.BeforeAttributes)
        }elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
    }
    $state.Status='restored';$state.RestoredAt=[DateTime]::UtcNow.ToString('o');Write-033Json $receiptPath $state;return $state
}
function Invoke-033Transaction([string]$Root,[object[]]$Plan,[string]$Label='candidate') {
    $Root=[IO.Path]::GetFullPath($Root)
    if(-not (Test-Path -LiteralPath $Root -PathType Container)){throw 'Target root missing'}
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($action in $Plan){
        $target=Get-033Path $Root $action.Path
        if(-not $seen.Add($target)){throw "Duplicate target: $target"}
        if($action.Source -and -not (Test-Path -LiteralPath $action.Source -PathType Leaf)){throw "Source missing: $($action.Source)"}
        if(Test-Path -LiteralPath $target -PathType Container){throw "Target is a directory: $target"}
    }
    $relative='_033transactions\'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[Guid]::NewGuid().ToString('N')
    $backupRoot=Get-033Path $Root $relative
    New-Item -ItemType Directory -Path $backupRoot | Out-Null
    $files=@();$index=0
    foreach($action in $Plan){
        $target=Get-033Path $Root $action.Path;$exists=Test-Path -LiteralPath $target -PathType Leaf
        $entry=[ordered]@{Path=$action.Path;Existed=$exists;Backup=('{0:D4}.bin' -f $index);Payload=('{0:D4}.after' -f $index);BeforeHash=$null;BeforeTime=$null;BeforeAttributes=0;AfterHash=$null}
        if($exists){
            $item=Get-Item -LiteralPath $target -Force;$entry.BeforeHash=(Get-FileHash -LiteralPath $target).Hash
            $entry.BeforeTime=$item.LastWriteTimeUtc.ToString('o');$entry.BeforeAttributes=[int]$item.Attributes
            Copy-Item -LiteralPath $target -Destination (Join-Path $backupRoot $entry.Backup)
            if((Get-FileHash -LiteralPath (Join-Path $backupRoot $entry.Backup)).Hash -ne $entry.BeforeHash){throw 'Backup verification failed'}
        }
        if($action.Source){
            $entry.AfterHash=(Get-FileHash -LiteralPath $action.Source).Hash
            Copy-Item -LiteralPath $action.Source -Destination (Join-Path $backupRoot $entry.Payload)
            if((Get-FileHash -LiteralPath (Join-Path $backupRoot $entry.Payload)).Hash -ne $entry.AfterHash){throw 'Payload changed while staging'}
        }
        $files+=[pscustomobject]$entry;$index++
    }
    $receipt=Join-Path $backupRoot 'receipt.json'
    $state=[ordered]@{Version=1;Root=$Root;Label=$Label;Status='prepared';CreatedAt=[DateTime]::UtcNow.ToString('o');RestoredAt=$null;Files=$files}
    Write-033Json $receipt $state
    try {
        foreach($entry in $files){
            $target=Get-033Path $Root $entry.Path
            if($entry.AfterHash){New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null;Copy-Item -LiteralPath (Join-Path $backupRoot $entry.Payload) -Destination $target -Force}
            elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
        }
        foreach($entry in $files){$target=Get-033Path $Root $entry.Path
            if($entry.AfterHash -and (Get-FileHash -LiteralPath $target).Hash -ne $entry.AfterHash){throw 'Installed hash mismatch'}}
        $state.Status='installed';Write-033Json $receipt $state
    }catch{Restore-033Transaction $receipt -Recover | Out-Null;throw}
    return $receipt
}
