# Only package-declared 033 pipeline keys are migrated. Plan computes bytes in
# memory; the managed transaction preserves originals and journals the change.
function Get-033RouteIniValue([string]$Text,[string]$Section,[string]$Key){
    $active='';$value=''
    foreach($line in [regex]::Split($Text,'\r\n|\n|\r')){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$active=$Matches[1];continue}
        if($active -ieq $Section -and $line -match '^\s*([^;#=][^=]*?)\s*=(.*)$' -and $Matches[1].Trim() -ieq $Key){$value=$Matches[2].Trim()}
    }
    return $value
}
function Set-033RouteIniValue([string]$Text,[string]$Section,[string]$Key,[string]$Value){
    # A semantically unchanged, unique key must keep its original bytes. This
    # includes mixed line endings written by the host after an older install.
    # Duplicates still go through the canonical merge, even if their values match.
    $probeSection='';$probeCount=0;$probeSame=$false
    foreach($probeLine in [regex]::Split($Text,'\r\n|\n|\r')){
        if($probeLine -match '^\s*\[([^\]]+)\]\s*$'){$probeSection=$Matches[1];continue}
        if($probeSection -ieq $Section -and $probeLine -match '^\s*([^;#=][^=]*?)\s*=(.*)$' -and $Matches[1].Trim() -ieq $Key){
            $probeCount++;$probeSame=($Matches[2].Trim() -ceq $Value)
        }
    }
    if($probeCount -eq 1 -and $probeSame){return $Text}
    $nl=$(if($Text.Contains("`r`n")){"`r`n"}else{"`n"})
    $lines=[Collections.Generic.List[string]]::new();$active='';$wrote=$false;$sectionFound=($Section -eq '')
    foreach($line in [regex]::Split($Text,'\r\n|\n|\r')){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){
            if($active -ieq $Section -and -not $wrote){$lines.Add($Key+'='+$Value);$wrote=$true}
            $active=$Matches[1];if($active -ieq $Section){$sectionFound=$true}
        }
        if($active -ieq $Section -and $line -match '^\s*([^;#=][^=]*?)\s*=(.*)$' -and $Matches[1].Trim() -ieq $Key){
            if(-not $wrote){$lines.Add($Key+'='+$Value);$wrote=$true};continue
        }
        $lines.Add($line)
    }
    if(-not $wrote){if(-not $sectionFound){$lines.Add('['+$Section+']')};$lines.Add($Key+'='+$Value)}
    return ($lines -join $nl)
}
function Merge-033RouteDefinitions([string]$Current,[string]$Required){
    $items=@($Current -split ','|Where-Object {$_ -and $_ -notmatch '^\s*(DLSS5_MV_PROVIDER|IMAGE_SPACE)\s*='})
    $items+=@($Required -split ','|Where-Object {$_ -match '^\s*(DLSS5_MV_PROVIDER|IMAGE_SPACE)\s*='})
    return ($items -join ',')
}
function Merge-033RouteSeedText([string]$Current,[string]$Required,[string]$Kind,[bool]$RequiresFeeder=$false){
    $text=$Current
    if($Kind -eq '033-host-v1'){
        # Always use our route's owned preset. Never modify a foreign/external
        # preset selected in an old host config or follow its path as a write target.
        $text=Set-033RouteIniValue $text 'GENERAL' 'PresetPath' (Get-033RouteIniValue $Required 'GENERAL' 'PresetPath')
        foreach($key in @('EffectSearchPaths','TextureSearchPaths')){
            $want=@((Get-033RouteIniValue $Required 'GENERAL' $key) -split ','|Where-Object {$_})
            $have=@((Get-033RouteIniValue $text 'GENERAL' $key) -split ','|Where-Object {$_})
            foreach($p in $want){if($have -inotcontains $p){$have+=@($p)}}
            $text=Set-033RouteIniValue $text 'GENERAL' $key ($have -join ',')
        }
        $oldDefs=Get-033RouteIniValue $text 'GENERAL' 'PreprocessorDefinitions'
        $defs=Merge-033RouteDefinitions $oldDefs (Get-033RouteIniValue $Required 'GENERAL' 'PreprocessorDefinitions')
        if($defs -or $oldDefs){$text=Set-033RouteIniValue $text 'GENERAL' 'PreprocessorDefinitions' $defs}
        # Screenshot belongs to 033; obsolete PrintScreen/F10 bindings must not
        # survive the Shift+F10 product migration just because the INI existed.
        $text=Set-033RouteIniValue $text 'INPUT' 'KeyScreenshot' (Get-033RouteIniValue $Required 'INPUT' 'KeyScreenshot')
        $text=Enable-033RouteRequiredAddons $text $RequiresFeeder
    }elseif($Kind -eq '033-preset-v1'){
        foreach($key in @('Techniques','TechniqueSorting')){
            $requiredList=@((Get-033RouteIniValue $Required '' $key) -split ','|Where-Object {$_})
            $foreign=@((Get-033RouteIniValue $text '' $key) -split ','|Where-Object {$_ -and $_.Trim() -notmatch '^(?i:Sign033|Lumenite_Kernel|DLSS5_Feed|DLSS5_Feed_Debug)(?:@(?:Signature033|lumenite_Kernel|DLSS5_Feed)\.fx)?$'})
            $text=Set-033RouteIniValue $text '' $key ((@($requiredList)+@($foreign)) -join ',')
        }
        $oldDefs=Get-033RouteIniValue $text '' 'PreprocessorDefinitions'
        $defs=Merge-033RouteDefinitions $oldDefs (Get-033RouteIniValue $Required '' 'PreprocessorDefinitions')
        if($defs -or $oldDefs){$text=Set-033RouteIniValue $text '' 'PreprocessorDefinitions' $defs}
        # An effect-scoped old provider override takes precedence over the root
        # definitions. Remove only those two owned overrides, keeping user values.
        foreach($section in @('DLSS5_Feed.fx','lumenite_Kernel.fx')){
            $before=Get-033RouteIniValue $text $section 'PreprocessorDefinitions'
            if($before -match '(?i)(DLSS5_MV_PROVIDER|IMAGE_SPACE)\s*='){
                $text=Set-033RouteIniValue $text $section 'PreprocessorDefinitions' (Merge-033RouteDefinitions $before '')
            }
        }
    }elseif($Kind -eq '033-mfg-v1'){
        # One NR owner and one panel. Preserve multiplier/FGMode and all other settings.
        $text=Set-033RouteIniValue $text 'Setting' 'EnableDlssNr' '0'
        $text=Set-033RouteIniValue $text 'Debug' 'EnableFrameGenOverrideMenu' '0'
        $text=Set-033RouteIniValue $text 'Debug' 'EnableUiMaskCalibration' '0'
    }elseif($Kind -eq '033-opti-v1'){
        # OptiScaler 官方共存协议：由它加载 ReShade64.dll；033 做 NR，所以关掉它自己的 DlssNr。其余键一个不碰。
        $text=Set-033RouteIniValue $text 'Plugins' 'LoadReshade' 'true'
        $text=Set-033RouteIniValue $text 'DlssNr' 'Enabled' 'false'
    }else{throw 'Unknown 033 route seed merge kind'}
    return $text
}
function Get-033RouteSeedMerge($File,[string]$Root,[string]$PackageRoot,$Current,$Profile=$null){
    if(-not $File.ContainsKey('SeedMerge')){return $null}
    $path=Get-033Path $Root $File.Target;$source=Get-033Path $PackageRoot $File.Source
    $required=[IO.File]::ReadAllText($source)
    $old=$(if($Current.Exists){[IO.File]::ReadAllText($path)}else{$required})
    $basis=$old;$readInput=$null
    if($File.SeedMerge -ceq '033-preset-v1'){
        $active=Get-033RouteActivePreset $path
        if($active){$basis=Merge-033RouteMissingValues $old $active.Text;$readInput=$active.Input}
    }
    $requiresFeeder=$false
    if($Profile -and $File.SeedMerge -ceq '033-host-v1'){
        $relative=([string]$File.Target).Replace('\','/');$prefix=$relative.Substring(0,$relative.Length-'ReShade.ini'.Length)
        $candidates=@(($prefix+'033-runtime/dlss5-feed.addon32'),($prefix+'033-runtime/dlss5-feed.addon64'))
        $requiresFeeder=@($Profile.Files|Where-Object {([string]$_.Target).Replace('\','/') -iin $candidates}).Count -gt 0
    }
    $merged=Merge-033RouteSeedText $basis $required ([string]$File.SeedMerge) $requiresFeeder
    if($merged -ceq $old){return $null}
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes($merged)
    $h=[Security.Cryptography.SHA256]::Create()
    try{$hash=([BitConverter]::ToString($h.ComputeHash($bytes))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}
    $after=$(if($Current.Exists){Copy-033ManagedObject $Current}else{Get-033ManagedSnapshot $PackageRoot $File.Source})
    $after.Hash=$hash;$after.Identity=$null
    return @{Bytes=$bytes;After=$after;ReadInput=$readInput}
}
function Enable-033RouteRequiredAddons([string]$Text,[bool]$RequiresFeeder){
    $value=Get-033RouteIniValue $Text 'ADDON' 'DisabledAddons'
    if(-not $value){return $Text}
    $kept=@();$changed=$false
    foreach($item in $value -split ','){
        $parts=$item.Split(@([char]'@'),2);$name=$parts[0];$file=$(if($parts.Length -gt 1){$parts[1]}else{''})
        $ours=(($name -ceq '热心网友033' -or -not $name) -and ($file -ceq '033-engine.dll' -or (-not $file -and $name)))
        if($RequiresFeeder){
            $ours=$ours -or $item -ceq 'Generic Depth' -or (($name -cmatch '^DLSS 5 Feed(?:\s|$)' -or -not $name) -and ($file -cin @('dlss5-feed.addon32','dlss5-feed.addon64') -or (-not $file -and $name)))
        }
        if($ours){$changed=$true}else{$kept+=@($item)}
    }
    if(-not $changed){return $Text}
    return Set-033RouteIniValue $Text 'ADDON' 'DisabledAddons' ($kept -join ',')
}
function Merge-033RouteMissingValues([string]$Defaults,[string]$Active){
    # The preset currently selected by the player takes priority. Fill missing
    # values from the old owned preset before enforcing only the pipeline keys.
    $seen=@{};$section=''
    foreach($line in [regex]::Split($Active,'\r\n|\n|\r')){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1];continue}
        if($line -match '^\s*([^;#=][^=]*?)\s*=(.*)$'){$seen[$section+"`n"+$Matches[1].Trim()]=$true}
    }
    $text=$Active;$section=''
    foreach($line in [regex]::Split($Defaults,'\r\n|\n|\r')){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1];continue}
        if($line -match '^\s*([^;#=][^=]*?)\s*=(.*)$'){
            $key=$Matches[1].Trim();$value=$Matches[2];$id=$section+"`n"+$key
            if(-not $seen.ContainsKey($id)){$text=Set-033RouteIniValue $text $section $key $value;$seen[$id]=$true}
        }
    }
    return $text
}
function Get-033RouteActivePreset([string]$OwnedPath){
    $dir=[IO.Path]::GetDirectoryName($OwnedPath);$config=Join-Path $dir 'ReShade.ini'
    if(-not [IO.File]::Exists($config)){return $null}
    $selected=Get-033RouteIniValue ([IO.File]::ReadAllText($config)) 'GENERAL' 'PresetPath'
    if(-not $selected){return $null}
    try{$resolved=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($selected)){$selected}else{Join-Path $dir $selected}))}catch{return $null}
    # Read an explicitly referenced local preset, never network/device paths;
    # it is a source only, never an installer write or cleanup target.
    if($resolved -notmatch '^[A-Za-z]:\\' -or $resolved -ieq $OwnedPath -or -not [IO.File]::Exists($resolved)){return $null}
    $raw=[IO.File]::ReadAllBytes($resolved)
    $memory=[IO.MemoryStream]::new($raw,$false);$reader=[IO.StreamReader]::new($memory,[Text.Encoding]::UTF8,$true)
    try{$text=$reader.ReadToEnd()}finally{$reader.Dispose();$memory.Dispose()}
    $preamble=[regex]::Split($text,'(?m)^\s*\[')[0]
    if($preamble -notmatch '(?m)^\s*Techniques\s*='){return $null}
    $h=[Security.Cryptography.SHA256]::Create()
    try{$hash=([BitConverter]::ToString($h.ComputeHash($raw))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}
    return @{Text=$text;Input=@{Path=$resolved;Hash=$hash}}
}
