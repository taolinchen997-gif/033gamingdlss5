param([switch]$MockNgx,[switch]$Baseline)
$ErrorActionPreference='Stop';$root=Split-Path -Parent $PSScriptRoot;$feed=Join-Path $root 'ref\DLSS5-Feeder-repo'
$msvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$sdk='C:\Program Files (x86)\Windows Kits\10';$v='10.0.26100.0'
$env:INCLUDE="$msvc\include;$sdk\Include\$v\ucrt;$sdk\Include\$v\um;$sdk\Include\$v\shared;$sdk\Include\$v\winrt"
$env:LIB="$msvc\lib\x64;$sdk\Lib\$v\ucrt\x64;$sdk\Lib\$v\um\x64"
$name=if($MockNgx){if($Baseline){'feeder532-mock-before'}else{'feeder532-mock'}}else{'feeder532'}
$out=Join-Path $root ('build\'+$name);New-Item -ItemType Directory -Path $out -Force|Out-Null
$source=if($Baseline){Join-Path $PSScriptRoot 'fixtures\feeder_lifetime_before.cpp'}else{Join-Path $feed 'src\dlss5-feed.cpp'}
$inputFiles=@($source,"$feed\external\minhook\src\buffer.c","$feed\external\minhook\src\hook.c","$feed\external\minhook\src\trampoline.c","$feed\external\minhook\src\hde\hde64.c")
$libs=@('version.lib','kernel32.lib','user32.lib','advapi32.lib','ole32.lib')
if($MockNgx){$inputFiles+=Join-Path $PSScriptRoot 'feeder_ngx_mock.cpp'}else{$libs+=Join-Path $root 'sdk\ngx-feeder-build\nvsdk_ngx_d.lib'}
Push-Location $out
try{
 & "$msvc\bin\Hostx64\x64\cl.exe" /nologo /LD /EHsc /O2 /MD /W3 /std:c++20 /utf-8 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /I "$feed\src" /I "$feed\external\reshade\include" /I "$root\sdk\ngx" /I "$root\third_party\OptiScaler033\external\vulkan\include" /I "$feed\external\imgui" /I "$feed\external\minhook\include" @inputFiles /link /OUT:dlss5-feed.addon64 /DEBUG:FULL /PDB:dlss5-feed.pdb @libs
 if($LASTEXITCODE){throw 'Feeder build failed'}
 Get-FileHash -LiteralPath (Join-Path $out 'dlss5-feed.addon64')
}finally{Pop-Location}
