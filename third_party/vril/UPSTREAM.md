# Vril source snapshot

Upstream: https://github.com/nzp-team/vril-engine

Revision: `7a31c3d7eb4bd9db87e295932a0f092a69495d5b`.

This directory contains the platform independent C sources, SDL platform, license,
README and desktop makefile needed by the Android port. Other console backends
and binary test reference images are not vendored. Android-specific changes are
guarded by `PLATFORM_ANDROID`; the GLU perspective call uses the engine's existing
local implementation.

Android renderer, lifecycle and touch code live in `android/native` at repository
root. SDL2 and SDL_mixer are fetched separately at the exact revisions in
`dependencies.lock.json`.

Original copyright notices are preserved. See `LICENSE` (GPL version 2 or later,
where indicated in source file notices).
