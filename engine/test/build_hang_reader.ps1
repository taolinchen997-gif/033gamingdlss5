$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$taskSdk='C:\Program Files (x86)\Windows Kits\10'
$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
$env:PATH="$taskMsvc\bin\Hostx64\x64;"+$env:PATH
$taskOut=Join-Path $taskRoot 'build\hang-reader'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
Push-Location $taskOut
try{& cl.exe /nologo /std:c++17 /MD /EHsc "$taskRoot\test\read_hang_stacks.cpp" dbgeng.lib /link /OUT:read_hang_stacks.exe;if($LASTEXITCODE){throw 'Reader build failed'}}finally{Pop-Location}
