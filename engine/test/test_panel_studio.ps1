param([string]$OutputDirectory='E:\033插件\build\panel-studio-preview',[string]$Engine='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared;$taskSdk\Include\$taskVersion\winrt"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$taskImgui=Join-Path $PSScriptRoot 'panel-preview-sdk'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Push-Location $OutputDirectory
try {
 & cl.exe /nologo /std:c++17 /O2 /MD /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /EHsc /utf-8 /I "$taskImgui" /I "$taskRoot\src" "$PSScriptRoot\panel_studio_preview.cpp" "$taskImgui\imgui.cpp" "$taskImgui\imgui_draw.cpp" "$taskImgui\imgui_tables.cpp" "$taskImgui\imgui_widgets.cpp" "$taskImgui\backends\imgui_impl_dx11.cpp" d3d11.lib dxgi.lib d3dcompiler.lib windowscodecs.lib ole32.lib /link /OUT:panel_studio_preview.exe
 if($LASTEXITCODE){throw 'Panel preview compile failed'}
 if($Engine){& .\panel_studio_preview.exe $OutputDirectory $Engine}else{& .\panel_studio_preview.exe $OutputDirectory}
 if($LASTEXITCODE){throw 'Panel preview failed'}
}finally{Pop-Location}
