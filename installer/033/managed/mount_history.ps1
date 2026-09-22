# A previous installation is evidence of a chosen mount, not game acceptance.
# Read only bounded data. Never interpret a record value as a path or command.
function Get-033LegacyMountHistory([string]$Record,[string]$Exe,[string]$Root){
    try{
        if(-not (Test-Path -LiteralPath $Record -PathType Leaf) -or (Get-Item -LiteralPath $Record).Length -gt 65536){return $null}
        $kv=@{}
        foreach($line in [IO.File]::ReadAllLines($Record,[Text.Encoding]::UTF8)){
            if($line -match '^(proxy|proxyall|exe)=(.*)$'){
                if($kv.ContainsKey($Matches[1])){return $null}
                $kv[$Matches[1]]=$Matches[2].Trim()
            }
        }
        if(-not $kv.ContainsKey('exe') -or $kv.exe -ine [IO.Path]::GetFileName($Exe)){return $null}
        if(-not $kv.ContainsKey('proxy') -or -not $kv.proxy){return $null}
        $name=Assert-033ProxyName $kv.proxy
        if($kv.ContainsKey('proxyall') -and $kv.proxyall -and $kv.proxyall -ine $name){return $null}
        $path=Join-Path $Root $name
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){return $null}
        $pe=Get-033PeInfo $path;$exePe=Get-033PeInfo $Exe
        if(-not $pe.IsDll -or $pe.Architecture -notin @('x86','x64') -or $pe.Architecture -ine $exePe.Architecture){return $null}
        if((Get-033ProxyIdentity $path).Kind -notin @('033-fork','reshade')){return $null}
        return @{Proxy=$name;Exe=[IO.Path]::GetFullPath($Exe);Root=[IO.Path]::GetFullPath($Root);Source='legacy-record';RecordSHA256=(Get-033FileHash $Record);EntrySHA256=(Get-033FileHash $path)}
    }catch{return $null}
}
function Get-033RetainedAutoMount([string]$Exe,$Route,[string]$Root,[string]$PackageRoot,$Survey=$null,$OldState=$null){
    # Fixed game/engine routes keep their reviewed component chains.
    if(-not $Route -or ($Route.ContainsKey('ExecutableNames') -and @($Route.ExecutableNames).Count)){return $null}
    if($Route.ContainsKey('Suitability') -and $Route.Suitability -and $Route.Suitability.ContainsKey('EngineMarkers') -and @($Route.Suitability.EngineMarkers).Count){return $null}
    $entry=Get-033RouteEntry $Route;if(-not $entry){return $null}
    $history=$null
    if(-not $OldState -and $Survey -and $Survey.Existing -and $Survey.Existing.Kind -eq 'managed'){$OldState=$Survey.Existing.Managed.State}
    if($OldState -and $OldState.Status -eq 'installed'){
        $previous=@($OldState.Targets|Where-Object {$_.Exe -ieq $Exe -and $_.Root -ieq $Root -and $_.Profile -ceq $Route.Id -and $_.ContainsKey('Mount')})
        if($previous.Count -eq 1){
            try{
                $name=Assert-033ProxyName ([string]$previous[0].Mount) $entry.Names
                $index=-1;for($i=0;$i -lt @($OldState.Targets).Count;$i++){if($OldState.Targets[$i].Exe -ieq $Exe){$index=$i;break}}
                $owned=@($OldState.Entries|Where-Object {$_.Root -eq $index -and $_.Path -ieq $name})
                $path=Join-Path $Root $name
                if($owned.Count -eq 1 -and (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-033FileHash $path) -ieq [string]$owned[0].Current.Hash){
                    $history=@{Proxy=$name;Exe=$Exe;Root=$Root;Source='managed-mount'}
                }
            }catch{}
        }
    }
    if(-not $history -and $Survey){
        if($Survey.PSObject.Properties['PreviousMount']){$history=$Survey.PreviousMount}
        elseif($Survey.Existing -and $Survey.Existing.Kind -eq 'old-package' -and $Survey.Existing.PSObject.Properties['LegacyMount']){$history=$Survey.Existing.LegacyMount}
    }
    if(-not $history -or $history.Exe -ine $Exe -or $history.Root -ine $Root){return $null}
    try{
        $name=Assert-033ProxyName ([string]$history.Proxy) $entry.Names
        [void](Set-033ProxyMount $Route $name)
        Assert-033ProxyAllowedHere $Root $Exe $name
        Assert-033ProxyNotForeign $Root $name
        $exports=Get-033PeExportNames (Get-033Path $PackageRoot ([string]$entry.File.Source))
        $imports=Get-033PeImportedFunctions $Exe
        if(-not (Test-033ProxyCanStandIn $name $imports $exports).Ok){return $null}
        return @{Proxy=$name;History=$history;Warning=('沿用上次安装记录的挂载名 '+$name+'。')}
    }catch{return $null}
}
