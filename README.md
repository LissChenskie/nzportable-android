# NZPortable Android

[![Android APK CI](https://github.com/LissChenskie/nzportable-android/actions/workflows/android.yml/badge.svg?branch=main)](https://github.com/LissChenskie/nzportable-android/actions/workflows/android.yml)
[![Experimental release](https://img.shields.io/badge/release-v0.1.0--android--20261029-orange)](https://github.com/LissChenskie/nzportable-android/releases/tag/v0.1.0-android-20261029)
[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](third_party/vril/LICENSE)

An **experimental native Android port** of [Nazi Zombies: Portable (NZ:P)](https://github.com/nzp-team/nzportable), built on the [Vril Engine](https://github.com/nzp-team/vril-engine), SDL2, OpenGL ES 2.0 and an optional Vulkan 1.1 renderer.

The primary optimization/testing target is the **Samsung Galaxy A51 LTE** (Exynos 9611 / Mali-G72 MP3). The port keeps the original game/server logic and QuakeC VM instead of emulating a PC executable.

> **Development status:** The CI builds and automated host graphics tests pass. On-device FPS, battery consumption, temperature, fog correctness across all maps and complete gameplay compatibility are **not yet verified**. Vulkan is experimental; **Auto selects OpenGL ES 2.0**. This project does **not** promise 60 or 120 FPS on the Galaxy A51.

## Download / Скачать APK

**Current experimental release: [NZPortable Android — 29 October 2026](https://github.com/LissChenskie/nzportable-android/releases/tag/v0.1.0-android-20261029)**

| Download | Devices | Notes |
| --- | --- | --- |
| **[Android ARM64 APK](https://github.com/LissChenskie/nzportable-android/releases/download/v0.1.0-android-20261029/nzportable-android-arm64-v8a.apk)** | Samsung Galaxy A51 and other compatible 64-bit Android devices | Recommended build |
| [Android ARMv7 APK](https://github.com/LissChenskie/nzportable-android/releases/download/v0.1.0-android-20261029/nzportable-android-armeabi-v7a.apk) | Compatible 32-bit Android devices | Alternative build |
| [SHA256 checksums](https://github.com/LissChenskie/nzportable-android/releases/download/v0.1.0-android-20261029/SHA256SUMS.txt) | Both APKs | Integrity verification |

Minimum Android API level: **26 (Android 8.0)**. Actual compatibility depends on the device and graphics drivers.

**Installation / Установка:**

1. Download the **ARM64 APK** for the Galaxy A51 and install it.
2. Open the NZPortable launcher. Tap **«Импортировать игровые данные из ZIP»** and select a complete, compatible PC NZPortable game-data ZIP. It must contain the `nzp` game folder and `progs.dat`. **The release APK does not include the game data.**
3. Leave the renderer on **«Авто (проверенный OpenGL ES 2.0)»** and press **«Играть»**.
4. To try Vulkan, choose **«Vulkan 1.1 (тестовый)»**, tap **«Применить графический API»**, then launch the game. If Vulkan fails to initialize, the port can fall back to GLES2.

**По-русски:** Скачай ARM64 APK, установи его и открой лаунчер. Импортируй полный ZIP с игровыми данными NZPortable, затем нажми **«Играть»**. Для Galaxy A51 сначала используй **OpenGL ES 2.0** — Vulkan пока тестовый.

> APKs are signed using a **development/test key**, not a stable production signing key. If Android rejects an update because its signature differs from an earlier test APK, back up any necessary app data before uninstalling the older version; uninstalling may delete imported files, settings and saves.

## Features

| Area | Implemented |
| --- | --- |
| **Rendering** | Native GLES2 backend; optional Vulkan 1.1 backend with fallback to GLES2 |
| **Graphics quality** | Native display resolution by default; user-selectable render scale/resolution, native-resolution UI, existing textures and effects |
| **Performance** | Batched geometry, cached GLES/Vulkan draw state, static BSP GPU buffers, optimized texture uploads, reduced temporary allocations and Vulkan staging memory reclamation |
| **Lighting and fog** | Dynamic lightmaps and fog; Vulkan combined lightmapped BSP fog fix (still needs device-side visual validation) |
| **Controls** | Touch movement, camera swipes, fire/aim/reload/use/jump/weapon swap/melee/grenade/sprint/menu; SDL controller support |
| **LAN co-op** | Host over Wi-Fi or a phone hotspot; browse LAN servers or connect by IP; separate server name and editable player nickname (up to 15 characters) |
| **Map selection** | Built-in and user maps; swipe/page/wheel navigation on Android (three built-in or four custom maps per page) |
| **Android lifecycle** | Audio, pause/resume handling, input reset, app-private game-data import |
| **Profiling** | Renderer performance counters, Vulkan CPU-stage diagnostics and offscreen benchmark CSV |

Graphics and performance presets are user choices. They are **not** evidence of achieved frame rate. See [Vulkan implementation and measurements](docs/VULKAN_ANDROID.md) and the [fog, co-op and memory fixes](docs/A51_COOP_MAP_FOG_FIXES.md).

## Local multiplayer / Кооператив

1. Connect phones to the **same Wi-Fi network**, or connect other phones to the host's **mobile hotspot**.
2. On the host: **COOPERATIVE → CREATE LAN GAME**. Set **PLAYER NICK**, server name, player slots and map. Start the host/lobby.
3. On joining phones: **COOPERATIVE → PLAYER NICK**, then **FIND LAN GAMES**. Use **CONNECT BY IP** if discovery does not find the host.
4. Both host and clients need compatible game data and a working local network connection.

The nickname identifies the **player**; the server name identifies the **hosted game**. LAN discovery depends on the local network and hotspot configuration. Not all devices or hotspot modes allow peer-to-peer connections.

## GLES2 vs. Vulkan

- **OpenGL ES 2.0** is the recommended renderer on the Galaxy A51 until Vulkan is proven faster and equally stable on physical hardware.
- **Vulkan 1.1** uses native Vulkan calls (not GL4ES/ANGLE/Zink); it is available for testing and may have lower or less stable FPS on some Mali drivers.
- The Vulkan path includes fixes for combined BSP lightmap fog, redundant draw state changes, uniform reuse, contiguous RGBA8 texture uploads and idle staging-buffer cleanup.
- Use the **same map, camera, game settings, device temperature and resolution** when comparing APIs. The available offscreen benchmark does not measure actual displayed FPS.

The current code does **not** silently downscale the image merely to raise an FPS counter. Lower resolutions and FPS caps can be selected explicitly; the UI remains at native resolution when the 3D scene is voluntarily scaled.

## Build from source

Required: Python 3.11+, Git, JDK 17, Android SDK platform **35**, build-tools **35.0.0**, NDK **27.2.12479018**, CMake **3.22.1**, and Ninja. Configure `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) and `JAVA_HOME`.

```bash
python3 scripts/bootstrap.py
python3 scripts/build_apk.py --abi arm64-v8a
# Optional second architecture:
python3 scripts/build_apk.py --abi armeabi-v7a
```

Outputs are placed under `build/nzportable-android-<abi>.apk`. Install a locally signed APK using:

```bash
adb install -r build/nzportable-android-arm64-v8a.apk
```

The normal CI/direct-SDK build expects the user to import game data from a ZIP on first launch. For a local developer build with pinned test data, see `python3 scripts/bootstrap.py --with-data`; the upstream nightly archive may change, in which case the recorded checksum must be reviewed rather than bypassed.

Android Studio can also open `android/` after bootstrap. The repository includes an Android Gradle build path alongside the direct SDK APK builder.

## Testing and profiling

The GitHub Actions [Android APK workflow](.github/workflows/android.yml) runs host tests, Vulkan rendering/validation tests, and `arm64-v8a`/`armeabi-v7a` builds. The release workflow publishes tested APK artifacts and SHA-256 sums.

On a Linux host with the required Mesa/EGL/GLES dependencies:

```bash
sh tests/run_host_tests.sh
sh tests/run_vulkan_tests.sh
```

For real-device diagnostics on Windows PowerShell with `adb` available:

```powershell
powershell -File scripts/profile-a51.ps1 -Seconds 1800
```

Enable the in-game performance log and test the same scene on both renderers. Review CPU/GPU timings, 1% lows, device temperatures and memory use. **Host software-renderer performance does not predict Mali-G72 performance.**

Known limitations and outstanding checks: Android device-specific Vulkan driver issues, reliable touch/UI sizing across different aspect ratios, LAN interoperability on varied hotspots, graphical parity in all maps, sustained thermals and battery impact. A touch-layout editor and broader physical-device qualification remain future work.

More details: [Android implementation](docs/ANDROID_PORT.md) · [Vulkan backend](docs/VULKAN_ANDROID.md) · [A51 fog/LAN/menu fixes](docs/A51_COOP_MAP_FOG_FIXES.md).

## Upstream, credits and license

NZPortable Android is an **unofficial experimental port**. Original NZ:P, Vril, Quake-derived code and third-party components belong to their respective authors. Upstream sources: [NZ:P](https://github.com/nzp-team/nzportable), [Vril Engine](https://github.com/nzp-team/vril-engine) and [FTEQW](https://github.com/nzp-team/fteqw).

Vril's code and SDL backend are included at revisions recorded in [dependencies.lock.json](dependencies.lock.json). Original notices and the [GPL license](third_party/vril/LICENSE) are retained. The port code is GPL-2.0-or-later; vendored libraries keep their own license terms. Game data and assets remain subject to their original copyrights and are **not relicensed or bundled with the release APK**.

Additional project provenance: [upstream hub README](docs/UPSTREAM_HUB.md).
