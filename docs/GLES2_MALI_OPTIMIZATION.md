# GLES2 Mali-G72 optimization: implementation and validation

**Branch:** `perf/gles2-mali-a51-pass-20261029`

## Baseline and constraints

Audited current `main` (`d1b4c76`) and the latest unmerged native-resolution
branch before changing code. That branch is 80 commits ahead of `main` and
contains LAN, door/weapon USE, touchscreen, FPS and native Android graphics
fixes. This optimization branch inherits them rather than regressing to the
older `main` graphics implementation.

Target: Samsung Galaxy A51 (Exynos 9611, Mali-G72 MP3, normally 60 Hz).
No physical device is attached to CI. Do not interpret a successful build
or the Mesa software renderer as proof of A51 FPS, power or GPU timing.

Image-fidelity constraints: no automatic reduction of resolution, textures,
particles, fog, shadows, geometry, view distance or lighting; no gameplay,
QuakeC physics or LAN protocol changes. Existing world PVS, GPU world cache,
lightmap blending and particle batching remain in their original order.

## Implemented optimizations

1. **GLES2 driver-state cache.** Compare actual values before submitting
   `glBlendFunc`, `glDepthFunc`, `glDepthMask`, `glCullFace`,
   `glPolygonOffset`, `glDepthRangef`, and `glViewport`. Skip redundant
   driver calls and their formerly unconditional `NZP_GLES_Flush` calls.
   Cache per-texture MIN/MAG filter changes (with invalidation on texture
   delete); cache actual texture unit 0 bindings and vertex attribute
   enables. Existing cached GLSL program and shader uniforms are retained.
   Direct EGL surface/presentation changes invalidate tracked viewport or
   texture state rather than assuming the driver did not change.
2. **Lazily combined matrices.** Recompute projection * modelview once after
   actual matrix changes instead of before each GLES batch. Identity loads,
   identical `glLoadMatrixf`, and no-op matrix stack restores do not flush
   a buffered draw. Texture-only matrices do not invalidate the world MVP.
   Preserve vertex, color, depth and transparency order.
3. **2D HUD/menu stream.** On Android, `Hyena_Draw2DQuads` appends packed
   GPU-ready vertices directly to the existing streamed GLES batch. Removes
   the intermediate positions, UVs and RGBA arrays and their per-vertex
   color conversions. Other platforms retain the existing path.
4. **BSP dynamic index compaction.** Skip copying an index span when it
   is already at its final offset. Texture and lightmap grouping, PVS,
   dynamic lightmap updates, animated textures and alpha-test behavior stay
   unchanged. No new per-frame allocations.
5. **Framebuffer and shader behavior.** Preserve previous full-native
   EGL rendering with no fullscreen FBO copy at 100%, and optional
   user-selected world-only scale. The existing three rotating stream
   VBOs and shader uniform cache remain. `glIsEnabled` readbacks from the
   fullscreen presentation pass are replaced with synchronized internal
   raster-state tracking; software-emulator `glFinish` workaround remains.
6. **CPU and frame pacing.** Change the Java overlay only on actual
   game/menu transitions; no repetitive JNI UI calls. Android SDL owns one
   FPS scheduler: remove the duplicate `Host_FilterTime` gate, and use an
   interruptible `nanosleep` for the remaining budget instead of a busy
   loop. Host simulation/net updates continue using real elapsed time.
   At normal temperatures the user's resolution and FPS remain unchanged;
   the Android OS's explicit *critical* or *emergency* thermal status can
   temporarily lower the FPS budget, never image quality. The menu exposes
   the thermal safety cap.
7. **Opt-in GPU-command counters.** `NZP_GLES_GetFrameStats` provides an
   allocation-free, backend-neutral POD snapshot of draw calls, useful
   flushes, texture changes, uniform updates, program switches, VBO/IBO
   transfers and avoided redundant changes. Profiling is off by default.

## Pixel correctness

`tests/gles_test.c` draws reference HUD quads using the original
client-array path, then draws the same overlapping alpha-blended quads
via the new packed GLES path. It compares every RGBA framebuffer byte
within ±1. Other tests cover cached sampler changes, skipped duplicate
state mutations, matrix transforms, GLES2 fog, BSP-style indexed VBO
draws, frame arenas, and native 2400×1080 / optional scaling viewports.
Tests run using Mesa EGL/GLES2 software rasterization in GitHub Actions.

**These tests cannot cover every physical map/material.** Especially verify
dynamic lightmaps, water, semitransparent effects, weapon models and HUD
on the A51 itself before merging.

## Why single-pass lightmaps and shader variants are not yet deployed

The current `r_world.c` groups opaque base textures by texture index,
then performs a separate pass grouped by lightmap atlas. Dynamic and
animated lightmaps, brush models, alpha-tested textures and fog have
different rendering conditions. Combining passes correctly would require
new joint material/lightmap grouping and lightmap UV attributes in the
GLES shader. Blindly multiplying base texture by a lightmap in one
shader may alter the original blend equation, dynamic brightness and
transparency. That needs reference captures on several maps before rollout.

Specializing the universal fog/alpha shader similarly adds program
switches. Without actual Mali timestamps/visual captures, any claimed
performance improvement would be conjecture. The program and state
caching changes above are directly verified, and the two-pass renderer
is retained as the correctness baseline.

The API-neutral `nzp_render_stats_t` and separation of Android
surface/framebuffer code from Hyena geometry provide a small starting
point for a future alternative graphics backend. **No Vulkan SDK,
shader, device, swapchain or graphics API has been added.**

## Device benchmarking (required before merging)

Enable `VIDEO OPTIONS -> PERFORMANCE LOG`, select `AUTO 100%`,
the same frame cap and VSync setting for both builds. Use the same
map, player view direction, zombie count and network state.

On Windows with ADB:
```powershell
powershell -ExecutionPolicy Bypass -File scripts/profile-a51.ps1 -Seconds 180 -OutDir .\before
powershell -ExecutionPolicy Bypass -File scripts/profile-a51.ps1 -Seconds 180 -OutDir .\after
python scripts/compare_android_perf.py before/logcat.txt after/logcat.txt
```

Profiler outputs average FPS, 1% low FPS, frame-time P50/P95/P99,
per-frame CPU-thread time, draw calls, flushes, shader/texture/uniform
events and VBO/IBO submitted bytes, and peak resident memory.
ADB script samples CPU, display, thermal, memory and battery states
every ten seconds. Measure battery/energy using an external power
meter or trustworthy device power counters when available.

For a complete acceptance pass: two phones over Hotspot,
buy and open doors; play with dense zombie waves, water, dynamic
lights, smoke/particles and menus; use 60 FPS at native resolution
for at least 30 minutes. Never use a graphics downgrade to inflate
benchmark results. GPU time (Mali counters or disjoint timer queries)
is **not measured** without suitable hardware/tools.

## Before/after measured performance

| Metric | Before | After |
| --- | --- | --- |
| Average FPS | Not measured on A51 | Not measured on A51 |
| 1% low FPS | Not measured | Not measured |
| P99 frame time | Not measured | Not measured |
| Draw calls/frame | Not measured | Not measured |
| CPU thread time/frame | Not measured | Not measured |
| GPU time/frame | Not measured | Not measured |
| RAM | Not measured | Not measured |
| Battery power / temperature delta | Not measured | Not measured |

Software EGL correctness tests provide **functionality evidence**, not
performance measurements. Real before/after data can be filled using
the scripts above.
