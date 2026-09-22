param([string]$OutputDirectory='E:\033插件\build\stability-cpu_20260906\panel')
$ErrorActionPreference='Stop';$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$taskImgui=Join-Path $PSScriptRoot 'panel-preview-sdk';New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Push-Location $OutputDirectory
try{
 & "$taskMsvc\bin\Hostx64\x64\cl.exe" /nologo /std:c++17 /O2 /MD /EHsc /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /I $taskImgui "$PSScriptRoot\panel_studio_cpu_test.cpp" "$taskImgui\imgui.cpp" "$taskImgui\imgui_draw.cpp" "$taskImgui\imgui_tables.cpp" "$taskImgui\imgui_widgets.cpp" /link /OUT:panel_studio_cpu_test.exe
 if($LASTEXITCODE){throw 'Panel CPU compilation failed'}
 & .\panel_studio_cpu_test.exe;if($LASTEXITCODE){throw 'Panel CPU checks failed'}
}finally{Pop-Location}
