# Native Vulkan 1.1 backend — Android test build

Status: experimental, hardware qualification pending. Based on the GLES2 Mali
optimization branch (`1a0d4c99`), preserving its native-resolution, touch, LAN,
UI scaling, frame limiting and QuakeC behavior. GLES2 remains available.

## Run

Import the same NZPortable data as before. In the Android launcher choose
**Vulkan 1.1 (test)**, press **Apply graphics API**, then Play. Alternatively,
Configuration → Video Options → fourth page → Graphics API, then restart the
app. Auto deliberately uses GLES2 until a driver/device combination has been
qualified on physical hardware. Unsupported Vulkan 1.1, initialization failures,
and an abnormal preceding Vulkan process exit select GLES2. The launcher can
reset the failure marker and change the API without entering the renderer.

Defaults remain 100% native resolution and VSync. Lower resolution is a user
choice, with a separate native-resolution HUD. No texture, distance, lighting,
particle or model quality settings are silently lowered by this change.

## Implemented rendering

`android/native/gles_compat.c` is now a shared Vril fixed-function **frontend**:
matrices, primitive expansion, color, fog and geometry batching are shared.
When Vulkan is active it sends native vertex/index/material data to
`android/native/vulkan/renderer.cpp`. It does not create a GL context or submit
GL driver calls. GLES2 continues to use its own existing shader/VBO implementation.
No GL4ES, ANGLE, Zink, OpenGL-on-Vulkan library or software renderer is shipped.
Android rejects CPU-type Vulkan devices; host Mesa is used only for tests.

The backend supports BSP/static GPU meshes and indexed draws, brush models,
animated models and textures, particles, sprites, water, sky, alpha discard,
ordered blending, linear/exp/exp2 fog, texture transforms, dynamic/animated
lightmap updates, the existing HUD/menu/text, and native UI over optional scaled
3D. Existing PVS/frustum traversal and gameplay/network code remain shared.

Opaque eligible BSP surfaces are grouped by page, animated base texture and
lightmap atlas. They use two samplers and two UV streams in one draw. The shader
preserves the original `DST_COLOR/SRC_COLOR` lightmap multiplication, including
fog-before-lightmap order and the diffuse attachment's 8-bit quantization.
Masked, water, underwater, emissive and special/debug paths retain their
existing dedicated rendering. `vk_single_pass 0` selects the reference two-pass
BSP path for comparison; default is 1. Shader specialization removes the second
sampling branch from ordinary materials.

## Source → implementation → verification

| Principle and technical source | Applied code | Evidence / remaining measurement |
|---|---|---|
| [Arm best-practices index](https://developer.arm.com/mobile-graphics-and-gaming/vulkan-api-best-practices-on-arm-gpus), [Khronos render-pass sample](https://docs.vulkan.org/samples/latest/samples/performance/render_passes/README.html): avoid unnecessary attachment memory traffic | Color CLEAR/STORE, depth CLEAR/DONT_CARE with transient image and lazy memory preference; redundant pre-draw clears skipped | Vulkan validation and pixel tests pass; external-memory bandwidth and energy savings require Mali counters |
| [Samsung pipeline recommendations](https://developer.samsung.com/galaxy-gamedev/resources/articles/usage.html): compile early, cache by state, persist driver-compatible cache | Common interleaved/BSP layouts and states prewarmed; cached key ignores irrelevant state; cache header vendor/device/UUID checked; atomic cache-file rename | Cache reload test; unexpected runtime pipeline misses are logged, not concealed; exhaustive no-stutter coverage not yet proven |
| [Samsung descriptor/buffer guidance](https://developer.samsung.com/galaxy-gamedev/resources/articles/usage.html), [Khronos descriptor sample](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html) | Dynamic UBO offsets aligned to queried limits; persistent frame mappings; reusable staging chunks; two fence-protected frame slots; texture descriptors kept immutable while referenced by submitted frames | Resource mutation/deletion and indexed draw tests under validation; no per-draw uniform descriptor rewrite |
| [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator), [quick start](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/quick_start.html) | VMA 3.1.0 suballocation, host-visible buffers with explicit flush/invalidate, device-preferred images; dynamic buffer/image versions reused only when no submitted frame retains them | Validation lifetime tests pass; memory footprint and allocation pressure on A51 remain to measure |
| [Khronos semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html) | Acquire semaphores per frame slot, present-wait semaphores per swapchain image; queue submission fences protect resources | Repeated frames, resolution/VSync changes, surface recreation and shutdown tests pass |
| [SDL Vulkan surface API](https://wiki.libsdl.org/SDL2/CategoryVulkan), [Android Vulkan](https://developer.android.com/ndk/guides/graphics) | SDL loader/extensions/surface; queried device version, queues, formats, limits and presentation modes; OUT_OF_DATE/SURFACE_LOST recovery | SDL/Mesa surface-recreation test; real Android background/foreground and rotation still required |
| [Android orientation](https://developer.android.com/games/optimize/vulkan-prerotation) | Identity surface transform requested to retain SDL orientation; unsupported non-identity-only surfaces fall back to GLES2 | No untested shader pre-rotation is enabled; optimized pre-rotation remains future work |

The full Arm document linked by its index redirected to an inaccessible support
site in this environment. The corresponding attachment rules were studied in
the accessible Khronos sample and Samsung article; no claim of having read the
blocked document is made.

Also inspected [vkQuake](https://github.com/Novum/vkQuake) at
`2e2413e8b30631624fd92ee4d3be81c2010092e5`, specifically `Quake/r_passes.c`
attachment setup. Its GPL-2.0 render-pass approach informed the review; its
newer engine/SDL3/Vulkan architecture was not copied wholesale into Vril.
Vendored Volk and VMA retain their MIT license files and pinned revisions in
`dependencies.lock.json`.

## Benchmark and telemetry

Fourth video page offers explicit Balanced (60), Maximum (120), and Battery
Saver (30) presets. These change only FPS/VSync by the user's action.
Existing thermal protection and Android foreground sleeping remain in place.

**Offscreen Benchmark** renders 600 frames of the current scene without
acquiring/presenting swapchain images (Vulkan), or into a native FBO without
swapping (GLES2). It drains GPU work once at the end and appends
`nzp/renderer-benchmark.csv`. It restores the prior FPS/VSync/profiling settings.
It never changes world resolution or simulation timing. Use a recorded demo
with identical content/settings for repeatable comparison; a live scene alone
is not a controlled workload. Do not interact during the test.

CSV includes completed-frame throughput, submission-interval 1% low/P95/P99,
CPU render-thread time, and available Vulkan GPU draw-command-buffer timestamps.
The interval percentiles describe submission pacing, not independently sampled
per-frame presentation/completion latency. GPU samples arrive from completed
older frame slots and exclude the separate upload command buffer. Unsupported
GPU timing is -1. Neither throughput nor submission rate is display refresh.

The optional existing `NZP_PERF` log now identifies the active backend and last
completed Vulkan GPU draw duration. The existing ADB scripts can collect logs,
process memory and thermal information. They do not magically measure GPU
bandwidth, power or all CPU allocations; those require device-specific tooling.

## Automated verification

- `tests/run_host_tests.sh`: existing GLES/EGL pixels, batching/matrices/fog,
  HUD reference comparison, touch actions/sprint, LAN parser/broadcast and ZIP
  installer tests.
- `tests/run_vulkan_tests.sh`: real SDL/Vulkan device with validation layers;
  pixels, fog, blending, packed textures, immutable in-flight texture updates,
  indexed static meshes, buffer deletion, optional world resolution/native HUD,
  offscreen completion counts, surface recreation, pipeline cache reload.
- The native two-pass and combined-lightmap synthetic images are compared
  across the full 64×64 image: tolerance one value per 8-bit channel.
- `tests/build_scene_smoke.py` builds the actual Vril engine on Linux with dummy Android services and
  pinned NZPortable data (`8edbcbfc…85cd`); it loaded `ndu` (Nacht der Untoten)
  using Vulkan and produced world/weapon/HUD screenshots with validation enabled.
  This is not an Android device or touch/lifecycle test.
- ARM64 and ARMv7 APK builds and signature/ZIP alignment checks are required.
  GitHub Actions contains independent GLES, Vulkan and APK jobs.

The full-engine smoke harness also completed a 600-frame Vulkan offscreen run,
verified CSV output and returned to presentation. The software-run CSV in
`docs/evidence/offscreen-benchmark-host.csv` is correctness evidence only; it
includes startup/capture overhead and is not an A51 or steady-state result.

### Controlled scene comparison (host correctness evidence)

The same fixed spawn view on `ndu`, with the test harness holding client time,
was captured at frame 240 using native Vulkan in both cases. Excluding the
changing FPS counter (top-left 250×70 pixels), the maximum channel difference
was **1/255**, mean absolute difference **0.000432** channel values, and no
pixel exceeded a one-value difference. Both runs reported zero validation
errors and clean Vulkan shutdown.

| Scene statistic | Two-pass reference | Combined eligible surfaces |
|---|---:|---:|
| Base/material draws | 10 | 12 |
| Remaining lightmap draws | 2 | 2 |
| Base/material indices | 1329 | 1329 |
| Separate lightmap indices | 1329 | 198 |

Pairing base textures with lightmap atlases increased material groups here.
The combined path reduced repeated lightmap geometry by 85.1%, **not** total
draw count. This is a measured workload change, not a measured Mali speedup.
Use `vk_single_pass 0/1` with matched demo runs to establish the actual tradeoff.

## Limits and physical A51 gate

There is no physical Galaxy A51 in this environment. A51 average FPS, 1% low,
P99, CPU/GPU frame times, memory bandwidth, energy and 30-minute thermal behavior
are **not measured**. Neither 120 internal FPS nor stable native-resolution 60
FPS is established. Host llvmpipe FPS must not be used to predict either result.

Before merging, test native 2400×1080 surface dimensions (actual usable surface
may exclude cutouts/bars), all menu/UI scales, optional lower resolutions,
masked textures, dynamic lights, water, particles, many zombies, purchases,
doors, weapons, LAN host/client, audio, background/resume, rotation, repeated
launches and GPU-driver failure recovery. Compare identical demo runs on GLES2
and Vulkan after thermal cooldown; record the CSV and ADB logs and capture
Mali bandwidth/energy data using supported hardware profilers.

Remaining engineering limits: auto has no physical-driver allowlist;
non-identity-only surfaces use GLES2; unknown pipeline state/layout combinations
can compile on first encounter; per-frame geometry/uniform arena is currently
16 MiB per slot and errors explicitly on exhaustion; CPU VM/physics SIMD or
parallelization was not guessed without profiling; performance/energy superiority
of this first Vulkan implementation is not yet demonstrated on Mali-G72 MP3.
