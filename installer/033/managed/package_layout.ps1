# Flat historical packages and the new root launchers + 033 data directory.
# Resolve only declared relative paths; preserve normal path/reparse checks.
function Get-033PackageDataRoot([string]$Root){
    $full=Get-033ManagedRoot $Root
    if(Test-Path -LiteralPath (Get-033Path $full '033-package.json') -PathType Leaf){return $full}
    $child=Get-033Path $full '033'
    if(Test-Path -LiteralPath (Get-033Path $child '033-package.json') -PathType Leaf){return $child}
    return $full
}
function Get-033PackageDistributionRoot([string]$DataRoot){
    $full=Get-033ManagedRoot $DataRoot
    if((Split-Path -Leaf $full) -ceq '033'){
        $parent=Split-Path -Parent $full
        if($parent -and (Test-Path -LiteralPath (Get-033Path $parent '033安装器.exe') -PathType Leaf)){return $parent}
    }
    return $full
}
