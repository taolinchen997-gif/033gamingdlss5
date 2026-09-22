# Import only caller-selected, hash-pinned chronological v2 transaction receipts.
# A filename, old marker, backup directory or current DLL is NOT original ownership.
function Import-033ManagedHistory {
    param([string[]]$GameExe,[string[]]$Receipts,[string[]]$ReceiptHashes,
          [string]$Vault=(Join-Path $env:LOCALAPPDATA '033Installer'),[switch]$PlanOnly)
    if(-not $GameExe.Count -or $GameExe.Count -gt 16){throw '迁移必须指定1至16个准确入口'}
    if(-not $Receipts.Count -or $Receipts.Count -ne $ReceiptHashes.Count){throw '必须逐份指定原事务收据及其已核验SHA256'}
    $Vault=Get-033ManagedRoot $Vault;Assert-033ManagedVaultCapacity $Vault;$targets=@();$byRoot=@{}
    foreach($exe in @($GameExe|ForEach-Object {[IO.Path]::GetFullPath($_)}|Sort-Object -Unique)){
        $root=Get-033ManagedRoot (Split-Path -Parent $exe)
        if($byRoot.ContainsKey($root)){throw '每个目录只允许一个准确入口'}
        $pe=Get-033PeInfo $exe;if($pe.Status -ne 'valid' -or $pe.IsDll){throw '迁移目标不是有效EXE'}
        if($Vault -ieq $root -or $Vault.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or $root.StartsWith($Vault+'\',[StringComparison]::OrdinalIgnoreCase)){throw '迁移备份库与目标重叠'}
        if(Read-033ManagedGroup $Vault $exe){throw '该入口已有新账本，禁止覆盖导入'}
        $byRoot[$root]=$targets.Count;$targets+=@(@{Root=$root;Exe=$exe;Hash=Get-033FileHash $exe;Profile='imported-exact-history'})
    }
    $group=@{Schema=1;Id=[Guid]::NewGuid().ToString('N');Vault=$Vault;Targets=$targets;Entries=@();Version='imported';PackageHash=$null;Status='installed';CreatedDirectories=@();ImportedReceipts=@()}
    $owned=@{};$sources=@{};$receiptSources=@();$order=@{}
    for($i=0;$i -lt $Receipts.Count;$i++){
        $path=[IO.Path]::GetFullPath($Receipts[$i]);$hash=Get-033FileHash $path
        if($ReceiptHashes[$i] -notmatch '^[A-Fa-f0-9]{64}$' -or $hash -ine $ReceiptHashes[$i]){throw '历史事务收据哈希不符'}
        $r=Read-033ManagedJson $path
        if($r.Version -ne 2 -or $r.Status -ne 'installed' -or -not $byRoot.ContainsKey($r.Root)){throw '只接受准确目标的已安装v2事务；不推测失败或未完成记录'}
        if(-not $path.StartsWith((Get-033Path $r.Root '_033transactions')+'\',[StringComparison]::OrdinalIgnoreCase)){throw '历史收据不在其准确事务目录'}
        $time=(ConvertTo-033UtcTime $r.CreatedAt).Ticks
        if($order.ContainsKey($r.Root) -and $time -lt $order[$r.Root]){throw '历史收据必须按安装时间由旧到新提供'};$order[$r.Root]=$time
        $receiptSources+=@(@{Path=$path;Hash=$hash});$ri=$byRoot[$r.Root]
        foreach($f in $r.Files){
            $key=$ri.ToString()+'|'+$f.Path.ToLowerInvariant()
            [void](Assert-033ManagedEntry $group @{Root=$ri;Path=$f.Path})
            if($owned.ContainsKey($key)){
                if($owned[$key].LastHash -ine $f.BeforeHash){throw "历史链有缺口，无法证明首次原件: $($f.Path)"}
            }else{
                $original=@{Exists=[bool]$f.Existed;Hash=$f.BeforeHash;Identity=$null;Attributes=[int]$f.BeforeAttributes;Time=$f.BeforeTime}
                if($original.Exists){
                    $source=Get-033Path (Split-Path -Parent $path) $f.Backup
                    if(-not $original.Hash -or (Get-033FileHash $source) -ine $original.Hash){throw "最初备份缺失/损坏: $($f.Path)"}
                    $sources[$key]=$source
                }elseif($original.Hash){throw '无效原始缺失记录'}
                $owned[$key]=@{Root=$ri;Path=$f.Path;Original=$original;OriginalBlob=$null;Current=$null;Policy='replace';LastHash=$null}
            }
            $owned[$key].LastHash=$f.AfterHash
        }
    }
    foreach($key in @($owned.Keys|Sort-Object)){
        $e=$owned[$key];$now=Get-033ManagedSnapshot $targets[$e.Root].Root $e.Path
        if($now.Hash -ine $e.LastHash){throw "当前文件不匹配历史链末端: $($e.Path)"}
        $e.Current=$now;$e.Remove('LastHash');$group.Entries+=@($e)
    }
    if($PlanOnly){return @{Status='validated-history';Targets=$targets;Files=$group.Entries.Count;Receipts=$receiptSources;GameWrites=0}}
    $mutex=Enter-033TransactionLock $Vault;$gates=$null
    try{
        foreach($t in $targets){if(Read-033ManagedGroup $Vault $t.Exe){throw '索引在迁移前变化'}}
        $gates=Open-033ManagedGates $group
        foreach($e in $group.Entries){if(-not(Test-033ManagedSnapshot (Get-033ManagedSnapshot $targets[$e.Root].Root $e.Path) $e.Current)){throw '导入前现场变化'}}
        $folder=Get-033Path $Vault ('groups/'+$group.Id);[void][IO.Directory]::CreateDirectory($folder)
        [void][IO.Directory]::CreateDirectory((Get-033Path $folder 'originals'))
        [void][IO.Directory]::CreateDirectory((Get-033Path $folder 'history'))
        $j=0
        foreach($e in $group.Entries){
            if($e.Original.Exists){$key=$e.Root.ToString()+'|'+$e.Path.ToLowerInvariant();$e.OriginalBlob='originals/'+$j+'.bin';[void](Copy-033VaultBlob $Vault $sources[$key] (Get-033Path $folder $e.OriginalBlob) $e.Original.Hash)};$j++
        }
        $j=0;foreach($r in $receiptSources){$rel='history/'+$j+'.json';Copy-033ManagedBlob $r.Path (Get-033Path $folder $rel) $r.Hash;$group.ImportedReceipts+=@(@{Path=$rel;Hash=$r.Hash});$j++}
        Write-033Json (Get-033Path $folder 'state.json') $group
        [void][IO.Directory]::CreateDirectory((Get-033Path $Vault 'index'))
        foreach($t in $targets){Write-033Json (Get-033ManagedIndex $Vault $t.Exe) @{Schema=1;Exe=$t.Exe;Group=$group.Id}}
        return @{Status='imported';Group=$group.Id;Files=$group.Entries.Count;Targets=$targets.Exe;Vault=$Vault;GameWrites=0;Message='原事务链与首次备份已校验并导入；未改游戏文件。'}
    }finally{Close-033ManagedGates $gates;$mutex.ReleaseMutex();$mutex.Dispose()}
}
