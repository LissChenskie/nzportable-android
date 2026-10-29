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
add_library(main SHARED ${CORE} ${PLATFORM} native/gles_compat.c native/video_android.c native/android_port.c native/lan_endpoint.c native/vulkan/renderer.cpp native/vulkan/vendor/volk.c)
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
    VERBATIM)
target_sources(main PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/vulkan_shaders.h")
target_compile_definitions(main PRIVATE NZP_VULKAN VK_NO_PROTOTYPES)
set_source_files_properties(native/vulkan/renderer.cpp PROPERTIES COMPILE_OPTIONS "-std=c++17;-Wno-missing-field-initializers;-Wno-unused-parameter")
target_link_libraries(main PRIVATE dl)
