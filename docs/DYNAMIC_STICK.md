# Floating (dynamic) left stick

The movement stick appears under the thumb: put a finger anywhere in the **left 30% of the screen** (below the top 20%) and the
stick is centred there; the movement is measured from that point. It is **on by default**; *Options > Video*, last page,
*LEFT STICK: DYNAMIC / FIXED* (cvar `touch_dynamic_stick`, archived) switches back to the fixed RetroTouch stick, live.

## How it works

RetroTouch (the pinned MIT overlay) has no floating stick and its view is `final`, so the stick is a small layer on top of it:

* `DynamicStick` (pure Java, tested on the host) holds the geometry: zone, radius, dead zone, finger ownership.
  It reports exactly what the fixed stick reports, because the native side and the auto-sprint threshold expect it: x to the right,
  y **down** (the engine negates it), length at most 1, dead zone 0.08 per axis, and the same radius (half of 0.33 of the shorter side).
* `DynamicStickView` is the touch surface, above the overlay, and it is **exactly as large as the zone** (bottom left, via
  `onMeasure` and gravity). `DynamicStickHud` is a separate full-screen view that only draws (base ring and knob while the stick is
  held, a faint ring where the fixed stick used to be while it is idle) and never takes a touch.
  The size matters. `ViewGroup.dispatchTouchEvent` skips every child whose bounds do not contain the new finger, but gives a finger
  inside the bounds of a view that is *already* receiving touches to that view straight away, without asking the views below
  (the "Child is already receiving touch within its bounds" branch). The first version of the stick view covered the screen, so
  while the stick was held the camera finger was handed to it and RetroTouch never saw it: no camera while moving. A finger outside
  the zone can no longer reach the stick view at all. The finger that started the stick keeps it until lifted, even if it leaves
  the zone.
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
* The size of the touch view and `inZone` agree on seven screen sizes (landscape and portrait) down to the pixel, so a finger outside
  the view is never a stick touch; the camera zone and RUN are outside it.
* The `ViewGroup` behaviour described above was read in the AOSP source (`dispatchTouchEvent`), not recalled.
* All Java compiles with `--release 8` against a real `android.jar` (API 30), the pinned RetroTouch classes and the SDL sources, as
  `scripts/build_apk.py` does (the only error is SDL's own use of an API 31 constant, because the jar used here is older than the
  project's API 35). The touched C files were syntax-checked on the host (`android_port.c` with small stand-ins for two NDK headers).

**Not verified: anything on a device.** In particular that the camera now works with the stick held (the logic follows the AOSP
code and the RetroTouch `onTouchEvent` bytecode, which handles split events per pointer, but it has not been run), the visuals, and
the feel. There is no emulator or device here.
