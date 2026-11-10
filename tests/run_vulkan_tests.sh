#!/bin/sh
# Requires SDL2, Vulkan headers/loader, validation layers, Mesa and glslang.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
python3 scripts/compile_vulkan_shaders.py --out build/host-tests/vulkan_shaders.h
cflags="-DNZP_VULKAN -DVK_NO_PROTOTYPES -Iandroid/native -Ibuild/host-tests $(pkg-config --cflags sdl2)"
cc $cflags -c android/native/vulkan/vendor/volk.c -o build/host-tests/volk.o
c++ -std=c++17 -O1 $cflags -c android/native/vulkan/renderer.cpp -o build/host-tests/renderer.o
cc -std=gnu99 $cflags -c android/native/gles_compat.c -o build/host-tests/frontend.o
for f in jobs thread_roles cpu_topology; do cc -std=gnu11 -D_GNU_SOURCE $cflags -c android/native/$f.c -o build/host-tests/$f.o; done
threads="build/host-tests/jobs.o build/host-tests/thread_roles.o build/host-tests/cpu_topology.o -lpthread"
# Temporal upscaler: built with Arm ASR when scripts/bootstrap.py fetched it, otherwise a stub.
asr=third_party/arm_asr
fsr_flags=""
fsr_objs=""
if [ -f $asr/src/components/fsr2/ffxm_fsr2.cpp ]; then
    mkdir -p build/host-tests/asr/Volk
    printf '#undef VK_USE_PLATFORM_XCB_KHR\n#undef VK_USE_PLATFORM_XLIB_KHR\n#include "%s/android/native/vulkan/vendor/volk.h"\n' "$PWD" > build/host-tests/asr/Volk/volk.h
    asr_inc="-Ibuild/host-tests/asr -I$asr/include -I$asr/include/host -I$asr -I$asr/include/gpu -I$asr/src -I$asr/src/shared -I$asr/src/components -I$asr/src/backends/shared -I$asr/src/backends/vk -I$asr/src/backends/shared/blob_accessors/prebuilt_shaders"
    for f in src/shared/ffxm_assert src/shared/ffxm_object_management src/components/fsr2/ffxm_fsr2 src/backends/shared/ffxm_shader_blobs \
             src/backends/shared/blob_accessors/ffxm_fsr2_shaderblobs src/backends/vk/ffxm_vk src/backends/vk/ffxm_hash; do
        o=build/host-tests/asr/$(basename $f).o
        # The VK backend only looks for "device local, not host visible" memory when not building for
        # Android; software Vulkan has no such type, so build it the way the real target does.
        extra=""; [ "$f" = src/backends/vk/ffxm_vk ] && extra="-D__ANDROID__"
        if [ ! -f $o ]; then
            c++ -std=c++20 -w -O1 $extra -DFFXM_VKLOADER_VOLK=1 -DFFXM_FSR -DVK_NO_PROTOTYPES -include wchar.h -include stddef.h $asr_inc -c $asr/$f.cpp -o $o &
        fi
        fsr_objs="$fsr_objs $o"
    done
    wait
    fsr_flags="-DNZP_FSR -DFFXM_VKLOADER_VOLK=1 $asr_inc"
fi
c++ -std=c++20 -w $cflags $fsr_flags -c android/native/vulkan/fsr.cpp -o build/host-tests/fsr.o
threads="$threads build/host-tests/fsr.o $fsr_objs"
cc -std=gnu99 $cflags -c tests/vulkan_frontend_test.c -o build/host-tests/frontend_test.o
c++ -std=c++17 $cflags tests/vulkan_test.cpp build/host-tests/renderer.o build/host-tests/volk.o $threads -o build/host-tests/vulkan_test $(pkg-config --libs sdl2) -ldl
c++ -std=c++17 $cflags tests/vulkan_rotation_test.cpp build/host-tests/renderer.o build/host-tests/volk.o $threads -o build/host-tests/vulkan_rotation_test $(pkg-config --libs sdl2) -ldl
c++ -std=c++17 $cflags tests/vulkan_fsr_test.cpp build/host-tests/renderer.o build/host-tests/volk.o $threads -o build/host-tests/vulkan_fsr_test $(pkg-config --libs sdl2) -ldl
c++ build/host-tests/frontend_test.o build/host-tests/frontend.o build/host-tests/renderer.o build/host-tests/volk.o $threads -o build/host-tests/vulkan_frontend_test $(pkg-config --libs sdl2) -lGLESv2 -ldl -lm
xvfb-run -a env SDL_VIDEODRIVER=x11 NZP_JOBS_WORKERS=${NZP_JOBS_WORKERS:-4} sh -c 'build/host-tests/vulkan_test && build/host-tests/vulkan_rotation_test && build/host-tests/vulkan_fsr_test && build/host-tests/vulkan_frontend_test'
