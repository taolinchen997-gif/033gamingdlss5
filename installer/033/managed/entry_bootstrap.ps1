# Metadata-only contract: a hosted 033 child cannot act as the game proxy.
# The current Beta2 core DllMain deliberately leaves vendor proxy pointers cold.
# Only its host may call K033_Beta2InitializeVendor after preparing shared state.
function Assert-033EntryBootstrap([string]$Path,$File){
    if(-not $File.ContainsKey('Role') -or $File.Role -ne 'entry'){return}
    $exports=Get-033PeExportNames $Path
    if($File.ContainsKey('HostKind')){
        if($File.HostKind -ceq 'reshade-yanyun-v1'){
            foreach($required in @('ReShadeRegisterAddon','ReShadeRegisterEventForAddon','ReShadeRegisterOverlayForAddon','K033_GetYanYunArtwork')){
                if($null -eq $exports -or -not $exports.Contains($required)){throw '033_YANYUN_HOST_MISMATCH: 燕云专属宿主接口不配套，未改游戏。'}
            }
        }elseif($File.HostKind -cne 'reshade' -or $null -eq $exports -or -not $exports.Contains('ReShadeRegisterAddon') -or -not $exports.Contains('K033_LegacyFeederHostV1')){
            throw '033_HOST_KIND_MISMATCH: 宿主声明与实际接口不一致，未改游戏。'
        }
    }
    if($File.Target -ceq '033-vulkan.dll'){
        foreach($name in @('vkNegotiateLoaderLayerInterfaceVersion','vkGetInstanceProcAddr','vkGetDeviceProcAddr','K033_VulkanTransportInfo','K033_VulkanPresentOrdered')){
            if($null -eq $exports -or -not $exports.Contains($name)){throw ('033 Vulkan layer entry is missing required export: '+$name)}
        }
    }
    if($null -ne $exports -and $exports.Contains('K033_Beta2InitializeVendor')){
        throw '033_ENTRY_REQUIRES_HOST: 包内入口组件需要 033 宿主初始化，不能直接用作游戏挂载 DLL。请使用修正后的完整包；尚未改动游戏。'
    }
}

# A Feeder file on disk is insufficient: its host must explicitly load and
# register the managed addon. Read PE exports only; never execute product code.
function Assert-033FeederBootstrap([string]$Root,$Profile){
    $feeders=@($Profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -in @('feeder-addon','feeder-client')})
    if(-not $feeders.Count){return}
    # Vulkan uses its separate transport ABI checked by Assert-033EntryBootstrap.
    if($Profile.Id -ceq 'native-vulkan-x64'){return}
    $hosts=@()
    foreach($f in @($Profile.Files|Where-Object {$_.ContainsKey('Role') -and $_.Role -eq 'entry'})){
        $path=Get-033Path $Root $f.Source;$exports=Get-033PeExportNames $path
        if($null -eq $exports -or -not $exports.Contains('ReShadeRegisterAddon')){continue}
        $hosts+=@($f)
        # Exact pre-contract legacy hosts retained in the existing 6.1.3 package.
        # No basename or broad version exception: all replacement hosts need V1.
        $historical=@('d7eb742f5fa6b6720f56ec478b22d79774fc0def9dae6c29b8ff9de5157df09b','07b314f4d4e7872e7d385276b34d0c255f9fee1b0c5da8300686b6941de2c6c7')
        if(-not $exports.Contains('K033_LegacyFeederHostV1') -and $historical -inotcontains (Get-033FileHash $path)){
            throw '033_FEEDER_HOST_MISMATCH: 输入桥与宿主不匹配，NR 无法获得画面。请使用配套完整包；尚未改动游戏。'
        }
    }
    if($hosts.Count -ne 1){throw '033_FEEDER_HOST_MISSING: 输入桥路线必须有且只有一个兼容宿主。'}
    foreach($f in $feeders){
        $exports=Get-033PeExportNames (Get-033Path $Root $f.Source)
        if($null -eq $exports -or -not $exports.Contains('K033_FeederEntry')){throw '033_FEEDER_ENTRY_MISSING: 输入桥缺少受管加载入口。'}
    }
}
