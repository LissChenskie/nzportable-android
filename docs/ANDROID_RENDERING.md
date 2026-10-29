# Android native resolution, rendering, and profiling

## Reason for previous blurry image

The old Android launcher forced `+vid_width 960 +vid_height 540`.
`video_android.c` additionally capped dimensions to 1600x900 and
silently clamped the render target to 1024x576 / 960x540 / 854x480
at elevated thermal-status values. The scene was then linearly stretched
to the actual Android EGL surface. No user-controlled scale was available.

## Render path (new)

- The actual drawable dimensions come from `SDL_GL_GetDrawableSize`
  for the Android game's current SDL2 EGL window, not a device model or
  hardcoded Samsung Galaxy A51 width. The Activity passes no overriding
  `vid_width`, `vid_height` or `cl_maxfps` console arguments.
- Default `vid_render_scale=100`: draw world and UI directly into the
  native default framebuffer 0. No offscreen color/depth allocation,
  fullscreen present blit, or hidden thermal reduction is involved.
- Optional `90, 80, 75, 60` percentages create a world-only framebuffer
  at the requested dimensions; **after** 3D world drawing, it is presented
  to the physical native surface. The engine then draws HUD, console,
  loading UI, menus and FPS overlay in full-resolution native pixels.
- The FBO is recreated only when the actual surface dimensions or the
  explicitly chosen scale changes; normal frames do not reallocate it.
- On Android surface resize/landscape reversal, the drawable is checked
  each frame, so dimensions and aspect are refreshed without stale FBOs.
- `vid_ui_scale` changes HUD and menu pixel sizes independently of the
  selected 3D scale. It is constrained to keep the 240-unit menu reference
  screen visible. RetroTouch uses its own normalized screen-space layout.
- Both resolution and UI cvars are archived and video-menu changes are
  saved via `Host_WriteConfiguration` immediately. The existing
  import/configuration workflow must preserve `config.cfg` on upgrades.
- The old `vid_width` and `vid_height` cvars remain registered as legacy
  compatibility variables, but no longer cap rendering.

## Menu controls

VIDEO OPTIONS, display page:

- RESOLUTION: AUTO (NATIVE) = 100%; then 90%, 80%, 75%, 60%.
- UI SCALE: 75%, 100%, 125%, 150%, capped by drawable dimensions.
- FPS LIMIT: 30, 45, 60, Unlimited (`cl_maxfps=0`). Default 60.
- SHOW FPS: HUD overlay; VSYNC: actual display presentation synchronization.
- PERFORMANCE LOG: opt-in 5-second sampled frame statistics to `logcat`.
- MORE GRAPHICS: existing FOV, gamma, ultrawide, particles, filtering,
  dithering (where supported). No quality feature is disabled automatically.

`cl_maxfps` is a frame pacing preference; VSync may also limit the
actual presentation rate. For truly uncapped FPS turn VSync off manually.
The Android event loop sleeps with `SDL_Delay` for whole unused milliseconds
rather than busy-waiting, and avoids overriding FPS based on thermal state.
The world frame delta is still computed from real monotonic elapsed time;
no game speed multiplier or new network protocol was added.

## Code-side performance changes

- Native 100% does not allocate or blit an offscreen full-screen FBO.
- 3 GLES2 stream buffer stores are allocated once; frame rotation reuses
  those stores, instead of `glBufferData` orphaning megabytes every frame.
- GLES2 fixed shader program is bound once per frame instead of every draw.
- Cached shader uniforms and existing compatible-state batching are retained.
- Android touch-overlay/thermal JNI polling interval increased from
  500ms/1000ms to 2500ms when no visibility change is needed.
- Optional profiler avoids GPU `glFinish` / synchronous readback and does
  not allocate memory per sampled frame. CPU, frame time and draw stats
  are collected only when explicitly enabled.
- Android background lifecycle already throttles event processing and
  suspends gameplay; LAN hosts backgrounded may time out remote clients.

## Profiling on a Samsung Galaxy A51

Use **the same map, game data and camera path**, brightness, battery level
and phone temperature for old versus new APK measurements.
Turn on PERFORMANCE LOG in Video Options, play for at least 5 seconds,
and retrieve logs with:

    adb logcat -d | findstr /I "NZP_PROFILE NZP_RESOLUTION"

`NZP_RESOLUTION` prints the actual surface and internal scene dimensions
after changes. `NZP_PROFILE` prints mean FPS, approximate 1% low FPS
(99th-percentile sampled frame interval), CPU thread milliseconds,
mean draw calls and vertex upload bytes, RSS (resident memory), and
the Android thermal status. GPU time, GPU load, battery power and
energy consumption are **not** sampled by this implementation; external
hardware tools or compatible GLES timer query support would be needed.
Frame wall time includes frame pacing and swap waits; it is not pure GPU time.

Suggested test matrix:

1. First launch on A51: resolution AUTO native; read
   `NZP_RESOLUTION ... render=2400x1080` (if landscape drawable is full
   2400x1080; cutouts/system bars can change the actual numbers).
2. Select 90%, confirm render size changes and UI remains sharp.
3. Return to AUTO; confirm original resolution, then restart and verify
   selection remains persisted. Try each UI scale and FPS cap mode.
4. Switch between landscape orientations, minimize and resume, verify
   menu click targets, RetroTouch joystick, door and wall purchases.
5. LAN hotspot on two phones: join, move, shoot, rounds/purchases and
   disconnect; ensure no netcode timing or packet format change.
6. Run ~30 min on the physical phone, compare 1% low FPS and thermals,
   and note battery drain. No physical A51 tests are claimed by CI.

## CI verification

`tests/test_native_resolution.c` checks exact native dimensions and
each optional percentage using 2400x1080 and odd-sized surfaces.
Existing GLES, assets, networking and touch tests remain enabled.
GitHub Actions builds ARM64 and ARMv7 APKs. Rendering screenshots,
GPU timer measurements and physical battery power are not available
in the CI environment and require Android device testing.

## Known limitations

Android Mali-G72 MP3 may not sustain 60 FPS at 2400x1080 in all waves
with many active zombies. The default stays native for visual fidelity;
users may *manually* select 90% or a lower mode if they prefer performance.
Full native HUD composition is only on Android; desktop remains unchanged.
Presenting an optional scaled world requires one fullscreen copy; at 100%
this copy is absent. No texture quality, draw distance, model or effect
settings are automatically reduced.
