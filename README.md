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

Planned stack: C++20, CMake, SDL3. Build instructions will follow once there
is code to build.

## Clean-room policy

Reverse engineering is used only to understand file formats and game
behavior, as permitted for interoperability. Findings are written up in our
own words under `docs/`, and the engine is implemented solely from those
documents. Decompiled code is never committed. See `docs/README.md`.

## License

GPL-3.0 — see [LICENSE](LICENSE).
