# Touch settings screen (Android)

All settings are on one screen made for fingers. *Main menu / pause menu > Options* opens it; the old pages (Configuration, Video with its
eight sub-pages, Audio, Controls, Accessibility) lead to the matching category of the same screen.

## What it looks like

* **Left: a rail** with BACK and six categories: VIDEO, BLUR (motion blur), AUDIO, CONTROLS, ACCESS(ibility), ADVANCED. The current one is
  marked in red.
* **Right: one scrolling list** of large rows (about 10 mm tall, whatever the phone: the sizes are in millimetres, from the panel's real
  dpi). Section headers are small red labels.
  * *switch*: tap anywhere on the row;
  * *slider*: touch the bar and drag, the value follows the finger and snaps to its step; the config file is written when the finger lifts;
  * *choice*: the row shows the current value; a tap opens a **sheet** with every value as a big row (the current one marked, values this
    device cannot use greyed out); tap one to pick it, tap outside or press Back to close;
  * *action*: opens an editor or runs a command (stick zone, key bindings, console, benchmark...).
* **Bottom line:** the description of the row you touched last (two lines).
* A dimmed row is not available now (for example blur sliders while the blur is off, movement blur without Vulkan); tapping it shows a
  note saying why.

## Gestures

* A tap fires when the finger **lifts** on the same row. Touching only highlights, so a wrong touch can be slid away from.
* Dragging up or down scrolls (with momentum). A drag never fires a row, a touch that stops a moving list is not a tap, a sideways drag
  cancels the tap. The tolerance for finger jitter is 3 mm.
* A slider takes the touch at once when it lands on the bar; a touch on the label part scrolls like anywhere else; dragging a slider
  never scrolls the list.
* Back (system key) closes an open sheet first, then leaves the screen.
* The other menus (main, pause, lobby, map pick...) keep their layout but now also fire **on release** instead of on touch.

## What is where (all settings, 46 rows)

VIDEO: 3D resolution, upscaling (FSR 1.0 / Lite / Full, unsupported ones greyed out), UI size, FPS limit (also turns vsync off above 60),
vsync, performance mode, field of view, gamma, ultrawide mode, original particles, texture filtering, dithering.
BLUR: mode (off / low / medium / high / ultra / custom), intensity up to 120%, camera blur, movement blur, samples, high quality,
effect resolution. Editing any value switches a preset to CUSTOM, as before.
AUDIO: master volume, music volume.
CONTROLS: touch buttons, dynamic left stick, stick zone and size, **camera sensitivity (new: it could only be changed from the
console before)**, invert look, aim assist, gamepad look sensitivity and acceleration, vibration, key bindings.
ACCESS: hitmarkers, text backdrop, accessible colors, forbid white flashes, month spoof, crosshair dot, camera bob, sprint melee dive.
ADVANCED: graphics API, pre-rotation, show FPS, performance log, offscreen benchmark, LAN compatibility, console.

## How it is built

| file | what |
|---|---|
| `android/native/touch_menu.[ch]` | the screen without the game: layout in mm, scrolling, gestures, hit-testing, drawing through a small backend. Pure C. |
| `android/native/touch_menu_content.[ch]` | the categories and rows as data: labels, hints, ranges, the values a choice offers. Pure C. |
| `third_party/vril/source/menu/menu_touch_settings.c` | the engine side: which cvar each row is, which rows are visible / enabled, what a change does, entry points. |
| `android/native/android_port.c` | gives the screen the raw finger events; release-activation for the other menus. |
| `android/native/video_android.c` | `VID_PixelsPerMM` (SDL dpi, with a sanity check and a fallback). |

Cvars are written through the variable (`set_cv(&scr_fov, ...)`), not by a name string, so a wrong name cannot silently write nowhere
(`scr_fov` is `fov`, `scr_showfps` is `show_fps`).

## What was verified

Run by `tests/run_touch_menu_tests.sh` (part of `tests/run_host_tests.sh`), on the host:

* `tests/test_touch_menu.c` (also under AddressSanitizer and UBSan): content tables (unique ids, lengths, every slider an exact number of
  steps); on **seven screen sizes** (2400x1080, 1920x1080, 2560x1600, 1280x720, 1600x720, 960x540 and a portrait one) and all six
  categories, with the **real texts**: rows 9-11.5 mm, the rail fits, nothing is drawn outside the screen, no two texts overlap, no
  category name is cut off; and the gestures: tap on release, jitter, sideways drag, scrolling by exactly the finger travel and its
  limits, momentum (monotonic, in bounds, ends), a touch stopping a fling, sliders (snapping, clamping beyond the ends, no scroll while
  dragging, label area, disabled), the sheet (scrolling, current value in view, disabled entries, outside tap, Back), the rail, BACK,
  toast, empty and degenerate states.
* `tests/touch_settings_glue_test.c`: the **real** `menu_touch_settings.c` against stand-ins for the cvars and the engine calls:
  for every one of the 41 settings the row **shows** its cvar and a gesture **writes** that cvar (and no setting exists that does
  nothing); the 5 actions each call one engine function; the compound rules (unlimited FPS turns vsync off, performance modes, preset to
  CUSTOM), saving once on release, disabled rows with their reasons, Back, re-entering. Six deliberate breakages of the glue (wrong
  cvar for a slider, a row reading the wrong cvar, a lost rule, saving on every move, ...) are each caught.
* `tests/render_touch_menu.py` draws any of those states as a picture; the pictures were looked at while the layout was fixed (the title
  bar was dropped because it left only four visible rows, the category BLUR was renamed because MOTION BLUR did not fit the rail, the
  sheet rows were shrunk so six choices fit on a phone).
* All touched C files were syntax-checked with the Android build's defines, with implicit function declarations as errors (what the NDK's
  Clang does), which found one missing prototype.

**Not verified: anything on a device or in the real engine.** The engine itself was not run: the glue was tested against stand-ins. The
real bitmap font (the pictures use a monospace stand-in at the same cell size, and the tests assume a font a little wider than the real
one), how the rows feel under a thumb, the sizes from the real dpi of a Galaxy A51, the momentum feel, the `SDL_GetDisplayDPI` value on
real hardware, the effect of release-activation on every old menu, and the full Gradle/NDK build.

## Limits

* The old pages are no longer reachable on Android (they are still compiled and used on other platforms).
* The bindings screen is still the old one, reached from CONTROLS > KEY BINDINGS.
* The main, pause, lobby and map menus keep their old layout (only the tap rule changed); a redesign of those is a separate step.
* A long description is cut to two lines.
* Portrait is not designed for (the app is landscape-only); it only has to not break.
