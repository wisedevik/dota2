# dota2-private-server

A backend for the 2016 macOS build of Dota 2 (build 1805, patch 6.88f): a Game
Coordinator, a Steam CM (connection manager) for the client to log on to, and a
`libsteam_api.dylib` replacement so the client runs without Steam.

The client boots to the dashboard, talks to the GC, and plays the hero demo and
bot matches. Matches run on the game's own listen server.

This is not affiliated with Valve. You need your own copy of the game, which
Steam still serves for free; nothing from the game is in this repository.

## Requirements

- macOS with Xcode command line tools; the client is x86_64, so Apple Silicon
  needs Rosetta
- CMake 3.20+ and protobuf (`brew install cmake protobuf`)
- [DepotDownloader](https://github.com/SteamRE/DepotDownloader) and a Steam
  account with Dota 2 in its library

## Getting the client

Put DepotDownloader in `devtools/depotdownloader/`, then:

```bash
STEAM_USER=<your login> devtools/fetch_client.sh
```

It downloads the seven depots of build 1805 into `client/game` (about 15 GB).

The client ships SDL 2.0.5, which leaves the window black on current macOS.
Replace it with 2.30.9 from the
[SDL releases](https://github.com/libsdl-org/SDL/releases/tag/release-2.30.9)
(`SDL2-2.30.9.dmg`):

```bash
lipo /Volumes/SDL2/SDL2.framework/SDL2 -thin x86_64 -output client/game/game/bin/osx64/libSDL2-2.0.0.dylib
```

```bash
install_name_tool -id @loader_path/libSDL2-2.0.0.dylib client/game/game/bin/osx64/libSDL2-2.0.0.dylib
```

```bash
codesign --force --sign - client/game/game/bin/osx64/libSDL2-2.0.0.dylib
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
ctest --test-dir build
```

Besides `gcserver`, the build produces the two x86_64 libraries the client
needs and copies them into `client/game/game/bin/osx64`: `libsteam_api.dylib`
and `window_fix.dylib` (works around a deadlock when the engine creates its
window on current macOS). Keep a copy of the original `libsteam_api.dylib` if
you want to go back.

## Running

```bash
build/gcserver
```

```bash
devtools/run_client.sh -windowed -w 1280 -h 720
```

`gcserver` listens on `127.0.0.1:27017`. Arguments after `run_client.sh` go to
the game; `+dota_launch_custom_game hero_demo hero_demo_main` starts straight in
the hero demo, `-condebug` writes the console to `client/game/game/dota/console.log`.

## Layout

```
src/public      Steamworks-style headers (CSteamID, ISteamGameCoordinator)
src/steamcm     Steam CM protocol: server, client, crypto
src/gcsdk       GC messages and shared object caches
src/dota_gc     the Dota GC: handshake, dashboard, practice lobbies
src/gcserver    gcserver executable
src/steam_api   libsteam_api.dylib for the client
src/windowfix   window_fix.dylib for the client
protobufs       Dota 2 protobuf definitions of this build
devtools        client download and launch scripts
```

The CM protocol is described in [docs/steam-cm.md](docs/steam-cm.md).

## Status

Working: logon, GC handshake, the player's SO cache, empty replies to the
dashboard's data requests, practice lobbies, hero demo and bot matches.

Not done yet: handing a launched lobby to a dedicated server, real data for the
dashboard (profile, match history, matchmaking stats), and a few client
messages that are not in the protobufs (8137, 8140, 8205).

Now and then the client dies at startup with an AppKit exception on
`GLRenderThread`; launching it again works.

## License

MIT, see [LICENSE](LICENSE).
