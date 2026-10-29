# Android native resolution, FPS and profiling

**Version:** 29 October 2026. Graphics feature branch: `feat/native-resolution-adaptive-performance-20261029`.

## Rendering design

Previously the Java launcher forced `+vid_width 960 +vid_height 540`,
the GLES driver clamped both dimensions to `1600x900` and reduced
resolution further under thermal load, down to `854x480`.

Those clamps are removed. On Android, the actual `SDL_GL_GetDrawableSize`
from the immersive EGL surface is the authoritative width and height.
Do not guess the screen size from the phone model. The in-game
`VIDEO OPTIONS -> RESOLUTION` defaults to **AUTO 100% NATIVE**.
At 100%, Quake world, HUD and menu draw **directly into the native EGL
surface** without an extra full-frame texture copy or scaling filter.
The launcher no longer overrides archived graphics variables.

Optional `90%, 80%, 75%, 60%` settings build a 3D-only GLES2 FBO.
The 3D world is presented to the display before the 2D HUD/menu
pass. Text, HUD, menu and Java RetroTouch overlay stay at **native
surface resolution**. Only 3D receives the user-requested scaling.

The same scale percentage applies to both axes. If the surface
changes size (rotation, cutout-safe layout, system bars, Android
multitasking), `VID_SDLResize` detects it and updates the FBO,
viewport and UI layout on the GL/render thread. No GLES context
recreation or hardcoded A51-specific dimensions are required.

`vid_render_scale` (100 default) and `vid_ui_scale` (100 default)
are archived cvars. Settings save to the normal game `config.cfg`.
Imported older `vid_width` and `vid_height` variables are retained
for compatibility but no longer have a hidden quality effect.

## FPS and power

Video Options provides `30, 45, 60, 90, 120` and `UNLIMITED`
via archived `cl_maxfps` (60 default on Android).
Selecting 90, 120 or Unlimited disables display VSync to avoid
unintentional 60Hz throttling; VSync remains available in the next
menu page. Quake's `Host_Frame` uses real elapsed time for the
network and simulation; changing the FPS cap must not change world
time. The main loop uses `SDL_Delay`, not a busy-wait loop.
Android lifecycle pauses the engine while backgrounded.

Android thermal status is recorded during profiling, **never used
to silently lower resolution or the selected frame limit**. GLES
redundant `glUseProgram` calls are cached, batching is retained, and
the 100%-native render path avoids an unnecessary presentation pass.
JNI thermal calls are polled at a reduced frequency. Frequent logging
is disabled unless the user explicitly enables profiling.

**A51 reality check:** The SM-A515F display normally operates at
60Hz. A `120 FPS` or `UNLIMITED` setting permits game frames faster
than the display update rate but does NOT magically make the panel
display 120 different frames each second. At native 2400x1080 the
Mali-G72 MP3 may be GPU-bound. Whether it can render 120 FPS must be
measured on hardware; no such claim is made.

## Measure before / after on a real device

1. Use **identical** map, view direction, 3D percentage, UI scale,
   FPS limiter, VSync, game assets and gameplay scenario.
2. In `VIDEO OPTIONS`, navigate to the last page and enable
   `PERFORMANCE LOG`; verify `RESOLUTION` is `AUTO`.
3. On a Windows PC with adb connected run
   `powershell -ExecutionPolicy Bypass -File scripts/profile-a51.ps1 -Seconds 180 -OutDir baseline`
   on the old APK. Repeat into `optimized` on the new APK.
4. Save game logs with the profiling cvar `nzp_profile 1`.
   The log line `NZP_PERF` reports average frames per second,
   1% low FPS, CPU frame time average/max, draw calls per rendered
   frame, one-core CPU utilization, peak RSS, resolution, and thermal
   status. Battery/thermal/memory are sampled through adb separately.
5. Compare the logs:
   `python scripts/compare_android_perf.py baseline/logcat.txt optimized/logcat.txt`.
6. Record 30+ minutes for thermal/battery results; spot-check 60,
   90, 120 and Unlimited separately, and watch for rendering artifacts.

GPU frame time is **not available** without a supported GPU timer query
extension/hardware profiler; Mali GPU counters and real power drain
cannot be inferred from CI emulator or CPU measurements.

## Automated tests and limits

`tests/gles_test.c` asserts exact 2400x1080 native pixel dimensions,
other aspect ratios/orientations and user-selected 90/80/75/60%
dimensions; it also checks actual GLES viewport scaling and native
HUD-over-world compositing on software Mesa EGL.
Host tests cover installer, LAN and touch controls. GitHub Actions
compiles ARM64 and ARMv7 APKs.

No real Samsung Galaxy A51 has been connected to the assistant's
execution environment. Hardware-only checks remain: native FPS,
frame pacing, actual 1% lows, temperatures, power draw, EGL surface
lifecycle, device cutout/safe areas, and LAN plus buying/doors
regression on a second handset.
