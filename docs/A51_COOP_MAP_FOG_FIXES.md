# NZPortable Android: BSP fog, co-op identity, map browser and memory fixes

## Vulkan fog

The combined BSP + lightmap pass now applies linear/exp/exp2 fog **after**
lightmap multiplication. Previously, the pass fogged the diffuse surface,
then multiplied the fog color by the lightmap, hiding fog on dark world
surfaces while ordinary models remained fogged.

This affects **combined lightmapped world materials** in Vulkan. GLES2 and
non-combined Vulkan draws preserve their existing shaders. A synthetic Vulkan
pixel regression checks a half-fogged BSP surface with a nonwhite lightmap.
We still need side-by-side footage on physical Galaxy A51 to verify water,
sky, masked surfaces, world fog colors and dynamic lightmaps.

## GLES2 versus Vulkan

The launcher retains GLES2 in Auto mode. Do not switch Auto to Vulkan solely
because Vulkan 1.1 is available: the Galaxy A51 may get better and more stable
FPS with GLES2. A Vulkan API toggle remains available, without reducing
native render scale or graphics effects.

For a fair comparison: same map/demo, view, zombie count, native 100% scale,
device temperature, GPU mode and FPS cap. Use `nzp_profile 1`, the
`NZP_VK_STAGES` log, `vid_benchmark` and `scripts/profile-a51.ps1`. The
existing offscreen benchmark reports submission throughput rather than
displayed frame rate. For game FPS, measure present intervals on the device.

## Map browser

- Stock maps are shown three per page on Android; custom maps four per page.
- Swiping up/down on the map browser changes pages; dragging does **not**
  select or start a map on the initial touch.
- Next/previous page buttons support touch and gamepad; mouse-wheel scrolling
  also changes pages. The standard non-Android layout is preserved.
- Map-label strings are now copied into their menu button's fixed storage;
  no per-frame heap allocation and no stale pointer after free.

## LAN player nickname

The cooperative main menu and host creation menu show **PLAYER NICK**.
Android uses a native single-line dialog with 15-character limit and sends
its value to the existing game-thread event queue. The engine queues the
original Quake `name "..." ` command after rejecting control chars, quotation
marks, backslashes and semicolons. This updates the persisted `_cl_name`
cvar and forwards the rename to a server when already connected. Server
hostname remains a separate setting.

## CPU and RAM changes

- Vulkan RGBA8 texture uploads use one contiguous copy or a row copy, instead
  of an inner pixel-by-pixel loop. Other texture encodings are unchanged.
- The combined world pass caches the lightmap upload result per visible group
  rather than requerying the atlas a second time per draw.
- Vulkan staging starts with 1 MiB chunks instead of 4 MiB and releases
  idle frame-slot buffers only **after its GPU fence has completed** and
  sustained inactivity. No live GPU memory is discarded.
- Consecutive identical texture references are kept once in the frame
  resource-lifetime list.

All changes require mobile device profiling to measure speed, memory,
battery and temperature gains. CI host validation is a correctness check,
**not** proof of higher FPS on a Mali-G72 MP3 GPU.

## Manual acceptance checklist

1. Test fog on `ndu` with Vulkan and GLES2: walls, floors, distant props,
   zombies, water/sky, explosions and a map with colored fog.
2. Select maps at the bottom of both stock and custom lists: scroll up/down,
   navigate pages with touch, controller and wheel; ensure scrolling never
   starts a match.
3. Edit a player nickname, host and join LAN, verify it appears to peers
   and remains after relaunch. Rename while connected and verify update.
4. Repeatedly change maps and restart the renderer while observing Android
   memory and Vulkan validation errors.
5. Compare actual A51 displayed FPS and P95/P99 frame times under identical
   workloads; keep GLES2 as fallback if it is faster.
