# Schema-1 compatible payload storage. Ledger paths remain exact relative paths.
# All mutations run under the existing vault transaction mutex. No game writes.
Set-StrictMode -Version Latest
function Invoke-033VaultStep([string]$Stage,[string]$Path) {} # CPU fault seam
function Get-033VaultObject([string]$Vault,[string]$Hash){
    if($Hash -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid vault content hash'}
    return Get-033Path $Vault ('objects/'+$Hash.ToLowerInvariant()+'.bin')
}
function Assert-033VaultAlias([string]$Vault,[string]$Target){
    $root=Get-033ManagedRoot $Vault;$full=[IO.Path]::GetFullPath($Target)
    if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Vault alias escaped root'}
    $rel=$full.Substring($root.Length+1).Replace('\','/')
    if($rel -notmatch '^groups/[a-f0-9]{32}/(operations/[a-f0-9]{32}/[0-9]+\.(before|after)|originals/[0-9]+\.bin)$'){throw 'Unsupported vault payload alias'}
    return Get-033Path $root $rel
}
function New-033VaultAlias([string]$Object,[string]$Target,[string]$Hash,[bool]$Replace){
    return [Installer033.VaultLinksV1]::Alias($Object,$Target,$Hash,$Replace)
}
function Copy-033VaultBlob([string]$Vault,[string]$Source,[string]$Target,[string]$Hash,[switch]$Existing){
    $targetPath=Assert-033VaultAlias $Vault $Target
    $object=Get-033VaultObject $Vault $Hash
    if(-not $Existing -and [IO.File]::Exists($targetPath)){throw 'Vault payload already exists'}
    if($Existing -and (Get-033FileHash $targetPath) -ine $Hash){throw 'Old backup changed; migration stopped'}
    if([IO.DriveInfo]::new([IO.Path]::GetPathRoot($Vault)).DriveFormat -ne 'NTFS'){
        if(-not $Existing){Copy-033ManagedBlob $Source $targetPath $Hash}
        return $false
    }
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $object))
    if(-not [IO.File]::Exists($object)){
        $temp=Get-033Path $Vault ('objects/'+[Guid]::NewGuid().ToString('N')+'.part')
        try{
            Copy-033ManagedBlob $Source $temp $Hash
            Invoke-033VaultStep 'object-verified' $temp
            [IO.File]::Move($temp,$object)
            Invoke-033VaultStep 'object-published' $object
        }finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
    }
    Invoke-033VaultStep 'before-alias' $targetPath
    $linked=New-033VaultAlias $object $targetPath $Hash ([bool]$Existing)
    if(-not $linked -and -not $Existing){
        if([IO.DriveInfo]::new([IO.Path]::GetPathRoot($Vault)).AvailableFreeSpace -lt ((Get-Item -LiteralPath $object).Length+1MB)){throw '备份目录不支持本次硬链接且没有足够空间复制；游戏文件尚未改动'}
        Copy-033ManagedBlob $object $targetPath $Hash
    }
    Invoke-033VaultStep 'after-alias' $targetPath
    return $linked
}
function Add-033VaultReferences($Value,[string]$Folder,$Refs,$Protected){
    if($null -eq $Value){return}
    if($Value -is [Collections.IDictionary]){
        foreach($pair in @(@('OriginalBlob','Original'),@('BeforeBlob','Before'),@('AfterBlob','After'))){
            if($Value.ContainsKey($pair[0]) -and $Value[$pair[0]]){
                $rel=[string]$Value[$pair[0]];$p=Get-033Path $Folder $rel
                if(-not $Value.ContainsKey($pair[1]) -or -not $Value[$pair[1]] -or -not $Value[$pair[1]].Hash){throw 'Blob reference has no recorded hash'}
                $hash=[string]$Value[$pair[1]].Hash
                if($hash -notmatch '^[a-fA-F0-9]{64}$'){throw 'Invalid recorded blob hash'}
                if($Refs.ContainsKey($p) -and $Refs[$p] -ine $hash){throw 'Conflicting backup hashes; no cleanup'}
                $Refs[$p]=$hash
                # Keep all before images, first originals and superseded originals.
                # These also protect unusual legacy references to an .after path.
                if($pair[0] -ne 'AfterBlob'){$Protected[$p]=$true}
            }
        }
        foreach($v in $Value.Values){Add-033VaultReferences $v $Folder $Refs $Protected}
    }elseif($Value -is [Collections.IEnumerable] -and $Value -isnot [string]){
        foreach($v in $Value){Add-033VaultReferences $v $Folder $Refs $Protected}
    }
}
function Get-033VaultGroupPlan([string]$Vault,[string]$Group){
    if($Group -notmatch '^[a-f0-9]{32}$'){throw 'Invalid vault group'}
    $folder=Get-033Path $Vault ('groups/'+$Group)
    $statePath=Get-033Path $folder 'state.json';$pendingPath=Get-033Path $folder 'pending.json'
    $refs=@{};$protected=@{};$prune=@{};$migrate=@();$docs=@();$pending=$null;$checked=@{}
    if([IO.File]::Exists($statePath)){
        $state=Read-033ManagedJson $statePath
        if($state.Schema -ne 1 -or $state.Id -ine $Group -or $state.Vault -ine $Vault){throw 'Vault state identity mismatch'}
        $docs+=@($state)
    }else{return @{Migrate=@();Prune=@();Skipped='no-state'}}
    if([IO.File]::Exists($pendingPath)){
        $pending=Read-033ManagedJson $pendingPath
        if($pending.Status -notin @('committed','recovered')){return @{Migrate=@();Prune=@();Skipped='unfinished-operation'}}
        $docs+=@($pending)
    }
    $operations=Get-033Path $folder 'operations'
    if([IO.Directory]::Exists($operations)){
        foreach($op in @(Get-ChildItem -LiteralPath $operations -Directory -Force)){
            if($op.Name -notmatch '^[a-f0-9]{32}$'){continue}
            $opPath=Get-033Path $folder ('operations/'+$op.Name)
            foreach($name in @('receipt.json','previous-journal.json')){
                $rp=Get-033Path $opPath $name
                if(-not [IO.File]::Exists($rp)){continue}
                $j=Read-033ManagedJson $rp;$docs+=@($j)
                if($name -ne 'receipt.json' -or $j.Schema -ne 1 -or $j.Status -notin @('committed','recovered') -or $j.Operation -cne ('operations/'+$op.Name) -or $j.Group.Id -ine $Group){continue}
                # A current commit with a missing/mismatched receipt still needs Recover.
                if($pending -and $pending.Operation -ceq $j.Operation -and (Get-033FileHash $rp) -ine (Get-033FileHash $pendingPath)){continue}
                foreach($f in $j.Files){
                    if(-not $f.AfterBlob){continue}
                    $rel=[string]$f.AfterBlob
                    if($rel -notmatch ('^operations/'+$op.Name+'/[0-9]+\.after$')){continue}
                    $p=Get-033Path $folder $rel;$prune[$p]=[string]$f.After.Hash
                }
            }
        }
    }
    foreach($doc in $docs){Add-033VaultReferences $doc $folder $refs $protected}
    foreach($p in @($prune.Keys)){if($protected.ContainsKey($p)){[void]$prune.Remove($p)}}
    # Validate the complete plan before altering even one backup.
    foreach($p in @($refs.Keys)){
        if(-not [IO.File]::Exists($p)){
            if($protected.ContainsKey($p)){throw ('Required backup missing: '+$p)}
            continue # Already pruned committed .after, or unused terminal payload.
        }
        $identity=[Installer033.FileIdentityV1]::Identity($p)
        if(-not $checked.ContainsKey($identity)){$checked[$identity]=Get-033FileHash $p}
        if($checked[$identity] -ine $refs[$p]){throw ('Backup hash mismatch: '+$p)}
        $rel=$p.Substring($folder.Length+1).Replace('\','/')
        if($rel -match '^(operations/[a-f0-9]{32}/[0-9]+\.(before|after)|originals/[0-9]+\.bin)$' -and -not $prune.ContainsKey($p)){
            $migrate+=@(@{Path=$p;Hash=$refs[$p]})
        }
    }
    return @{Migrate=$migrate;Prune=@($prune.Keys|Where-Object {[IO.File]::Exists($_)}|ForEach-Object {@{Path=$_;Hash=$prune[$_]}});Skipped=$null}
}
function Invoke-033VaultMaintenanceLocked([string]$Vault,[string]$Group,[switch]$PlanOnly){
    $Vault=Get-033ManagedRoot $Vault
    $groups=Get-033Path $Vault 'groups';$plans=@();$linked=0;$fallback=0;$pruned=0;$collected=0;$skipped=@()
    $names=if($Group){@($Group)}elseif([IO.Directory]::Exists($groups)){@(Get-ChildItem -LiteralPath $groups -Directory -Force|Where-Object {$_.Name -match '^[a-f0-9]{32}$'}|ForEach-Object {$_.Name})}else{@()}
    foreach($name in $names){$plan=Get-033VaultGroupPlan $Vault $name;$plans+=@($plan);if($plan.Skipped){$skipped+=@($name+':'+$plan.Skipped)}}
    $before=Get-033VaultUsage $Vault
    if(-not $PlanOnly){
        foreach($plan in $plans){
            foreach($e in $plan.Migrate){
                $object=Get-033VaultObject $Vault $e.Hash
                if([IO.File]::Exists($object) -and [Installer033.FileIdentityV1]::Identity($object) -eq [Installer033.FileIdentityV1]::Identity($e.Path)){continue}
                if(Copy-033VaultBlob $Vault $e.Path $e.Path $e.Hash -Existing){$linked++}else{$fallback++}
            }
            foreach($e in $plan.Prune){
                $p=Assert-033VaultAlias $Vault $e.Path
                if((Get-033FileHash $p) -ine $e.Hash){throw 'Payload changed during cleanup'}
                Invoke-033VaultStep 'before-prune' $p
                [Installer033.VaultLinksV1]::DeleteVerified($p,$e.Hash);$pruned++
                Invoke-033VaultStep 'after-prune' $p
            }
        }
        $objects=Get-033Path $Vault 'objects'
        if([IO.Directory]::Exists($objects)){
            foreach($f in @(Get-ChildItem -LiteralPath $objects -File -Force)){
                if($f.Name -notmatch '^([a-f0-9]{64})\.bin$'){continue}
                $expected=$Matches[1];$p=Get-033Path $objects $f.Name
                # NTFS link count is the actual reference count, not a cached index.
                # No ledger ever points at canonical object names, only its aliases.
                if([Installer033.FileIdentityV1]::LinkCount($p) -ne 1){continue}
                if((Get-033FileHash $p) -ine $expected){throw 'Orphan object hash mismatch; preserved'}
                Invoke-033VaultStep 'before-gc' $p
                [Installer033.VaultLinksV1]::DeleteVerified($p,$expected);$collected++
            }
        }
    }
    $after=Get-033VaultUsage $Vault
    return @{Status=$(if($PlanOnly){'planned'}else{'compacted'});PlannedBackupPaths=@($plans|ForEach-Object {$_.Migrate}).Count;PlannedAfterPrunes=@($plans|ForEach-Object {$_.Prune}).Count;Linked=$linked;CopyFallback=$fallback;PrunedAfter=$pruned;CollectedObjects=$collected;Skipped=$skipped;Before=$before;After=$after;ReclaimedBytes=([long]$before.UniqueFileBytes-[long]$after.UniqueFileBytes);GameWrites=0}
}
function Get-033VaultUsage([string]$Vault){
    $seen=@{};$logical=[long]0;$unique=[long]0;$count=0
    if([IO.Directory]::Exists($Vault)){
        # Walk one directory at a time; never recurse through a junction.
        $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($Vault)
        while($queue.Count){
            $dir=$queue.Dequeue()
            foreach($f in @(Get-ChildItem -LiteralPath $dir -Force)){
                $p=Get-033Path $Vault ($f.FullName.Substring($Vault.Length+1))
                if($f.PSIsContainer){$queue.Enqueue($p);continue}
                $id=[Installer033.FileIdentityV1]::Identity($p);$logical+=$f.Length;$count++
                if(-not $seen.ContainsKey($id)){$unique+=$f.Length;$seen[$id]=$true}
            }
        }
    }
    return @{Files=$count;LogicalBytes=$logical;UniqueFileBytes=$unique;Note='File identity deduplicated byte count; filesystem allocation and compression excluded'}
}
function Invoke-033VaultMaintenance([string]$Vault,[switch]$PlanOnly){
    $Vault=Get-033ManagedRoot $Vault;$mutex=Enter-033TransactionLock $Vault
    try{return Invoke-033VaultMaintenanceLocked $Vault -PlanOnly:$PlanOnly}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
function Complete-033VaultStorage([string]$Vault,[string]$Group){
    try{return Invoke-033VaultMaintenanceLocked $Vault $Group}
    catch{return @{Status='deferred';Warning=('文件事务已完成，备份整理未完成，原备份保留，可再次整理：'+$_.Exception.Message)}}
}
function Get-033VaultSpacePlan([string]$Vault,$Plan,[string]$OldFolder){
    $needs=@{};$objects=@{};$vaultDrive=[IO.Path]::GetPathRoot($Vault)
    $canLink=([IO.DriveInfo]::new($vaultDrive).DriveFormat -eq 'NTFS')
    $needs[$vaultDrive]=[long]0
    foreach($a in $Plan.Actions){
        $root=[string]$Plan.Targets[$a.Root].Root;$drive=[IO.Path]::GetPathRoot($root)
        if(-not $needs.ContainsKey($drive)){$needs[$drive]=[long]0}
        $payloads=@()
        if($a.Before.Exists){$p=Get-033Path $root $a.Path;$payloads+=@(@{Hash=$a.Before.Hash;Bytes=[long](Get-Item -LiteralPath $p).Length})}
        if($a.After.Exists -and -not ($a.ContainsKey('NoWrite') -and $a.NoWrite)){
            if($a.ContainsKey('GeneratedBytes')){$size=[long]$a.GeneratedBytes.Length}
            else{
                $source=if($a.ContainsKey('FromVault') -and $a.FromVault){Get-033Path $OldFolder $a.Source}else{$a.Source}
                $size=[long](Get-Item -LiteralPath $source).Length
            }
            $needs[$drive]+=$size # Independent game files/staging; never hard links.
            $payloads+=@(@{Hash=$a.After.Hash;Bytes=$size})
        }
        foreach($p in $payloads){
            if(-not $canLink){$needs[$vaultDrive]+=$p.Bytes;continue}
            if(-not $objects.ContainsKey($p.Hash)){
                $obj=Get-033VaultObject $Vault $p.Hash;$links=1
                if([IO.File]::Exists($obj)){
                    if((Get-033FileHash $obj) -ine $p.Hash){throw 'Existing vault object is corrupt; game files unchanged'}
                    $links=[Installer033.FileIdentityV1]::LinkCount($obj)
                }else{$needs[$vaultDrive]+=$p.Bytes}
                $objects[$p.Hash]=$links
            }
            if($objects[$p.Hash] -ge 1023){$needs[$vaultDrive]+=$p.Bytes}else{$objects[$p.Hash]++}
        }
    }
    return ,$needs
}
