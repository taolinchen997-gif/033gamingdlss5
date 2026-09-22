# 燕云定制版（S24）只给燕云十六声用。通用安装器里给别的游戏准备的三块已从包里拿掉：
#   vulkan_layer.ps1（Vulkan 层注册）、api_companions.ps1（DX8/DX9/DDraw/Glide 伴随转换件）、
#   mirror_components.ps1（RE Engine _storage_ 镜像）。
# 事务仍在原来的位置调用同名函数；这里给出的结果与原模块对燕云方案的结果逐字段相同。
# 一旦遇到只有别的游戏才会有的数据，就停下、不改游戏，而不是当作没看见。
function Stop-033NotYanYun([string]$What){
    throw ('这是燕云定制版安装器，不处理'+$What+'；没有改动游戏。请用 033 通用版处理这个游戏或这份记录。')
}
function Test-033VulkanProfile($Profile){
    if($Profile.ContainsKey('Activation') -and $Profile.Activation){Stop-033NotYanYun 'Vulkan 路线'}
    return $false
}
function Assert-033VulkanActivation($Profile){
    if($Profile.ContainsKey('Activation')){Stop-033NotYanYun 'Vulkan 路线'}
}
function Get-033VulkanIdentity([string]$Exe,[string]$Root){Stop-033NotYanYun 'Vulkan 层'}
function Get-033VulkanGeneratedFile($Profile,[string]$Exe,[string]$Root,$File){
    if(Test-033VulkanProfile $Profile){Stop-033NotYanYun 'Vulkan 层'}
    return $null
}
function Add-033VulkanPlan($Plan,$OldState){
    if($OldState -and $OldState.ContainsKey('VulkanLayers') -and @($OldState.VulkanLayers|Where-Object {$_}).Count){Stop-033NotYanYun '登记过 Vulkan 层的备份记录'}
    foreach($target in $Plan.Targets){if($target.ContainsKey('VulkanLayer')){Stop-033NotYanYun 'Vulkan 层'}}
    $Plan['RegistryActions']=@();$Plan['VulkanLayers']=@()
}
function Assert-033VulkanJournal($Journal){
    if($Journal.ContainsKey('RegistryActions') -and @($Journal.RegistryActions|Where-Object {$_}).Count){Stop-033NotYanYun '带 Vulkan 注册表操作的事务记录'}
}
function Invoke-033VulkanRegistryActions($Journal,[string]$JournalPath,[switch]$Undo){
    Assert-033VulkanJournal $Journal
}
function Add-033ApiCompanions($Profile,$Package,[string]$Exe,[string]$Root,$OldState=$null){
    $copy=Copy-033ManagedObject $Profile
    if($copy.ContainsKey('Suitability') -and $copy.Suitability -and $copy.Suitability.ContainsKey('ProductLine') -and $copy.Suitability.ProductLine -ceq 'legacy'){
        Stop-033NotYanYun '老游戏伴随转换件'
    }
    return @{Profile=$copy;Decisions=@();Warnings=@()}
}
function Get-033OwnedMirrorComponents($Profile,[string]$Root,$OldState,[int]$RootIndex){
    if(@(Get-033MirrorDirectories $Profile $Root).Count){Stop-033NotYanYun 'RE Engine 镜像目录'}
    return @()
}
