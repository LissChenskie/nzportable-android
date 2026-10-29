#!/usr/bin/env python3
"""Build actual Vril on Linux with dummy Android services for renderer scene QA.

Run tests/run_vulkan_tests.sh first. Requires pkg-config, SDL2_mixer development
files and the same SDL2/GLES/Vulkan development environment as that suite.
Supply your own pinned NZPortable data, then run the executable under Xvfb:

  NZP_SMOKE_MAP=ndu SDL_AUDIODRIVER=dummy xvfb-run -a \
    build/host-smoke/nzp-host -basedir /absolute/game/data -vkvalidation

Set data/nzp/graphics-api.txt to 1 (Vulkan) or 2 (GLES2). Screenshots are written
under build/game-smoke (Vulkan) or capture.bmp (GLES2). This is host correctness
QA, not an Android port and not a hardware performance benchmark.
"""
from pathlib import Path
import concurrent.futures
import os
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
os.chdir(root)
engine = root / 'third_party/vril/source'
out = root / 'build/host-smoke'
out.mkdir(parents=True, exist_ok=True)
(root / 'build/game-smoke').mkdir(exist_ok=True)
(out / '_build_info.h').write_text('#define GIT_HASH "host-scene-test"\n#define GIT_BRANCH "host-scene-test"\n#define BUILD_DATE "test"\n')
flags = ['-std=gnu99', '-O1', '-fno-strict-aliasing', f'-I{engine}',
         f'-I{root}/android/native', f'-I{out}']
flags += ['-D' + value for value in ('GLQUAKE', 'PLATFORM_SDL', 'PLATFORM_ANDROID',
    'NZP_VULKAN', 'PLATFORM_CONFIRM_IS_ENTER', 'PLATFORM_DIRECTORY=sdl',
    'PLATFORM_RENDERER=gl', 'MAX_AI_COUNT=24', 'PLATFORM_USES_GENERIC_GLYPHS',
    'PLATFORM_SUPPORTS_HIGH_FRAMERATES', 'PLATFORM_SUPPORTS_RUMBLE')]
# Optional local package prefix for environments where development .deb files
# are unpacked without system installation. Ordinary Linux uses pkg-config.
prefix = os.environ.get('NZP_HOST_PREFIX')
if prefix:
    prefix = Path(prefix)
    flags += [f'-I{prefix}/usr/include', f'-I{prefix}/usr/include/SDL2',
              f'-I{prefix}/usr/include/x86_64-linux-gnu',
              f'-I{root}/third_party/SDL_mixer/include']
    lib = prefix / 'usr/lib/x86_64-linux-gnu'
    libraries = ['/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0',
                 str(lib / 'libSDL2_mixer-2.0.so.0'), str(lib / 'libGLESv2.so'),
                 '-Wl,-rpath-link,' + str(lib)]
else:
    packages = ['sdl2', 'SDL2_mixer', 'glesv2']
    flags += shlex.split(subprocess.check_output(['pkg-config', '--cflags', *packages], text=True))
    libraries = shlex.split(subprocess.check_output(['pkg-config', '--libs', *packages], text=True))
files = []
for pattern in ('*.c', 'menu/*.c', 'qcvm/*.c', 'render/*.c', 'tests/*.c',
                'platform/sdl/*.c', 'platform/sdl/gl/*.c'):
    files += list(engine.glob(pattern))
files = [p for p in files if p.name != 'gl_vidsdl.c']
files += [root / 'android/native/gles_compat.c', root / 'android/native/video_android.c',
          root / 'tests/android_host_stubs.c']

def compile_source(path):
    obj = out / (str(path.relative_to(root)).replace('/', '_') + '.o')
    subprocess.run(['cc', *flags, '-c', str(path), '-o', str(obj)], check=True)
    return str(obj)

with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
    objects = list(pool.map(compile_source, files))
subprocess.run(['c++', *objects, 'build/host-tests/renderer.o', 'build/host-tests/volk.o',
                '-o', str(out / 'nzp-host'), *libraries, '-ldl', '-lm'], check=True)
print(out / 'nzp-host')
