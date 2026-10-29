# LAN multiplayer implementation status

## Existing transport
Vril already has loopback, UDP and datagram transport in `third_party/vril/source/platform/sdl`.
`NET_Slist_f` schedules UDP discovery, while `maxplayers` changes the listen state.
The Android manifest already declares `android.permission.INTERNET`.

## Verified defect fixed
`MaxPlayers_f` previously forced `deathmatch 1` for any value above one. The
Android LAN foundation branch now preserves `deathmatch 0` when `coop 1`.

## Not yet implemented or verified
- Android hotspot subnet-directed broadcasts and reliable discovery.
- Native cooperative creation/join/lobby UI, including touch keyboard and IP fallback.
- QuakeC verification and authoritative synchronization of zombies, points, rounds,
  doors, perks, purchases, respawns and match completion.
- Cross-device integration, reconnect, version/resource compatibility checks.
- Packet loss/latency tests and 30-minute physical-device soak test.

## Required validation
Use two real Android devices on hotspot and ordinary Wi-Fi, then four devices,
and compare round counters, entity state and player inventory. Existing socket
support is not evidence of completed multiplayer gameplay.
