# Mali Vulkan, hotspot co-op, big.LITTLE threads and FSR upscaling

This document describes the changes in the `feat/mobile-mali-net-threads-fsr` series and, just as
important, what has **not** been verified. Everything below was tested on Mesa lavapipe with the
Khronos validation layers and on the host; **nothing was measured on a physical Mali phone.**

## 1. Vulkan on Mali

* **Fragment shader**: `ALPHA_TEST` is a specialization constant, so opaque pipelines contain no
  `discard` (keeps early-Z / forward pixel kill on Bifrost/Valhall). Colour and fog-blend maths is
  `mediump` (fp16 on Mali); UVs, lightmap UVs and fog distance stay `highp`.
* **Pre-rotation** (`vk_prerotate`, default 1, env `NZP_VK_PREROTATE=0` disables): the swapchain uses
  the surface `currentTransform` so the compositor does not rotate every frame. A surface that only
  supports a rotated transform no longer forces GLES2. If the rotated extent disagrees with SDL's
  drawable size the renderer falls back to identity. Whether a given Mali driver reports a rotated
  `currentTransform` is unknown until tested on the device; the log line `NZP_VULKAN surface=... rotation=`
  shows what was chosen.
* **Pipeline prewarm** runs on all cores (see threading).

## 2. Network: Android phone hosting a hotspot, other phones joining

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

## 4. FSR upscaling

What is included: **Arm Accuracy Super Resolution** (MIT), the Mali-oriented derivative of AMD
FidelityFX Super Resolution **2.2** - the temporal upscaler that FSR 3 is built on. It is fetched by
`scripts/bootstrap.py` (pinned in `dependencies.lock.json`) and linked statically.

What is **not** included: **FSR 3 frame generation** (optical-flow interpolation of extra frames).
It needs accurate per-pixel motion and a GPU budget that a Mali-G72 class device does not have.

How it works: with `vid_render_scale` below 100 and `vk_fsr 1` (default), the world pass renders
jittered, writes motion vectors (camera reprojection) and depth, Arm ASR upscales to the display size,
and the HUD is drawn at native resolution on top. `vk_fsr_sharpness` controls RCAS (default 0.2).

Limits to know about:

* Needs `VK_KHR_shader_float16_int8`, `VK_KHR_spirv_1_4`, `VK_KHR_shader_float_controls`,
  `VK_KHR_shader_subgroup_extended_types`, `shaderInt16`, `independentBlend`, `fragmentStoresAndAtomics`
  (all of Arm ASR's prebuilt shaders are 16-bit SPIR-V 1.4). The log line `NZP_VULKAN fsr_supported=`
  lists each capability; on devices without them the renderer silently keeps the bilinear upscale.
  An older Mali-G72 phone with a Vulkan 1.1 driver may well be in that group.
* Motion vectors cover the camera only. Moving entities are treated as static in the world, so fast
  zombies can leave trails; the weapon model is marked camera-locked. There is no reactive mask.
* The depth buffer is stored (D32) and a motion-vector target is added, which costs bandwidth that the
  bilinear path does not pay.
* On the synthetic test card FSR was **not** better than bilinear (detail thinner than half a render
  pixel is not recoverable); the automated tests prove that it runs without validation errors, that
  jitter and the motion-vector sign are right, and that output does not have a dark border. Judging
  image quality needs real content on a real device.
* The HDR input mode is used for 8-bit content because it is the configuration validated here; the LDR
  permutation produced dark, grid-patterned output under software Vulkan.
* Licences: Arm ASR is MIT (derived from AMD FSR 2.2, MIT). See the notices in `third_party/arm_asr/LICENSES`.
