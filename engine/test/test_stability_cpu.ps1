param([string]$OutputDirectory='E:\033插件\build\stability-cpu_20260906')
$ErrorActionPreference='Stop';$taskRoot=Split-Path -Parent $PSScriptRoot
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207';$taskSdk='C:\Program Files (x86)\Windows Kits\10';$taskVersion='10.0.26100.0'
$env:INCLUDE="$taskMsvc\include;$taskSdk\Include\$taskVersion\ucrt;$taskSdk\Include\$taskVersion\um;$taskSdk\Include\$taskVersion\shared"
$env:LIB="$taskMsvc\lib\x64;$taskSdk\Lib\$taskVersion\ucrt\x64;$taskSdk\Lib\$taskVersion\um\x64"
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Push-Location $OutputDirectory
try{
 $common=@('/nologo','/std:c++17','/O2','/MD','/EHsc','/utf-8','/D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR')
 & "$taskMsvc\bin\Hostx64\x64\cl.exe" @common "$PSScriptRoot\queue_safety_cpu_test.cpp" /link /OUT:queue_safety_cpu_test.exe
 if($LASTEXITCODE){throw 'Queue safety compilation failed'}
 & .\queue_safety_cpu_test.exe;if($LASTEXITCODE){throw 'Queue safety failed'}
 $faces=@('facedetectcnn','facedetectcnn-model','facedetectcnn-data')|ForEach-Object {"$taskRoot\third_party\FaceDetect033\src\$_.cpp"}
 & "$taskMsvc\bin\Hostx64\x64\cl.exe" @common "$PSScriptRoot\portrait_detector_cpu_test.cpp" @faces d3dcompiler.lib /link /OUT:portrait_detector_cpu_test.exe
 if($LASTEXITCODE){throw 'Portrait CPU compilation failed'}
 & .\portrait_detector_cpu_test.exe "$taskRoot\build\portrait-controls-tests\face.rgba"
 if($LASTEXITCODE){throw 'Portrait CPU regression failed'}
}finally{Pop-Location}
