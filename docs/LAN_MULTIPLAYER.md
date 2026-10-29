# NZPortable Android — LAN cooperative mode (experimental)

This document describes the actual Vril/Quake UDP implementation in the
`feat/lan-coop-android-20261029` branch. It is **not a statement that four
physical Android devices have already been verified**. Read the known limits.

## Architecture

- **Host**: the player's Android app runs Vril's authoritative local Quake
  server and its own loopback client in the same `:game` process.
- **Remote peers**: use Vril's existing datagram/UDP transport:
  `net_main.c`, `net_dgrm.c`, `net_udp_sdl.c`, `net_loop.c`,
  `host.c`, `host_cmd.c` and `sv_main.c`.
- **Default port**: UDP **26000**. The game does not need an external service,
  account, cloud server, mobile data connection, or web service.
- **Discovery**: standard Quake `CCREQ_SERVER_INFO` broadcasts on the
  limited-broadcast address and all suitable active IPv4 interfaces' directed
  broadcast addresses (Android); the reply contains the real server endpoint,
  name, map, active/max player count, and protocol version.
- **Direct join**: the user can input a complete numeric
  `IPv4[:port]` address. The address is validated before calling the
  existing `connect` engine command; no arbitrary console commands are
  accepted.
- **Coop cvars**: `coop 1`, `deathmatch 0`, `maxplayers 2..4`,
  `listen 1` are applied before `map` to stop Quake's multiplayer
  deathmatch default. The server simulation is paused in the LAN lobby;
  the host explicitly unpauses to begin the match.
- **Authority**: Vril already simulates zombies, projectiles, movement and
  game entities in the host server, with per-client signon and Quake
  world-state messages. We do *not* create independent client zombie worlds.
  The exact outcomes of special NZ:P mechanics still depend on the particular
  compiled `nzp/progs.dat` imported on each device.

## Setup

1. Install the same APK **and the same complete NZPortable PC game ZIP** on
   every phone. Every phone needs the corresponding maps, sounds and
   `nzp/progs.dat`. A matching protocol number alone does not verify all
   game assets.
2. The host enables an Android **Wi-Fi hotspot**, or everybody connects to
   the same home Wi-Fi. Turn the hotspot on *before* launching NZPortable.
   No internet or mobile-data plan is required.
3. Host: `COOPERATIVE > CREATE LAN GAME`, set name, slots (2–4),
   difficulty and round, select map, then choose `OPEN LAN LOBBY`.
   The app actually loads a server map and stops its physics while waiting.
4. Peers: `COOPERATIVE > FIND LAN GAMES`, select the listed game. If the
   hotspot filters broadcast, use `CONNECT BY IP` and type
   `hostIPv4:26000` (example: `192.168.43.1:26000` **only if that is
   the actual host IP**; there is no fixed hotspot subnet).
5. The host sees joined player names and counts. Tap `START MATCH`.
   `CLOSE SERVER` shuts down the hosting session.

If your Android device has multiple active IPv4 networks, the advertised
address in the lobby may not be the one peers can reach. Use the address
shown in the hotspot configuration or the connected peers' gateway, and
join by IP. Private hotspot routing, wireless client isolation and
manufacturer network policies can still prevent device-to-device UDP.

## Current integration

Implemented in the Android branch:

- Actual Vril UDP discovery/connect/host and original network server
- Android hotspot/Wi-Fi directed + limited broadcast
- Live subnet IPv4 lookup; fast numeric connect; nonblocking socket polling
- UDP recv/send error differentiation and safe shutdown socket iteration
- Native Android address/name text input via SDL event queue
- Coop host creation, map selection, slots, round and difficulty
- Host lobby with active engine client list and manual start
- Build and endpoint validation tests; host loopback UDP transport smoke test
- Retains previous map-load fix and RetroTouch gameplay overlay

### Important remaining work / not verified

- **No physical two-phone hotspot session was available for this build**.
  Thus actual Android hotspot discovery, map signon, spawn placement,
  client-to-client visibility, ammo, purchases, AI targeting, zombies,
  round progression and 30-minute stability have **not** been verified.
- Java touch UI and native network compilation succeeding does not establish
  correct in-game co-op. Read the Vril logs from *both* host and client.
- Version mismatch uses the original Quake `NET_PROTOCOL_VERSION` gate;
  asset hashes/version of `progs.dat` and map BSP content are **not**
  negotiated yet. Only connect devices with identical imports.
- `FIND LAN GAMES` shows host name/map/slots; it does not yet perform a
  separate RTT ping measurement or distinct detailed resource compatibility
  probe.
- There is no automatic reconnection with saved inventory/points. After
  dropped Wi-Fi, reconnect manually; restoration of original player state
  is not guaranteed. A disconnected player should not terminate the Vril
  server, but this is not yet phone-tested.
- There is no client-side Ready toggle in the LAN lobby. Joined clients
  remain connected to the paused server until the host unpauses it.
- `connect` still uses the classic synchronous Quake initial connection
  handshake, with short SDL yields on Android. Normal gameplay uses
  nonblocking UDP sockets, but failed connection setup can still briefly
  interrupt the UI.
- Clients receive Vril's standard disconnection status when the host closes,
  not a new customized Android popup.
- The host/three-client, simulated packet loss, full player state replication,
  zombie synchronization and long-session acceptance tests cannot be
  honestly certified without engine-level integration and physical tests.

## Automated checks

The GitHub Actions Android workflow produces the APKs and runs
`tests/run_host_tests.sh`. It now includes:

- `tests/lan_endpoint_test.c`: runs the **same parser** used by the Android
  IP entrypoint, rejects malformed endpoints and console injection strings.
- `tests/test_lan_udp_transport.py`: proves four separate UDP sockets can
  exchange messages with one host, sockets are nonblocking, and a client can
  reconnect after closing its socket **on local host networking**. This is
  a transport smoke test, not an emulator/gameplay or Vril protocol test.
- Existing GLES, render and game-data installer regression tests.

## Real-world acceptance matrix (to be executed)

| Scenario | Status |
|---|---|
| ARM64 Android APK compiles | CI |
| Host opens UDP server | Code path wired; phone verification required |
| Second Android connects over hotspot | Not verified |
| Discovery across hotspot and home Wi-Fi | Not verified |
| Manual IPv4 join | Code path wired; phone verification required |
| Host + three clients | Not verified |
| Same map and visible players | Not verified |
| Zombie spawns, kills and rounds identical | Not verified |
| Purchases, equipment, powerups and points identical | Not verified |
| Client disconnect, continue and reconnect | Not verified |
| Lossy network and 30-minute match | Not verified |
| All clients' touch controls | Not verified |

## Logs and troubleshooting

On a Windows PC with ADB, connect each phone in turn:

```powershell
adb logcat -c
adb logcat -d -v time > lan-device-log.txt
adb shell dumpsys meminfo org.nzportable.android:game
```

Filter for `NZP_PERF`, `UDP`, `Network`, `Server`, `Signon`,
`No Response`, `client`, `Warning`, `Error` and `AndroidRuntime`.

When a second phone cannot join, confirm that a server was created in
`OPEN LAN LOBBY`, the device is on the host's hotspot and its real IP
was supplied, no client isolation is enabled, and the import data matches.
Try manually using the host IP before blaming discovery. A router may
block broadcast but still allow unicast UDP.

## Sources

- [NZPortable (upstream)](https://github.com/nzp-team/nzportable)
- [Vril Engine](https://github.com/nzp-team/vril-engine)
- [QuakeC](https://github.com/nzp-team/quakec)
- [FTEQW](https://github.com/nzp-team/fteqw)

The QuakeC repository includes server-player logic, zombies and rounds
using shared player entities and server messages, but the imported compiled
`progs.dat` remains an independent prerequisite. We cannot confirm its
version from this repository alone.
