# Camera motion blur

Optional (default **OFF**), configured in *Options > Video* pages 6 and 7. Vulkan is the main implementation;
GLES2 gets a smaller one that blurs camera rotation only.

## How it works (Vulkan)

Gather blur along the camera's motion, folded into the pass that already exists. There are **two paths**, picked automatically by
*Movement blur* (the pipelines are specialised, so each variant contains only what it uses):

* **Rotation only (Movement blur = 0, the LOW and MEDIUM presets).** The fast path. The streak comes from the camera
  reprojection matrix alone, so **there is no motion-vector image, no second render target and no vector fetch**. The camera-locked
  weapon is not drawn into the world image at all: those draws are recorded and **replayed in the swapchain pass right after the
  blur**, at display resolution. The gather therefore never sees the weapon: it cannot be blurred and cannot leak into the
  background (no halo, at any quality). A frame in which no pixel would move by 0.75 px (camera still) skips the blur completely
  and does the plain copy.
* **With vectors (Movement blur > 0, HIGH and ULTRA, or whenever Lite quality / Arm ASR already produce vectors).**
  Adds walking/strafing parallax.

1. **World pass (vector path).** The world is drawn offscreen with the two-target layout FSR Lite (quality) and
   Arm ASR already use: colour + `RG16F` motion vectors (previous minus current position, in pixels of the world image, camera
   motion of static geometry). Depth is not stored (it stays in tile memory). The camera-locked weapon writes an exact 0 vector.
   *Off* adds no target, no pass and no vector output. The rotation-only path draws a plain colour world image.
2. **Composite = blur.** The renderer already draws one full-screen quad that puts the 3D image into the swapchain pass, and
   the HUD is drawn after it. The blur **replaces that quad's fragment shader** (`blur.frag`), so at *Effect resolution: Full* there
   is **no extra render pass and no extra intermediate image**. It reads the final image (the world, or the output of Lite / FSR 1.0 /
   Arm ASR), so it works on top of every upscaler. The HUD stays sharp because it is drawn afterwards.
3. **Effect resolution < Full** (Low/Medium/High use half or three quarters): the gather runs in one small extra pass into
   an `RGBA8` image (blur in rgb, blur weight in alpha), and the swapchain pass mixes `sharp image` and `blur` by that weight.
   Everything that does not move keeps the full-resolution detail of the 3D image.

Per pixel (`blur.frag`):

* `v_total` from the motion-vector image, `v_rotation` analytically from the 3x3 camera reprojection (the one FSR Lite uses; exact
  for a turning camera, independent of depth), `v_translation = v_total - v_rotation` (parallax). So **Camera Blur** and **Movement
  Blur** are really independent gains: `v = shutter * (camera * v_rotation + movement * v_translation)`.
* Exactly-zero vectors (weapon, nothing drawn) are never blurred. Velocities under 0.75 px are left alone (a still picture is
  pixel-identical, see tests); the blur fades in up to 3 px, so it never pops.
* **Fixed shutter time** (`intensity / 60 s`), not a fixed fraction of a frame: the streak length is `displacement * T / dt` with the
  real frame interval, clamped to 4..100 ms. The same camera motion looks the same at 30, 60 and 120 FPS (the 60 vs 120 FPS test
  gives identical pictures) and a hitch frame cannot smear the screen.
* Soft length cap `len / (1 + len / max)`: linear for short streaks, bounded for huge ones (snap turn, respawn). History resets
  (teleport, new map, first frame after a rebuild) draw that frame unblurred.
* Taps are spread over `[-v/2, v/2]` (an open shutter, no lag). **One tap per ~2 px of streak**, never more than *Blur Samples*,
  so slow pixels cost little. Interleaved-gradient noise (computed from the logical display position, so it is the same at any surface
  rotation and effect resolution) shifts the taps inside their cell: no ghost copies, static fine grain, no flicker.
* Taps outside the screen are dropped instead of smearing the border pixels.
* **Quality HIGH** (vector path only) additionally compares every tap's vector with the pixel's own and rejects taps that move
  differently. This is what stops the weapon (and nearer geometry) from smearing into the background; FAST skips it (one fetch per
  tap instead of two). On the rotation-only path the setting has no effect: there is nothing to reject.

No history texture is used, so there is **no ghosting** by construction, and nothing to reset on a resolution change.

## Settings

| Setting | Values | Notes |
|---|---|---|
| Motion blur | OFF / LOW / MEDIUM / HIGH / ULTRA / CUSTOM | cvar `mblur_mode`. Presets ignore the other cvars. |
| Blur intensity | 10%..**120%** | shutter time as a fraction of 1/60 s (120% = 1/50 s); also scales the longest streak. Higher values are clamped |
| Camera blur | 0..100% | rotation gain |
| Movement blur | 0..100% | translation gain, **Vulkan only** (shows `VULKAN ONLY` on GLES2). **0 = the faster rotation-only path**; any value above 0 needs the vector target |
| Blur samples | 4..16 | upper bound of taps per pixel |
| Blur quality | FAST / HIGH | per-tap velocity check (vector path only) |
| Effect resolution | FULL / 75% / HALF | resolution of the blur pass relative to the display |

| Preset | Intensity | Camera / Movement | Samples | Quality | Effect resolution |
|---|---|---|---|---|---|
| LOW | 30% | 100% / 0% (rotation only) | 6 | FAST | HALF |
| MEDIUM | 50% | 100% / 0% (rotation only) | 8 | FAST | HALF |
| HIGH | 75% | 100% / 60% (vectors) | 10 | HIGH | 75% |
| ULTRA | 100% | 100% / 100% (vectors) | 12 | HIGH | FULL |

LOW and MEDIUM were changed from the first version, which had walking blur (and so the vector target) in every preset: they are now
the fast path. Turn *Movement blur* up in CUSTOM if you want it back. The `mblur_movement` default is now 0.

Choosing a preset copies its values into the individual cvars, so choosing CUSTOM afterwards starts from what you saw.
Editing a single setting while a preset is selected switches to CUSTOM; while OFF the rows show `-` and do nothing.
Everything applies live: turning the effect on/off or changing *Effect resolution* rebuilds the render targets
(like toggling an upscaler); the rest applies on the next frame. All cvars (`mblur_mode`, `mblur_intensity`, `mblur_camera`,
`mblur_movement`, `mblur_samples`, `mblur_quality`, `mblur_scale`) are archived. A config without them keeps the old behaviour (OFF).

## GLES2

ES2 has one colour attachment, so there is no motion-vector image. `NZP_GLES_PresentBlur` replaces the plain world-to-screen
copy (one pass, no extra target; the world is rendered into a texture, as it already is for a lowered resolution) and blurs the
**camera rotation** analytically from the same reprojection matrix: same shutter time, soft cap, jitter, edge rule and reset rule.
Limitations, by design: no movement/parallax blur; the camera-locked weapon cannot be told apart from the world and is blurred with
it; quality and effect-resolution settings do not apply (the GLES2 pass always runs at display resolution with the per-pixel
tap count). If the driver rejects the shader the effect stays off and the game continues (`NZP_GLES_MotionBlurFailed`).

## Cost on a tile-based GPU (Mali-G72 MP3), by construction

* Extra memory. Rotation only: **0 bytes** at FULL effect resolution, otherwise just the small `RGBA8` blur image. Vector path: the
  `RG16F` vector image per frame in flight (skipped if Lite quality / Arm ASR already has it) plus that image. At 1080x2400, world
  100%: about 19.8 MiB of vectors (+4.9 MiB at HALF, +11.1 MiB at 75%). The tests check the exact byte counts.
* Per frame traffic that the rotation-only path removes: the vector image write (4 bytes/pixel) and read, and the vector fetches.
* Offscreen rendering is required for any post effect; at render scale 100% without an upscaler this is the only structural cost
  (colour + vectors are written to memory once and read once). At lower render scale or with an upscaler the offscreen target already exists.
* No depth write-back, no history image, no extra sync: the only new pass (below Full) has `DONT_CARE` load and one external
  dependency in, one out.
* Fetch budget at 1080x2400, **worst case** (every pixel on a longest streak; real frames are far below because static pixels
  take 2 fetches and short streaks use few taps):

| Setting | blur pixels | max fetches/pixel | still pixel | worst case per frame | at 60 FPS |
|---|---|---|---|---|---|
| LOW (rotation only) | 0.65 M | 7 | 1 | 9.7 M | 0.58 G/s |
| MEDIUM (rotation only) | 0.65 M | 9 | 1 | 11.0 M | 0.66 G/s |
| HIGH (vectors) | 1.46 M | 22 | 2 | 37.3 M | 2.24 G/s |
| ULTRA (vectors) | 2.59 M | 26 | 2 | 67.4 M | 4.04 G/s |
| first version MEDIUM (vectors) | 0.65 M | 18 | 2 | 16.8 M | 1.01 G/s |

  With the camera still the rotation-only path skips the blur entirely (no pass, no fetches).

  These come from the shader code, not from a measurement. On a three-core Mali-G72 **HIGH and ULTRA at native 1080x2400
  are expected to be too heavy for 60 FPS during fast turns**; LOW/MEDIUM are the intended presets for the Galaxy A51. This must be
  confirmed on the device. How many FPS the rotation-only path gives back depends on where the first version was losing them (the
  vector image bandwidth, the gather fetches, or the fixed offscreen copy); that is not known without a device measurement.

## What was verified, and what was not

Run by `tests/run_vulkan_tests.sh` and `tests/run_host_tests.sh` on a **software** Vulkan (lavapipe, validation layers on, zero
validation errors) and Mesa llvmpipe GLES2:

* Vulkan: a still camera gives a pixel-identical image to blur off; blur only along the motion (horizontal pan 34% of the sharpness,
  vertical pan the mirror image); weapon pixels unchanged (47.36 -> 47.36) and the HUD identical; weapon halo next to the weapon
  `-18.7` clean, `-6.0` FAST, `-17.7` HIGH; camera gain and movement gain each switch only their own part; 60 vs 120 FPS identical;
  Blur Samples, Intensity and Effect resolution change the image; memory accounting exact; borders keep their brightness; a reset
  frame is unblurred; toggling on/off at runtime; on top of Lite fast, FSR 1.0 and Arm ASR; unchanged under pre-rotation 90 degrees;
  with a lowered 3D resolution. Shaders compile with both `glslangValidator` and `glslc` (the Android build's compiler) and the blur
  test gives the same numbers with either.
* GLES2: still image identical to the plain copy, directional blur, frame-rate independence, movement gain ignored, intensity and samples,
  reset frame, and the engine's own drawing still works after the blur program was used.
* Settings logic (`tests/test_motion_blur.c`): presets, OFF, CUSTOM, clamping, snapping, the shutter formula.
* Rotation-only path (Vulkan): a still camera is pixel-identical to blur off; for a pure rotation the picture is identical to the vector
  path (mean difference 0.00); weapon pixels exactly the unblurred ones and no halo even at FAST quality (red-green next to the weapon
  `-18.7`, clean `-18.7`; the vector path at FAST gives `-6.0`); the weapon drawn through `NZP_VK_Draw` and through
  `NZP_VK_DrawBuffers` is deferred identically; 0 bytes of extra memory at FULL and exactly the half-resolution image at HALF; works
  on top of Lite fast and FSR 1.0, with a lowered render scale, with surface pre-rotation and with a reset frame.
* Intensity 120%: blurs more than 100%; values above 120% are clamped (Vulkan and GLES2).
* `tests/vulkan_motion_blur_bench.cpp` prints relative cost on lavapipe plus the CPU time of command recording (about 0.03-0.09 ms per
  frame; 0.03 ms with blur off). Its wall times are a **software rasteriser**, useful only to compare settings with each other. In that
  proxy the rotation-only path was about 7% faster than the vector path at MEDIUM settings and needed 0.99 MiB instead of 4.94 MiB; do
  not read that as the gain on a Mali GPU.

**Not verified here:** the FPS effect of any of this, and anything else on real hardware (Mali-G72 timings, thermals, real frame pacing); the on-device look in real maps
(moving entities, sky and particles are treated as static world geometry: they get camera blur only, never their own motion blur, and
a sky drawn as view-dependent geometry may show some parallax blur while walking; not checked); the menu rendering; the full Gradle/NDK build
(no Android SDK in the environment: the touched native files were compiled on the host, `video_android.c` and `menu_video.c` with the
Android build's defines).
