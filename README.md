# OpenDay

OpenDay is an open-source, native reimplementation of the engine of the 1998
turn-based strategy game *Fallen Haven: Liberation Day*, in the spirit of
OpenRA, OpenXcom and fheroes2.

**Primary target:** macOS on Apple Silicon (arm64). Windows and Linux are
secondary targets.

> **Status:** early research. Nothing playable yet. Current work: documenting
> the original data formats (`docs/formats/`) and writing extraction tools.

## Disclaimer

OpenDay is an **unofficial** fan project. It is not affiliated with, endorsed
by or connected to the original developers or publishers. "Fallen Haven" is a
trademark of its respective owner and is used here only to describe
compatibility.

OpenDay contains **no original game content** — no executables, graphics,
sounds, videos, maps or databases. To use it you need your own legitimate copy
of the game (e.g. from GOG). OpenDay reads the data files from your
installation at runtime.

## Game data

Point OpenDay at your installation with the `OPENDAY_GAME_DIR` environment
variable, e.g. for the default GOG Galaxy location on Windows:

```
C:\Program Files (x86)\GOG Galaxy\Games\Fallen Haven Liberation Day
```

## Building

Requirements: CMake 3.21+ and a C++20 compiler (MSVC 2022+, Clang 15+ or
GCC 12+). SDL3 and SQLite are used from the system if installed, otherwise
CMake downloads pinned versions and builds them. On Linux, SDL3 needs the
usual X11/Wayland development packages. Pass `-DOPENDAY_BUILD_VIEWER=OFF` to
build only the tools and libraries without SDL3.

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

If `OPENDAY_GAME_DIR` is set at configure time, an extra test decodes the
graphics of your installation.

## Tools

- `dbitool` — inspect and extract `IMGS\*.DBI` sprite archives
  (`info`, `list`, `extract`, `sheet`).
- `fftool` — inspect and extract the `ANMSUNIT\ANMSUNIT.FF` unit animation
  container (Smacker videos; frames as PNG, audio as WAV).
- `tools/mdb/mdb2sqlite.py` — convert the game's Jet 3 databases to SQLite
  (Python standard library only).
- `mapview` — SDL3 viewer for every scenario map: terrain, roads, walls,
  mines, buildings and units, with palette cycling (animated water), and a
  tactical battle on the map: movement with path finding and action
  points, shooting with hit rolls, damage and destruction, reaction fire,
  mines, turns, scenario objectives, reinforcements and the computer
  player. The human sides take turns in the same window (hot seat). It
  needs the converted databases:

  ```
  python3 tools/mdb/mdb2sqlite.py --game-dir "<game dir>" ~/openday-data
  mapview --game-dir "<game dir>" --data ~/openday-data --scenario s4-5-1
  ```

  Arrow keys / WASD or mouse drag scroll, PageUp/PageDown switch scenario,
  1–4 zoom, C toggles palette cycling, O toggles the objects, M toggles
  sound. Left click selects a unit of the player whose turn it is
  (Shift + click builds a group), and the path to the tile under the mouse
  is previewed. Right click moves there or fires at an enemy unit or
  building (Ctrl + right click fires at any tile). F toggles autofire,
  Enter ends the turn; the computer player then plays its turn. The
  window title shows the turn, the objective, the player, the selected
  unit, the hit chance and the result. `--no-ai` lets you play slot 0
  yourself.
  `--screenshot out.png` renders the whole map to a file instead of
  opening a window; `--do move:X,Y:TX,TY` (or `attack:X,Y:TX,TY`, `end`)
  sends orders first and prints the server's events, and `--ticks N`
  captures an animation in progress.

- `openday` — the game program (work in progress): intro video, main menu,
  skirmish battles with deployment, the original tactical interface (side
  panel, minimap, unit information, path preview, objectives dialog,
  options), the computer player, and the battle report. Campaign, saving
  and multiplayer are not there yet.

  ```
  openday --game-dir "<game dir>" --data ~/openday-data
  ```

  `--no-intro` skips the intro, `--mute` starts without sound and music,
  `--scale N` sets the window size, `--cheats` enables Tab + W / L in
  battle. The resolution of the battle screens (640×480 or 1024×768) is
  chosen in the main menu.

Everything these tools produce comes from your own game data; do not
redistribute it.

## Third-party components

See [THIRD_PARTY.md](THIRD_PARTY.md): SDL3, SQLite, stb_truetype, stb_vorbis
and the Liberation Sans font, all fetched at build time.

## Clean-room policy

Reverse engineering is used only to understand file formats and game
behavior, as permitted for interoperability. Findings are written up in our
own words under `docs/`, and the engine is implemented solely from those
documents. Decompiled code is never committed. See `docs/README.md`.

## License

GPL-3.0 — see [LICENSE](LICENSE).
