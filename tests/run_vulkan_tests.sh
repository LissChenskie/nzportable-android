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
cc -std=gnu99 $cflags -c tests/vulkan_frontend_test.c -o build/host-tests/frontend_test.o
c++ -std=c++17 $cflags tests/vulkan_test.cpp build/host-tests/renderer.o build/host-tests/volk.o -o build/host-tests/vulkan_test $(pkg-config --libs sdl2) -ldl
c++ build/host-tests/frontend_test.o build/host-tests/frontend.o build/host-tests/renderer.o build/host-tests/volk.o -o build/host-tests/vulkan_frontend_test $(pkg-config --libs sdl2) -lGLESv2 -ldl -lm
xvfb-run -a env SDL_VIDEODRIVER=x11 sh -c 'build/host-tests/vulkan_test && build/host-tests/vulkan_frontend_test'
