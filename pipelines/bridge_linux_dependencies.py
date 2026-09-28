"""Find ELF dependencies to bundle beyond linuxdeploy's default exclusions."""
import fnmatch
import os
from pathlib import Path
import re
import subprocess
import sys

runtime = Path(sys.argv[1]).resolve()
# These belong to the OS ABI or the host graphics driver stack. Bundling
# Ubuntu's glibc or graphics-driver libraries would break newer Fedora drivers.
os_libraries = (
    'libc.so.*', 'libm.so.*', 'libdl.so.*', 'libpthread.so.*', 'librt.so.*',
    'libresolv.so.*', 'libutil.so.*', 'libgcc_s.so.*', 'libstdc++.so.*',
    'libGL.so.*', 'libGLX.so.*', 'libGLdispatch.so.*', 'libEGL.so.*',
    'libGLES*.so.*', 'libOpenGL.so.*', 'libvulkan.so.*', 'libdrm*.so.*',
    'libgbm.so.*', 'libwayland-*.so.*', 'libxkbcommon*.so.*',
)
if '--prune-os-libraries' in sys.argv[2:]:
    for path in (runtime / 'usr/lib').glob('*'):
        if any(fnmatch.fnmatch(path.name, pattern) for pattern in os_libraries):
            path.unlink()
env = dict(os.environ, LD_LIBRARY_PATH=str(runtime / 'usr/lib'))
extra = set()
for directory in ('usr/bin', 'usr/lib', 'usr/plugins'):
    for path in (runtime / directory).rglob('*'):
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        result = subprocess.run(['ldd', str(path)], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if 'not found' in result.stdout:
            raise RuntimeError(f'Unresolved dependency in {path}:\n{result.stdout}')
        for name, location in re.findall(r'^\s*(\S+) => (/\S+) ', result.stdout, re.MULTILINE):
            if any(fnmatch.fnmatch(name, pattern) for pattern in os_libraries):
                continue
            resolved = Path(location).resolve()
            if runtime not in resolved.parents:
                extra.add(location)
print('\n'.join(sorted(extra)))
