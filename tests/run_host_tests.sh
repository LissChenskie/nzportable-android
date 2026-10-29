#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
cc -std=c99 -Wall -Wextra -Werror tests/test_lan_network.c -o build/host-tests/test_lan_network
build/host-tests/test_lan_network
cc -std=gnu99 -Iandroid/native tests/gles_test.c android/native/gles_compat.c -o build/host-tests/gles_test -lEGL -lGLESv2 -lm
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 build/host-tests/gles_test
javac -d build/host-tests android/app/src/main/java/org/nzportable/android/DataInstaller.java tests/DataInstallerTest.java
java -cp build/host-tests DataInstallerTest "$@"
