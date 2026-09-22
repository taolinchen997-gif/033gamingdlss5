# Read-only PE and game inventory. No LoadLibrary, execution, driver or registry use.
# Design reference: DLSS5-Swapper 357265c3 (MIT); see SWAPPER_SOURCE_NOTICE.md.
function Get-033PeInfo([string]$Path) {
    $result=[ordered]@{Path=[IO.Path]::GetFullPath($Path);Status='unknown';Architecture=$null;IsDll=$false;Imports=@();DelayImports=@();Error=$null}
    $stream=$null
    try {
        $stream=[IO.File]::Open($result.Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        function ReadAt([long]$Offset,[int]$Count){
            if($Offset -lt 0 -or $Count -lt 0 -or $Offset -gt $stream.Length-$Count){throw 'Truncated PE range'}
            $data=[byte[]]::new($Count);$stream.Position=$Offset;$n=0
            while($n -lt $Count){$read=$stream.Read($data,$n,$Count-$n);if(-not $read){throw 'Short PE read'};$n+=$read}
            return ,$data
        }
        $dos=ReadAt 0 64;if([BitConverter]::ToUInt16($dos,0) -ne 0x5a4d){throw 'Not a PE file'}
        $pe=[BitConverter]::ToUInt32($dos,60);$coff=ReadAt $pe 24
        if([BitConverter]::ToUInt32($coff,0) -ne 0x4550){throw 'Invalid PE signature'}
        $machine=[BitConverter]::ToUInt16($coff,4);$count=[BitConverter]::ToUInt16($coff,6)
        $optSize=[BitConverter]::ToUInt16($coff,20);$opt=ReadAt ($pe+24) $optSize
        if($optSize -lt 96 -or $count -lt 1 -or $count -gt 96){throw 'Invalid PE header sizes'}
        $magic=[BitConverter]::ToUInt16($opt,0)
        if($magic -eq 0x10b -and $machine -eq 0x14c){$arch='x86';$dirs=96;$imageBase=[uint64][BitConverter]::ToUInt32($opt,28)}
        elseif($magic -eq 0x20b -and $machine -in @(0x8664,0xaa64)){
            $arch=if($machine -eq 0x8664){'x64'}else{'arm64'};$dirs=112
            if($optSize -lt 112){throw 'Truncated PE32+ header'}
            $imageBase=[BitConverter]::ToUInt64($opt,24)
        }else{throw 'Unsupported or inconsistent PE machine/magic'}
        $dirCount=[BitConverter]::ToUInt32($opt,$dirs-4)
        if($dirCount -gt 16 -or $dirs+8*$dirCount -gt $optSize){throw 'Invalid PE data directories'}
        $headers=[BitConverter]::ToUInt32($opt,60);$sections=@()
        for($i=0;$i -lt $count;$i++){
            $s=ReadAt ($pe+24+$optSize+40*$i) 40
            $sections+=@{VA=[long][BitConverter]::ToUInt32($s,12);Raw=[long][BitConverter]::ToUInt32($s,20);Size=[long][BitConverter]::ToUInt32($s,16)}
        }
        function Offset([long]$Rva,[int]$Size){
            if($Rva -ge 0 -and $Rva+$Size -le $headers -and $Rva+$Size -le $stream.Length){return $Rva}
            $matches=@($sections|Where-Object {$Rva -ge $_.VA -and $Rva+$Size -le $_.VA+$_.Size})
            if($matches.Count -ne 1){throw 'RVA is outside file-backed PE data or ambiguous'}
            $at=$matches[0].Raw+$Rva-$matches[0].VA
            if($at -gt $stream.Length-$Size){throw 'Truncated PE section'};return $at
        }
        function NameAt([long]$Rva){
            $name=[Collections.Generic.List[byte]]::new()
            for($i=0;$i -lt 1024;$i++){
                $b=(ReadAt (Offset ($Rva+$i) 1) 1)[0]
                if(-not $b){if(-not $name.Count){throw 'Empty import name'};return [Text.Encoding]::ASCII.GetString($name.ToArray()).ToLowerInvariant()}
                if($b -lt 32 -or $b -gt 126){throw 'Invalid import name'};$name.Add($b)
            };throw 'Unterminated import name'
        }
        function Names([int]$Index,[int]$Stride){
            if($dirCount -le $Index){return @()}
            $rva=[BitConverter]::ToUInt32($opt,$dirs+8*$Index);$size=[BitConverter]::ToUInt32($opt,$dirs+8*$Index+4)
            if(-not $rva -and -not $size){return @()}
            if(-not $rva -or $size -lt $Stride -or $size -gt 1048576){throw 'Invalid import directory'}
            $names=@();$terminated=$false
            for($i=0;$i+$Stride -le $size;$i+=$Stride){
                $entry=ReadAt (Offset ([long]$rva+$i) $Stride) $Stride
                if(@($entry|Where-Object {$_ -ne 0}).Count -eq 0){$terminated=$true;break}
                $nameRva=[long][BitConverter]::ToUInt32($entry,$(if($Index -eq 13){4}else{12}))
                if($Index -eq 13){
                    $attrs=[BitConverter]::ToUInt32($entry,0)
                    if($attrs -notin @(0,1)){throw 'Unknown delay import attributes'}
                    if($attrs -eq 0){if($imageBase -gt $nameRva){throw 'Invalid delay import VA'};$nameRva-=[long]$imageBase}
                }
                $names+=NameAt $nameRva
            }
            if(-not $terminated){throw 'Unterminated import directory'};return $names
        }
        $result.Architecture=$arch;$result.IsDll=([BitConverter]::ToUInt16($coff,22) -band 0x2000) -ne 0
        $result.Imports=@(Names 1 20|Select-Object -Unique);$result.DelayImports=@(Names 13 32|Select-Object -Unique)
        $result.Status='valid'
    }catch{$result.Error=$_.Exception.Message}
    finally{if($stream){$stream.Dispose()}}
    [pscustomobject]$result
}
function Get-033ApiNames([string[]]$Imports){
    $map=@{'ddraw.dll'='ddraw';'d3dim.dll'='ddraw';'d3dim700.dll'='ddraw';'glide.dll'='glide';'glide2x.dll'='glide';'glide3x.dll'='glide';'d3d8.dll'='dx8';'d3d9.dll'='dx9';'d3d10.dll'='dx10';'d3d10_1.dll'='dx10';'d3d11.dll'='dx11';'d3d12.dll'='dx12';'dxgi.dll'='dxgi';'opengl32.dll'='opengl';'vulkan-1.dll'='vulkan'}
    # 空输入经 [string[]] 绑定会变成一个 $null 元素；跳过它，否则 ContainsKey($null) 直接抛。
    @($Imports|Where-Object {$_}|ForEach-Object {if($map.ContainsKey($_)){$map[$_]}}|Sort-Object -Unique)
}
# 同时有 DX10/11/12 模块时，DX9 / DX8 / DirectDraw 不算游戏画面接口（v5.0 选挂载点时就这么判；2026-09-11 仁王1 找回）。
function Select-033ModernGraphicsModules([string[]]$Modules){
    $Modules=@($Modules|Where-Object {$_})
    if(-not @($Modules|Where-Object {@('d3d10.dll','d3d10_1.dll','d3d11.dll','d3d12.dll') -icontains $_}).Count){return @($Modules)}
    return @($Modules|Where-Object {@('d3d9.dll','d3d8.dll','ddraw.dll','d3dim.dll','d3dim700.dll','glide.dll','glide2x.dll','glide3x.dll') -inotcontains $_})
}
# Dynamically loaded graphics APIs. Many engines never import d3d9/d3d11/d3d12
# statically: they LoadLibrary the name at runtime (Gujian3 loads d3d11.dll that
# way; The Witcher 3's DX12 renderer reaches d3d12 through Streamline's
# sl.interposer.dll). Route matching that only reads the import table therefore
# refuses those games outright. This is read-only: it scans the file's own bytes
# for the module names, in both ASCII and UTF-16, and never loads anything.
# Bounded: PE sections only, 4 MiB windows with overlap, stops once every name is
# accounted for or the byte budget is spent. Evidence is "the name appears in the
# binary", which is weaker than an import and is reported as such.
function Get-033DynamicApiNames([string]$Path,[ValidateRange(1,4096)][int]$BudgetMiB=512){
    # 2026-09-17 Fable：5.0 认 ddraw/glide（菲莉丝 requiem.exe 只有 ddraw.dll 字样，5.0 走 dgVoodoo DDraw；这里原来没有它，判成 D3D11 挂 dxgi 永远不加载）。
    $names=@('ddraw.dll','glide2x.dll','glide3x.dll','d3d8.dll','d3d9.dll','d3d10.dll','d3d11.dll','d3d12.dll','dxgi.dll','opengl32.dll','vulkan-1.dll','sl.interposer.dll')
    $found=@{};$stream=$null
    try{
        $stream=[IO.File]::Open([IO.Path]::GetFullPath($Path),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        # ISO-8859-1: one byte = one char, and it exists on Windows PowerShell 5.1
        # (Encoding.Latin1 is .NET 5+ only, and the installer must run under 5.1).
        $bytes=[Text.Encoding]::GetEncoding(28591)
        $window=4MB;$overlap=64;$budget=[long]$BudgetMiB*1MB;$read=[long]0
        $buffer=[byte[]]::new($window)
        while($stream.Position -lt $stream.Length -and $read -lt $budget -and $found.Count -lt $names.Count){
            $n=$stream.Read($buffer,0,$window);if(-not $n){break};$read+=$n
            $ascii=$bytes.GetString($buffer,0,$n)
            $wide=[Text.Encoding]::Unicode.GetString($buffer,0,$n-($n%2))
            foreach($name in $names){
                if($found.ContainsKey($name)){continue}
                if($ascii.IndexOf($name,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                   $wide.IndexOf($name,[StringComparison]::OrdinalIgnoreCase) -ge 0){$found[$name]=$true}
            }
            if($stream.Position -lt $stream.Length -and $n -eq $window){$stream.Position=$stream.Position-$overlap}
        }
    }catch{ }
    finally{ if($stream){$stream.Dispose()} }
    # sl.interposer means the game reaches D3D12 through Streamline.
    $modules=@($found.Keys);if($modules -contains 'sl.interposer.dll'){$modules+='d3d12.dll'}
    # 不要在前面加逗号。',@(...)' 会把整个数组当成一个元素吐出去, 调用处再套 @() 就成了
    # "数组里套数组", 选路拿 'dx11' 去查表时查的其实是整个数组, 永远查不到 —— 2026-09-11
    # 古剑三 / 巫师3 DX12 因此在生成计划时"一条路线都没匹配上", 而判断那条恰好经 $(...) 传参
    # 被拆开一层才碰巧能用。和 Get-033ApiNames 一样直接输出元素, 调用处用 @() 收。
    @(Get-033ApiNames $modules)
}
# 2026-09-17 Fable：Get-033BinaryTree / Get-033GameInventory 从未被调用（全包检索 0 处），删除。
