# NZPortable for Android

Native Android port of [NZ:P](https://github.com/nzp-team/nzportable), using
[Vril Engine](https://github.com/nzp-team/vril-engine), SDL2 and OpenGL ES 2.
The initial performance profile targets Samsung Galaxy A51 LTE / Exynos 9611.
This is an experimental port; physical A51 performance is not yet measured.

## Build

Requires Python 3.11+, Git, JDK 17, Android SDK platform 35, build-tools 35.0.0,
NDK 27.2.12479018 and SDK CMake 3.22.1. Set `ANDROID_HOME` and `JAVA_HOME`.

```sh
python scripts/bootstrap.py --with-data
python scripts/build_apk.py --abi arm64-v8a
```

Output: `build/nzportable-android-arm64-v8a.apk` (optimized native code, test signature).
Install with `adb install -r build/nzportable-android-arm64-v8a.apk`. The first launch
extracts the game data, then press **Играть**. Without `--with-data`, import a full
PC release ZIP using the launcher's document picker. Upstream nightly URLs change;
the data checksum is deliberately pinned and must be reviewed when updating.

Android Studio can open the `android/` directory after `python scripts/bootstrap.py`.
Gradle 8.9 / Android Gradle Plugin 8.7.3: run `gradle :app:assembleDebug` there.
`-PnzpAbis=arm64-v8a,armeabi-v7a` selects both ARM ABIs. The direct SDK build script
is also provided so APK construction does not depend on Maven availability.

## Included

- GLES shaders for the engine's legacy rendering API, without desktop GL/GL4ES.
- Static world VBOs, streamed indices and batched dynamic geometry.
- Aspect-correct bounded resolution: 1280×576 on a 2400×1080 landscape surface.
- Touch movement, swipe camera, fire/aim/reload/use/jump/swap/knife/grenade/run/menu.
- SDL controllers, audio, app-private data, pause/resume and held-input reset.
- 60 FPS starting cap; thermal 30/15 FPS tiers and resolution reduction.
- Pinned dependencies, two-ABI Android CI and real GLES pixel/installer tests.

See [implementation and validation](docs/ANDROID_PORT.md). A fixed touch layout
is included; a layout editor, device visual regression, and sustained A51 thermal
benchmarking remain to be done. No claim of guaranteed 60 FPS or perfect parity.

## Tests

Linux with JDK 17, Mesa EGL and GLES2 development packages:

```sh
sh tests/run_host_tests.sh
```

For device logs on Windows: `powershell -File scripts/profile-a51.ps1 -Seconds 1800`.
Play a map during capture. FPS, heat and audio must be checked on the actual phone.

## Sources and license

Vril's C sources and SDL backend are vendored at the revision recorded in
[dependencies.lock.json](dependencies.lock.json), with copyright notices and
[GPL license](third_party/vril/LICENSE) preserved. Android code is GPL-2.0-or-later.
SDL2/SDL_mixer retain their upstream licenses. Game data retains the original
authors' rights and credits; this port does not relicense it.
Original project information and credits: [upstream hub README](docs/UPSTREAM_HUB.md).
