#!/bin/sh
# Touch settings screen: the core (layout, gestures, drawing of the real texts on 7 screen sizes) and the engine glue (every row
# reads and writes its own cvar), both on the host. Optional: TM_DUMP_DIR=<dir> also writes the drawn states for
# tests/render_touch_menu.py.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
cc -std=c99 -Wall -Wextra -Werror -Iandroid/native tests/test_touch_menu.c android/native/touch_menu.c android/native/touch_menu_content.c -o build/host-tests/test_touch_menu -lm
build/host-tests/test_touch_menu
# the glue test uses the real menu_touch_settings.c with its two engine includes pointed at the stand-in header
sed -e 's#"../nzportable_def.h"#"stub_engine.h"#' -e 's#"menu_defs.h"#"stub_engine.h"#' third_party/vril/source/menu/menu_touch_settings.c > build/host-tests/glue_under_test.c
cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -DPLATFORM_ANDROID -DRENDERER_SUPPORTS_DITHERING -Itests/stubs -Ibuild/host-tests -Iandroid/native \
   tests/touch_settings_glue_test.c android/native/touch_menu.c android/native/touch_menu_content.c -o build/host-tests/touch_settings_glue_test -lm
build/host-tests/touch_settings_glue_test
