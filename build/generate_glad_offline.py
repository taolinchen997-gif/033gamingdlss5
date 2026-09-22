"""离线运行 host/deps/glad 里固定版本的 GLAD 生成器，只读 host/deps/khronos 里的 XML，不联网。

用法：python generate_glad_offline.py <host 目录>
"""
from pathlib import Path
import os, sys, xml.etree.ElementTree as ET

FORK = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]/'host'
GLAD = FORK/'deps/glad'
os.chdir(FORK/'deps/khronos')
sys.path.insert(0, str(GLAD))
sys.dont_write_bytecode = True
# GLAD 的 StaticFileOpener 连头文件请求都会解析成包里的文件；再明确拒绝意外的网络连接，保证离线。
import socket
def denied(*args, **kwargs): raise RuntimeError('network forbidden in offline source generation')
socket.create_connection = denied
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
doc = ET.parse(FORK/'deps/glad.vcxproj')
def value(name): return doc.find('.//m:' + name, ns).text
apis = ','.join(value(n) for n in ('GladGLVersion', 'GladWGLVersion', 'GladVulkanVersion'))
extensions = ','.join(value(n).replace(';', ',') for n in ('GladGLExtensions', 'GladWGLExtensions', 'GladVulkanExtensions'))
import glad.plugin
glad.plugin.entry_points = lambda **kwargs: ()  # 不去发现已安装的扩展
import glad.files
from urllib.parse import urlparse
class PinnedFileOpener(glad.files.StaticFileOpener):
    def urlopen(self, url, data=None, *args, **kwargs):
        if data is not None: raise RuntimeError('payload forbidden')
        name = urlparse(url).path.rsplit('/', 1)[-1]
        if name in ('gl.xml', 'vk.xml', 'wgl.xml'):
            return (FORK/'deps/khronos'/name).open('rb')
        return super().urlopen(url, data, *args, **kwargs)
glad.files.StaticFileOpener = PinnedFileOpener
from glad.__main__ import main
main(['--reproducible', '--out-path', str(GLAD/'target'), '--api', apis, '--extensions', extensions, 'c', '--mx'])
