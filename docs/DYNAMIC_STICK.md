# Floating (dynamic) left stick

The movement stick appears under the thumb: put a finger anywhere in the **left 30% of the screen** (below the top 20%) and the
stick is centred there; the movement is measured from that point. It is **on by default**; *Options > Video*, last page,
*LEFT STICK: DYNAMIC / FIXED* (cvar `touch_dynamic_stick`, archived) switches back to the fixed RetroTouch stick, live.

## How it works

RetroTouch (the pinned MIT overlay) has no floating stick and its view is `final`, so the stick is a small layer on top of it:

* `DynamicStick` (pure Java, tested on the host) holds the geometry: zone, radius, dead zone, finger ownership.
  It reports exactly what the fixed stick reports, because the native side and the auto-sprint threshold expect it: x to the right,
  y **down** (the engine negates it), length at most 1, dead zone 0.08 per axis, and the same radius (half of 0.33 of the shorter side).
* `DynamicStickView` sits above the overlay. It accepts only a touch that starts in the zone; every other touch is refused and falls
  through to RetroTouch, so buttons and the camera zone behave as before. The finger that started the stick keeps it until lifted,
  even if it leaves the zone. While the stick is touched it draws the base ring and the knob; while idle it draws a faint ring where
  the fixed stick used to be, so you know where to land.
* `RetroTouchBridge` gives the floating mode its own gameplay layout (no stick control, saved-layout ID
  `nzportable_android_v3_dynamic`); the fixed mode keeps `nzportable_android_v2_large`, so each remembers its own edits. The bridge
  switches touch splitting on for the content layout (two thumbs must reach two different views) and ignores the overlay's own
  "all fingers released" `onMove(0, 0)` while floating, which would otherwise cancel a thumb still holding the stick.
* The layout editor, a paused game and the menus release the stick and disable the layer.

## Limits

* The zone is a fixed rectangle. RUN starts at about 31% of the width, hence 30%. Buttons the player drags into the zone with the
  RetroTouch editor will not take touches there while the floating stick is on (the editor itself is not affected).
* The base does not follow a thumb that is pulled past the ring: the knob stays on the ring.
* Changing the setting resets nothing else, but a layout edited with the fixed stick does not carry over to the floating layout.

## What was verified

* Host test `tests/DynamicStickTest.java`: zone edges, radius equal to the fixed stick, per-axis dead zone, length limit, the sign
  convention (up the screen is negative y), the sprint threshold being reachable, a second finger cannot take or move the stick, other
  fingers lifting do not end it, cancel, floating to a new place, portrait screens.
* All Java compiles with `--release 8` against a real `android.jar` (API 30), the pinned RetroTouch classes and the SDL sources, as
  `scripts/build_apk.py` does (the only error is SDL's own use of an API 31 constant, because the jar used here is older than the
  project's API 35). The touched C files were syntax-checked on the host (`android_port.c` with small stand-ins for two NDK headers).

**Not verified: anything on a device.** In particular that Android hands separate fingers to the stick layer and to RetroTouch the
way the framework documents for split touches, that RetroTouch behaves with the split events, the visuals, and the feel. There is no
emulator or device here.
