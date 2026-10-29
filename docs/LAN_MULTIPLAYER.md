# NZPortable Android LAN cooperative multiplayer — experimental

Status: engine-integrated LAN hosting and joining, but four-player gameplay on real
Android phones has NOT been verified. Successful compilation is not proof of gameplay sync.

## Features implemented in this branch

- The COOPERATIVE main-menu entry is enabled.
- CREATE LAN GAME uses existing stock/custom maps and pre-game settings, followed
  by the real Vril local server with coop=1, deathmatch=0, maxplayers 2–4.
- FIND LAN GAMES uses Quake Datagram UDP discovery and real hostcache entries.
  The server list displays map, advertised name and occupied/max slots.
- CONNECT BY IP opens an Android EditText dialog, accepts IPv4[:port], validates
  its octets and port and hands the result to existing Vril connect.
- The host displays its actual local interface address and connected player
  names in the pause menu; clients have an explicit leave option.
- Directed broadcasts are sent to all active IPv4 subnet broadcast addresses
  with limited broadcast fallback, without hardcoding any hotspot IP.
- LAN discovery uses the actual reply source IP if a hotspot advertises another
  unreachable interface. Numeric LAN joins skip synchronous slist discovery.
- Android reverse-DNS lookups are avoided; the socket itself remains nonblocking.
- All Android address/name dialog input crosses into the SDL game thread via events.
- Existing RetroTouch gameplay controls, GLES optimization and solo startup remain.

## How to play / limitations

1. On phone A enable Android Wi-Fi hotspot; on B (and optionally C/D) connect to it.
   Home Wi-Fi works too if clients can send UDP packets to one another.
2. Install the same Android APK and import identical game-data archives on all devices.
3. On A: COOPERATIVE > CREATE LAN GAME > map > START GAME. Set the player count
   before choosing the map. The host is also a player.
4. Once A has loaded the world, on B select COOPERATIVE > FIND LAN GAMES.
   If discovery fails, use CONNECT BY IP with the address displayed on A,
   e.g. 192.168.1.1:26000. This is an example, not an assumed hotspot address.

Vril creates its server as a map starts. A persistent pre-match lobby with
ready states and a host-controlled launch AFTER other players join is not
implemented. Joining the real running server is the available workflow.

## Gameplay authority and asset compatibility

Vril's local host runs the authoritative server, with Quake datagram reliable
packets and QuakeC world simulation. The upstream NZ:P QuakeC sources have
multi-player player spawn/disconnect, rounds, and zombie AI loops:
https://github.com/nzp-team/quakec/tree/main/source/server
The FTEQW QuakeC source contains FTE-specific compiler branches; it is not
automatically interchangeable with the user's imported Vril progs.dat.
The Android client currently has no game-asset hash/version negotiation beyond
the engine's existing network protocol compatibility check.

## CI tests and outstanding validation

tests/run_host_tests.sh runs tests/test_lan_network.c: valid and malicious
IP/port inputs plus /24, /28 and /16 directed broadcast calculations.
Existing GLES and import checks run too. GitHub Actions compiles ARM64 and ARMv7.

NOT YET VERIFIED on physical hardware:
- Two-phone hotspot discovery, connect and simultaneous visible player entities.
- Four participants, synchronized zombie kills, score, round, door, perks, weapons.
- Host/player disconnect, reconnection, missing/mismatched map, router isolation.
- 30-minute four-phone run under packet loss and thermal throttling.

Known limitations: old Datagram_Connect still blocks the game frame while trying
its synchronous handshake; host backgrounding suspends the server main loop,
potentially causing a timeout. Neither reconnection restoration nor ready-status
sync is promised.

To collect diagnostic logs on a Windows PC:

    adb logcat -d | findstr /I "NZP_PERF NZP_LEVEL_LOAD UDP LAN"

Do not merge or call multiplayer fully verified based on compilation alone.
