"""入口 dxgi.dll（ReShade 6.8.0 改版）：先离线生成 GLAD，再用 MSBuild 编 Release x64，参数和 V6.2 安装包里那份相同。

用法：python build/build_host.py [--fork host] [--cache out/host-cache] [--out out/host]
输出：<out>/dxgi.dll（以及 033-host.pdb / 033-host.map）。
"""
from pathlib import Path
import argparse, os, shutil, subprocess, sys, hashlib

repo = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--fork', default=str(repo/'host'))
ap.add_argument('--cache', default=str(repo/'out'/'host-cache'))
ap.add_argument('--out', default=str(repo/'out'/'host'))
ap.add_argument('--vs', default=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools')
ap.add_argument('--msvc', default='14.44.35207')
ap.add_argument('--sdk', default=r'C:\Program Files (x86)\Windows Kits\10')
ap.add_argument('--sdk-version', default='10.0.26100.0')
a = ap.parse_args()
FORK, CACHE, OUT = Path(a.fork), Path(a.cache), Path(a.out)
MSBUILD = Path(a.vs)/'MSBuild/Current/Bin/MSBuild.exe'
MSVC = Path(a.vs)/'VC/Tools/MSVC'/a.msvc
SDK, VERSION = Path(a.sdk), a.sdk_version
for tool in (MSBUILD, MSVC/'bin/Hostx64/x64/cl.exe'):
    if not tool.is_file(): raise SystemExit(f'找不到 {tool}')
OUT.mkdir(parents=True, exist_ok=True); (OUT/'temp').mkdir(exist_ok=True)

# Windows 的环境变量不分大小写：只给子进程去重，并清掉会改变编译结果的继承变量。
env = {k.upper(): v for k, v in os.environ.items()}
for key in ['CL', '_CL_', 'LINK', '_LINK_', 'PYTHONPATH', 'VK_SDK_PATH', 'VULKAN_SDK', 'MSBUILD_EXE_PATH', 'MSBUILDADDITIONALLOADPATHS', 'VCTARGETSPATH']:
    env.pop(key, None)
env['PATH'] = ';'.join([str(MSVC/'bin/Hostx64/x64'), str(SDK/'bin'/VERSION/'x64'), str(Path(sys.executable).parent), env.get('PATH', '')])
env['INCLUDE'] = ';'.join([str(MSVC/'include')] + [str(SDK/'Include'/VERSION/n) for n in ['ucrt', 'um', 'shared', 'winrt']])
env['LIB'] = ';'.join([str(MSVC/'lib/x64'), str(SDK/'Lib'/VERSION/'ucrt/x64'), str(SDK/'Lib'/VERSION/'um/x64')])
env['_CL_'] = '/MP4'
env['PYTHONDONTWRITEBYTECODE'] = '1'
env['TEMP'] = env['TMP'] = str(OUT/'temp')

def run(name, args, cwd):
    print(f'{name} …', flush=True)
    with (OUT/(name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run([str(x) for x in args], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((OUT/(name + '.log')).read_text(encoding='utf-8', errors='replace')[-8000:])
        raise SystemExit(f'{name} 失败（{result.returncode}）')

run('glad', [sys.executable, '-I', Path(__file__).with_name('generate_glad_offline.py'), FORK], FORK)
run('msbuild', [MSBUILD, FORK/'ReShade.vcxproj', '/t:Build', '/m:1', '/v:minimal', '/nologo',
    '/p:Configuration=Release', '/p:Platform=x64', '/p:PlatformToolset=v143', '/p:VCToolsVersion=' + a.msvc,
    '/p:WindowsTargetPlatformVersion=' + VERSION, '/p:SolutionDir=' + str(FORK) + '\\',
    '/p:K033OutputRoot=' + str(CACHE) + '\\', '/p:PreBuildEventUseInBuild=false', '/p:PostBuildEventUseInBuild=false',
    '/p:ImportDirectoryBuildProps=false', '/p:ImportDirectoryBuildTargets=false', '/p:MultiProcessorCompilation=false',
    '/p:GenerateMapFile=true', '/p:GenerateDebugInformation=true'], FORK)
binary = CACHE/'bin/x64/ReShade64.dll'
shutil.copy2(binary, OUT/'dxgi.dll')
for extension in ['pdb', 'map']:
    extra = binary.with_suffix('.' + extension)
    if extra.is_file(): shutil.copy2(extra, OUT/('033-host.' + extension))
print('dxgi.dll', hashlib.sha256((OUT/'dxgi.dll').read_bytes()).hexdigest())
