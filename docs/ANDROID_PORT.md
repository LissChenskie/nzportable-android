# Android implementation and validation

The port retains Vril's game/server/QuakeC VM and SDL sound/controller backends.
It does not run the Linux executable, a WebView, Wine, Termux or GL4ES.

## Graphics

`android/native/gles_compat.c` implements the legacy drawing subset actually used
by the SDL renderer with GLES 2 shaders. It translates matrix stacks, alpha
testing, texture replace/modulate, texture matrices and fog. Convex BSP fans,
triangle strips and quads become triangles. Flat shading uses provoking-vertex
colors on the CPU; no unsupported GLES flat-shading qualifier is required.

Dynamic geometry goes through three rotating VBO arenas, orphaned once per frame
and advanced by subrange uploads. Adjacent compatible
submissions coalesce, and changes to matrices, textures or raster state flush
before taking effect. BSP geometry has a separate direct GPU path: immutable
world VBOs, up to a 32 MiB cache budget, with streamed index buffers. It does not
expand/copy the static world's vertices each frame. The CPU copy is retained for
the fallback path. Wireframe/front-buffer diagnostic modes are not reproduced.

`video_android.c` renders to an RGBA/depth16 FBO, then upscales once to the SDL EGL
surface. Both dimension limits apply, preserving aspect ratio: 2400×1080 becomes
1280×576 at the 1280×720 default bounds. No MSAA or extra stencil attachment.

## Frame budget and thermal response

The default governor limits work to 60 FPS and sleeps through unused budget.
EGL vsync remains enabled. The core's maximum-FPS gate tolerates up to 1 ms of
timer jitter to avoid alternating accepted/rejected frames at display refresh.
Simulation receives actual elapsed time; no fast-math gameplay changes or
hardcoded Cortex core affinity are applied.

Android 10+ thermal status is queried once/second through one JNI method. At
moderate severity the cap is 30 FPS and 1024×576 bounds, severe 960×540, critical
15 FPS and 854×480. Workload increases only after 30 seconds at lower severity.
These are conservative starting policies, not measured A51 performance claims.

## Input and lifecycle

Android audio uses one SDL_mixer output: music is decoded there and the engine's
SFX ring is mixed into its postmix callback. This avoids opening two default
devices on Android OpenSL ES. The DMA position is atomic across audio/game threads.

SDL finger IDs own the movement stick, relative camera zone and action buttons.
Buttons are reference counted so releasing one finger cannot release another
finger's held action. Touch-generated mouse events are disabled. Menu taps use
the existing menu hit testing. Controller activity hides the touch overlay.
Background/focus loss clears held inputs; background opens pause, saves config,
pauses effects and music and avoids simulation catch-up on resume.
The game Activity uses its own app process, so exiting and launching again gets
a clean native allocator/VM while keeping the launcher and installed files.

Current touch controls have a fixed layout and a `touch_sensitivity` cvar.
A draggable layout editor and dedicated mobile options UI remain future work.
Physical controller input, rumble and existing bindings use upstream SDL code.

## Data

Game data is extracted once on a worker thread to app-private `files/game/nzp`.
The included test APK contains the official PC release data. CI's smaller APKs
use the system document picker to import a complete PC release ZIP. No shared
storage permission or all-files access is requested. ZIP traversal and size
limits, staged activation and failed-import rollback are host tested. Executables
from the PC ZIP are ignored.

`dependencies.lock.json` records source revisions and the tested data digest.
Upstream's `nightly` URL is mutable: `bootstrap.py --with-data` intentionally fails
if those bytes change. Review and update the digest when importing a newer
nightly; never silently treat a changed archive as the tested version.

## Validation and remaining device work

Real Mesa EGL tests verify rendered pixels for batching/state boundaries,
matrix push/pop and transforms, indexed packed arrays, strips/quads, flat colors,
linear fog, texture replace/modulate, static VBO draws and presentation. Java
tests verify the installer with adversarial paths, rollback and the official
release ZIP. Native builds link with `--no-undefined`; APK checks cover signature,
packaged ABI, exported SDL_main and 16 KiB library alignment.

Physical Galaxy A51 validation is still required: gameplay on multiple maps,
touch comfort, Bluetooth/USB pads, audio latency, repeated background/resume,
visual comparison with upstream, 30-minute frame-time/thermal/battery capture.
An emulator/software renderer cannot establish the Mali-G72's performance.

Android 10 x86_64 / SwiftShader smoke checks reached the native main menu,
rendered the Nacht der Untoten map and game HUD, and opened the pause/exit
dialogs through Android back and touch input. That run caught the Android
single-output audio requirement and the per-draw VBO storage churn; both were
fixed before delivering the ARM64 test APK. These are functional checks only.
