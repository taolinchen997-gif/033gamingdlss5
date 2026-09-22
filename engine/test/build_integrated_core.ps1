param([string]$OutputDirectory=(Join-Path (Split-Path -Parent $PSScriptRoot) 'build\033-integrated-candidate_20260906\core'))
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskVendor=Join-Path $taskRoot 'third_party\OptiScaler033'
$taskMsvc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$taskOut=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')+'\'
New-Item -ItemType Directory -Path $taskOut -Force | Out-Null
$taskStamp=Get-Date -Format 'yyyyMMdd_HHmmss'
# A stable generated header preserves incremental builds. The release manifest
# records the actual DLL hash, source diff and build time, independently.
if(-not (Test-Path -LiteralPath (Join-Path $taskVendor 'OptiScaler\resource_build_date.h'))){
    [IO.File]::WriteAllText((Join-Path $taskVendor 'OptiScaler\resource_build_date.h'),('#define VER_BUILD_DATE "'+$taskStamp+'"'+"`n"),[Text.Encoding]::ASCII)
}
if(-not (Test-Path -LiteralPath (Join-Path $taskVendor 'OptiScaler\resource_build_commit.h'))){
    [IO.File]::WriteAllText((Join-Path $taskVendor 'OptiScaler\resource_build_commit.h'),('#define VER_BUILD_COMMIT "033-9737616"'+"`n"),[Text.Encoding]::ASCII)
}
# Upstream FSR SDK objects use /MD. Rebuild the one /MT C dependency from its
# matching 2.13.3 source, so the linked core uses one CRT without hiding warnings.
$taskCmake='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$taskFt=Join-Path $taskRoot 'build\freetype033-md'
if(-not (Test-Path -LiteralPath (Join-Path $taskFt 'Release\freetype.lib'))){
    & $taskCmake -S (Join-Path $taskRoot 'third_party\freetype033') -B $taskFt -G 'Visual Studio 17 2022' -A x64 -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    if($LASTEXITCODE){throw 'FreeType configure failed'}
    & $taskCmake --build $taskFt --config Release --parallel 4
    if($LASTEXITCODE){throw 'FreeType build failed'}
}
$taskFtLib=Join-Path $taskFt 'Release\freetype.lib'
$taskFtTarget=Join-Path $taskVendor 'external\freetype\freetype.lib'
if((Get-FileHash -LiteralPath $taskFtLib).Hash -ne (Get-FileHash -LiteralPath $taskFtTarget).Hash){Copy-Item -LiteralPath $taskFtLib -Destination $taskFtTarget -Force}
$env:_CL_='/utf-8 /MP4'
& $taskMsvc (Join-Path $taskVendor 'OptiScaler\OptiScaler.vcxproj') /t:Build /m:1 /v:minimal /nologo /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$taskVendor\" "/p:OutDir=$taskOut" "/p:IntDir=$taskOut`obj\" /p:TargetName=033-render-core /p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false /p:CL_MPCount=4
if($LASTEXITCODE){throw 'Integrated rendering core build failed'}
& $taskMsvc (Join-Path $taskVendor 'OptiScaler\dlssnr\forwarder\dlssnr_forwarder.vcxproj') /t:Build /m:1 /v:minimal /nologo /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$taskVendor\" "/p:OutDir=$taskOut" "/p:IntDir=${taskOut}forwarder-obj\" /p:PreBuildEventUseInBuild=false /p:PostBuildEventUseInBuild=false
if($LASTEXITCODE){throw 'Integrated NR forwarder build failed'}
Get-FileHash -LiteralPath (Join-Path $taskOut '033-render-core.dll')
