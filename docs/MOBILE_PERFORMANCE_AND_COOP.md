# Mali Vulkan, hotspot co-op, big.LITTLE threads and FSR upscaling

This document describes the changes in the `feat/mobile-mali-net-threads-fsr` series and, just as
important, what has **not** been verified. Everything below was tested on Mesa lavapipe with the
Khronos validation layers and on the host; **nothing was measured on a physical Mali phone.**

## 1. Vulkan on Mali

* **Fragment shader**: `ALPHA_TEST` is a specialization constant, so opaque pipelines contain no
  `discard` (keeps early-Z / forward pixel kill on Bifrost/Valhall). Colour and fog-blend maths is
  `mediump` (fp16 on Mali); UVs, lightmap UVs and fog distance stay `highp`.
* **Pre-rotation** (`vk_prerotate`, **default 0 = off** until it is verified on devices; Video options, page 5, "PRE-ROTATION"; restart after changing): the swapchain uses
  the surface `currentTransform` so the compositor does not rotate every frame. A surface that only
  supports a rotated transform no longer forces GLES2. If the rotated extent disagrees with SDL's
  drawable size the renderer falls back to identity. Whether a given Mali driver reports a rotated
  `currentTransform` is unknown until tested on the device; the log line `NZP_VULKAN surface=... rotation=`
  shows what was chosen.
* **Pipeline prewarm** runs on all cores (see threading).

## 2. Network: Android phone hosting a hotspot, other phones joining

**Escape hatch:** Video options, page 5, "LAN COMPATIBILITY" (cvar `net_lan_legacy`) restores the original networking behaviour exactly (no Wi-Fi binding, first-interface address, fixed 1 s retransmit, 3x2.5 s connect). If co-op fails with the new code but works with this on, the cause is in these changes; the log lines `NZP_NET bind ...` show what the socket binding did.

* `LanNetwork.java` holds a low-latency Wi-Fi lock and a multicast lock while the game is in the
  foreground and tracks the Wi-Fi `Network` (even when it has no internet). Client sockets are bound to
  it with `android_setsocknetwork`, so traffic to the hotspot's `192.168.x.x` is not routed over mobile
  data. Hosting sockets are never bound.
* The advertised/local address ignores cellular (`rmnet*`) and tunnel interfaces and prefers AP/Wi-Fi
  interfaces; it is re-evaluated continuously, so enabling the hotspot after launching works.
* Transient socket errors (`ENETUNREACH`, `ECONNREFUSED`, ...) no longer drop the connection.
* Connect retries every 1.5 s (5x) instead of 2.5 s (3x): inside the server's 2 s duplicate-request
  window, so a lost ACCEPT is answered again instead of closing the new socket.
* The reliable channel retransmit timer is adaptive (0.12-1.0 s) instead of a fixed 1.0 s.
* New permissions: `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE`, `CHANGE_WIFI_MULTICAST_STATE`, `WAKE_LOCK`.

Not tested: a real two-phone hotspot session. The first thing to check on the device is the
`NZP_THREADS` / `NZP_VULKAN` / socket warnings in `adb logcat -s NZPortable SDL`.

## 3. Threads (4 big + 4 LITTLE)

The engine is single-threaded and most of it cannot safely be parallelised without running it (sound
mixing lazily loads samples through the engine cache; the server/QuakeC and client share state). So:

* `cpu_topology` detects performance/efficiency cores (cpu_capacity, max frequency, or /proc/cpuinfo
  part ids) and splits at the largest frequency gap (4+4, 2+6, 1+3+4 all handled).
* The game thread is pinned to the performance cluster with raised priority (`nzp_cpu_affinity 0`
  disables pinning). Background workers use the efficiency cluster; performance workers only join
  during start-up/loading ("burst").
* The only consumer today is **pipeline creation at start-up** (thread-safe per the Vulkan spec).
  `NZP_JOBS_WORKERS=n` overrides the worker count.

## 4. Upscaling: FSR Lite and FSR full

Video options, page 1, "FSR UPSCALING" cycles **OFF -> LITE -> FULL** (cvar `vk_fsr`: 0 off, 2 lite, 1 full;
default off). Both only work while `vid_render_scale` is below 100%, and both turn themselves off for the
session if frames stay very slow (the menu then shows AUTO-OFF).

* **LITE** (any Vulkan GPU with independent blend): my own cheap temporal upscaler, *not* AMD or Arm code. The
  world renders with a sub-pixel jitter and one full-screen pass blends the reprojected history into the
  current frame. Two variants (`vk_lite_quality`: 0 auto, 1 fast, 2 quality; auto = fast on ARM Mali):
  * *fast* (Mali-G72 oriented): **2 texture fetches per display pixel** (current frame, history). No motion
    vector target and no depth in memory: history is reprojected by one 3x3 matrix from the camera rotation
    (translation is ignored), and history that differs from the current sample is rejected by colour distance.
    Texture coordinates come straight from vertex-shader varyings so Bifrost can prefetch.
  * *quality*: 7 fetches (5 current-frame taps for a range-limited unsharp mask and clamp box, motion vector,
    history), camera motion vectors from an extra render target.
  Why fast exists: a G72 does about one bilinear texel per clock per core, roughly 2.5 Gtexel/s on an MP3, so
  7 fetches at 2400x1080 is ~7 ms per frame while 2 fetches is ~2 ms (an estimate from the hardware numbers, not a
  measurement). The remaining cost is memory traffic: read current+history, write the new history, then the
  present quad reads it again. Expect it to pay off only when the world pass is the bottleneck.
* **FULL**: Arm Accuracy Super Resolution (MIT), the Mali-oriented derivative of AMD FidelityFX Super
  Resolution **2.2**, the temporal upscaler FSR 3 builds on. Fetched by `scripts/bootstrap.py`. It stores depth
  and is several full-resolution passes, which is what made it 12 fps on a Mali-G72 and 30 fps on an Adreno 642L
  in the first tests; it is meant for modern GPUs. Needs `VK_KHR_shader_float16_int8`, `VK_KHR_spirv_1_4`,
  `VK_KHR_shader_float_controls`, `VK_KHR_shader_subgroup_extended_types`, `shaderInt16`, `independentBlend`,
  `fragmentStoresAndAtomics` (all of Arm ASR's prebuilt shaders are 16-bit SPIR-V 1.4); `NZP_VULKAN fsr_supported=`
  in the log lists each one.

What is **not** included: FSR 3 frame generation.

Limits to know about:

* Whether an upscaler helps at all depends on the frame time. At 350 fps (2.9 ms) any extra pass costing a
  few milliseconds is a loss; upscaling pays off only when the scene is clearly GPU-bound.
* Motion vectors cover the camera only. Moving entities are treated as static in the world, so fast zombies
  can leave trails; the weapon model is marked camera-locked. There is no reactive mask.
* On the synthetic test card (lavapipe, not a phone) Lite reduces the detail error vs. bilinear on a static
  view (20.9 vs 23.4) and equals it while panning; FULL does not beat bilinear there. Neither recovers detail
  thinner than half a render pixel. Real image quality and GPU cost need real devices.
* FULL uses the HDR input mode for 8-bit content because it is the configuration that was validated; the LDR
  permutation produced dark, grid-patterned output under software Vulkan.
* Licences: Arm ASR is MIT (derived from AMD FSR 2.2, MIT), see `third_party/arm_asr/LICENSES`.

## 5. Run log and crash report (no adb needed)

`<Android/data/<package>/files>/nzp-last-run.log` holds everything the game logs (starting with the device
model), `nzp-previous-run.log` the run before it. A native crash (SIGSEGV, SIGABRT, ...) appends a report with
the fault address and a backtrace; an uncaught Java exception goes to `nzp-java-crash.txt`. After a failed
co-op attempt, send the files of both phones.

## Recreate-loop fix

A driver that keeps returning `VK_SUBOPTIMAL_KHR` used to trigger a swapchain rebuild on every frame (and with FSR, a new upscaler context with dozens of pipelines each time, i.e. ~1 fps). The renderer now rebuilds only if the surface transform/extent really changed, and the FSR context survives swapchain rebuilds. Look for `NZP_VULKAN ignoring persistent VK_SUBOPTIMAL_KHR` in the log.

## Touch controls

The engine-drawn buttons/stick/look of the old touch scheme are removed (they flashed on screen when leaving a
menu); gameplay input now comes only from the RetroTouch overlay. The engine still handles taps in menus and on
the loading screen. Video options, page 5, "TOUCH BUTTONS": ALWAYS ON (default) / OFF / AUTO. AUTO hides the
overlay while a controller is detected; some phones (seen on a Poco) report a virtual input device as a
controller, which hid the overlay entirely, so the default is ALWAYS ON. The run log shows the state as
`NZP_TOUCH overlay_shown=... controller_detected=... policy=...`.
