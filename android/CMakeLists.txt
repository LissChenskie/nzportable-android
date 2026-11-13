cmake_minimum_required(VERSION 3.22)
project(nzportable_android C CXX)
if(ANDROID)
    # CMake 3.22's generic Clang IPO flags select host gold. NDK uses lld.
    set(CMAKE_C_LINK_OPTIONS_IPO "-flto=thin;-fuse-ld=lld")
    set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto=thin;-fuse-ld=lld")
    add_link_options("-Wl,-z,max-page-size=16384")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/third_party/SDL" "${CMAKE_CURRENT_BINARY_DIR}/SDL")
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
foreach(codec CMD FLAC GME MOD MIDI OPUS WAVPACK)
    set(SDL2MIXER_${codec} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL2MIXER_VORBIS STB CACHE STRING "" FORCE)
set(SDL2MIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_MINIMP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3_MPG123 OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/third_party/SDL_mixer" "${CMAKE_CURRENT_BINARY_DIR}/SDL_mixer")
set(ENGINE "${ROOT}/third_party/vril/source")
file(GLOB CORE CONFIGURE_DEPENDS "${ENGINE}/*.c" "${ENGINE}/menu/*.c" "${ENGINE}/qcvm/*.c" "${ENGINE}/render/*.c" "${ENGINE}/tests/*.c")
file(GLOB PLATFORM CONFIGURE_DEPENDS "${ENGINE}/platform/sdl/*.c" "${ENGINE}/platform/sdl/gl/*.c")
list(FILTER PLATFORM EXCLUDE REGEX "/gl_vidsdl\\.c$")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/_build_info.h"
    "#define GIT_HASH \"7a31c3d-android\"\n#define GIT_BRANCH \"android\"\n#define BUILD_DATE \"2026-10-29\"\n")
add_library(main SHARED ${CORE} ${PLATFORM} native/gles_compat.c native/video_android.c native/android_port.c native/lan_endpoint.c native/lan_network.c native/cpu_topology.c native/thread_roles.c native/jobs.c native/vulkan/renderer.cpp native/vulkan/fsr.cpp native/vulkan/vendor/volk.c)
target_include_directories(main PRIVATE "${ENGINE}" native "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(main PRIVATE GLQUAKE PLATFORM_SDL PLATFORM_ANDROID PLATFORM_CONFIRM_IS_ENTER
    PLATFORM_DIRECTORY=sdl PLATFORM_RENDERER=gl MAX_AI_COUNT=24 PLATFORM_USES_GENERIC_GLYPHS
    PLATFORM_SUPPORTS_HIGH_FRAMERATES PLATFORM_SUPPORTS_RUMBLE)
target_compile_options(main PRIVATE -O2 -fno-strict-aliasing -fvisibility=hidden -Wall -Wno-unused-variable -Wno-unused-but-set-variable)
# SDL loads this symbol using dlsym; do not hide the application's entry point.
set_source_files_properties("${ENGINE}/platform/sdl/sys_sdl.c" PROPERTIES COMPILE_OPTIONS "-fvisibility=default")
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,--no-undefined")
target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer GLESv2 android log m)

# SPIR-V is compiled by the pinned NDK on the build host; no runtime GLSL compiler.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CMAKE_HOST_WIN32)
    set(NZP_SHADER_HOST windows-x86_64)
elseif(CMAKE_HOST_APPLE)
    set(NZP_SHADER_HOST darwin-x86_64)
else()
    set(NZP_SHADER_HOST linux-x86_64)
endif()
find_program(GLSLC NAMES glslc glslc.exe
    PATHS "${ANDROID_NDK}/shader-tools/${NZP_SHADER_HOST}" NO_DEFAULT_PATH REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vulkan_shaders.h"
    COMMAND ${Python3_EXECUTABLE} "${ROOT}/scripts/compile_vulkan_shaders.py"
        --compiler "${GLSLC}" --out "${CMAKE_CURRENT_BINARY_DIR}/vulkan_shaders.h"
    DEPENDS "${ROOT}/scripts/compile_vulkan_shaders.py"
        native/vulkan/shaders/vril.vert native/vulkan/shaders/vril.frag
        native/vulkan/shaders/lite.vert native/vulkan/shaders/lite.frag
        native/vulkan/shaders/lite_fast.vert native/vulkan/shaders/lite_fast.frag
    VERBATIM)
target_sources(main PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vulkan_shaders.h")
target_compile_definitions(main PRIVATE NZP_VULKAN VK_NO_PROTOTYPES)
set_source_files_properties(native/vulkan/renderer.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17;-Wno-missing-field-initializers;-Wno-unused-parameter")
target_link_libraries(main PRIVATE dl)

# Temporal upscaling: Arm Accuracy Super Resolution (MIT, derived from AMD FSR 2.2) with its prebuilt
# Vulkan shaders. Built as a static library using the project's volk (FFXM_VKLOADER_VOLK).
set(ASR "${ROOT}/third_party/arm_asr")
if(ANDROID_ABI STREQUAL "arm64-v8a" AND EXISTS "${ASR}/src/components/fsr2/ffxm_fsr2.cpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asr_shim/Volk")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/asr_shim/Volk/volk.h" "#undef VK_USE_PLATFORM_XCB_KHR\n#undef VK_USE_PLATFORM_XLIB_KHR\n#include \"${CMAKE_CURRENT_LIST_DIR}/native/vulkan/vendor/volk.h\"\n")
    add_library(nzp_arm_asr STATIC
        "${ASR}/src/shared/ffxm_assert.cpp" "${ASR}/src/shared/ffxm_object_management.cpp"
        "${ASR}/src/components/fsr2/ffxm_fsr2.cpp" "${ASR}/src/backends/shared/ffxm_shader_blobs.cpp"
        "${ASR}/src/backends/shared/blob_accessors/ffxm_fsr2_shaderblobs.cpp"
        "${ASR}/src/backends/vk/ffxm_vk.cpp" "${ASR}/src/backends/vk/ffxm_hash.cpp")
    target_include_directories(nzp_arm_asr PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/asr_shim" "${ASR}/include" "${ASR}/include/host")
    target_include_directories(nzp_arm_asr PRIVATE "${ASR}" "${ASR}/include/gpu" "${ASR}/src" "${ASR}/src/shared" "${ASR}/src/components"
        "${ASR}/src/backends/shared" "${ASR}/src/backends/vk" "${ASR}/src/backends/shared/blob_accessors/prebuilt_shaders")
    target_compile_definitions(nzp_arm_asr PUBLIC FFXM_VKLOADER_VOLK=1 VK_NO_PROTOTYPES PRIVATE FFXM_FSR)
    target_compile_options(nzp_arm_asr PRIVATE -std=c++20 -w -fvisibility=hidden "SHELL:-include wchar.h -include stddef.h")
    set_target_properties(nzp_arm_asr PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(main PRIVATE NZP_FSR)
    target_link_libraries(main PRIVATE nzp_arm_asr)
    set_source_files_properties(native/vulkan/fsr.cpp PROPERTIES COMPILE_OPTIONS "-std=c++20;-Wno-missing-field-initializers;-Wno-unused-parameter")
else()
    message(STATUS "Arm ASR/FSR is enabled only for arm64-v8a; FSR upscaling is compiled out for this ABI")
endif()
