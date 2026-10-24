# OpenDay

OpenDay is an open-source, native reimplementation of the engine of the 1998
turn-based strategy game *Fallen Haven: Liberation Day*, in the spirit of
OpenRA, OpenXcom and fheroes2. It runs natively on **macOS (Apple Silicon)**,
Windows and Linux.

> **Status:** work in progress. Single player is playable: the campaign
> (four colonies) and skirmish battles against the computer, with automated
> units (air strikes, drops, transports), the encyclopedia and saving.
> Network play (and with it death match, which has no computer player) is
> not there yet.

## Disclaimer

OpenDay is an **unofficial** fan project. It is not affiliated with, endorsed
by or connected to the original developers or publishers. "Fallen Haven" is a
trademark of its respective owner and is used here only to describe
compatibility.

OpenDay contains **no original game content**: no executables, graphics,
sounds, videos, maps or databases, neither in the repository nor in the
release packages. You need your own legitimate copy of the game (for example
from GOG). OpenDay reads the data files from your copy at run time.

## Installing

Download the package for your system from the
[releases](https://github.com/Tenax84/open-day/releases) page:

- **macOS (arm64):** unzip and move `OpenDay.app` to Applications. The app is
  signed ad hoc only, not notarized: on first start, right-click it and choose
  *Open* (on macOS 15 and later: try to open it once, then *System Settings →
  Privacy & Security → Open Anyway*), or run
  `xattr -dr com.apple.quarantine OpenDay.app` in Terminal.
- **Windows (x64):** unzip anywhere and start `openday.exe`.
- **Linux (x64):** unpack the `.tar.gz` and start `bin/openday`.

### Getting the game files

OpenDay needs the folder of an installed game, the one that contains
`GLOBALS`, `IMGS`, `INTERF`, `SCENS`, `ANMSUNIT` and so on.

- **Windows:** install the game from GOG (GOG Galaxy or the offline
  installer). OpenDay finds the usual install locations by itself.
- **macOS and Linux:** the GOG release is a Windows installer. Either extract
  it with [innoextract](https://constexpr.org/innoextract/)
  (`brew install innoextract`, or your distribution's `innoextract` package):

  ```
  innoextract --gog -d ~/fh-install setup_fallen_haven_liberation_day_*.exe
  ```

  (the game folder is `~/fh-install` or `~/fh-install/app`), or copy the
  installed folder from a Windows PC. Then put the folder where OpenDay
  looks for it:

  - macOS: `~/Library/Application Support/OpenDay/game`
  - Linux: `~/.local/share/openday/game`

  or choose it in the dialog OpenDay shows when it finds no game files, or
  pass `--game-dir`. Upper or lower case file names both work.

OpenDay looks for the game in this order: `--game-dir`, the
`OPENDAY_GAME_DIR` environment variable, the folder remembered from an
earlier start, the `game` folder above, and the usual GOG locations
(including Wine prefixes and CrossOver bottles). If none holds the game, a
message explains what to do and lets you choose the folder.

### First start

On the first start (and after the game files change) OpenDay converts the
game's databases (Jet/Access `.MDB` files, also those inside the `.ZIP`
archives) to SQLite. This takes a few seconds and shows a progress window.
Nothing is written into the game folder.

Per-user files (converted data, saves, settings in `openday.cfg`):

| System | Folder |
|---|---|
| Windows | `%APPDATA%\OpenDay\` |
| macOS | `~/Library/Application Support/OpenDay/` |
| Linux | `~/.local/share/openday/` |

### Command line

```
openday [--game-dir DIR] [--data DIR] [--convert] [--no-intro] [--mute] [--scale N] [--cheats]
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | the game folder (remembered for later starts) |
| `--data DIR` | folder of the converted databases (default: `data` in the per-user folder) |
| `--convert` | convert the databases again |
| `--no-intro` | skip the intro videos |
| `--mute` | no sound or music for this run |
| `--scale N` | window size as a multiple of 640×480 |
| `--cheats` | Tab + W / Tab + L win or lose a battle |

Environment variables: `OPENDAY_GAME_DIR`, `OPENDAY_DATA_DIR`,
`OPENDAY_SAVE_DIR` (saves folder). On macOS the program is
`OpenDay.app/Contents/MacOS/OpenDay`.

### Saving

A campaign can be saved from the in-game options (*Save*) on the city and
continent screens and during a battle (after deployment, also while the
computer player moves); a loaded battle continues exactly where it was
saved. Skirmish battles cannot be saved, as in the original. The ten slots
are text files `saves/campaign.01` … `campaign.10` in the per-user folder.
Saves of the original game (`SAVEGAME\savegame.NN` in the game folder) can
be imported from the load dialog with *Original game saves*.

## Building from source

Requirements: CMake 3.21+ and a C++20 compiler (Xcode 15+ / Clang 15+,
MSVC 2022+, GCC 12+). SDL3 and SQLite are used from the system if installed,
otherwise CMake downloads pinned versions (see
[THIRD_PARTY.md](THIRD_PARTY.md)). On Linux, SDL3 needs the usual X11/Wayland
development packages (see `.github/workflows/build.yml`).

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
cmake --build build --config Release --target package   # optional: release package in build/packages
```

`-DOPENDAY_BUILD_VIEWER=OFF` builds only the libraries and the command-line
tools, without SDL3. With `OPENDAY_GAME_DIR` (and `OPENDAY_DATA_DIR`, a
converted data folder) set at configure time, extra tests read your local
installation. On Windows, `run.bat` builds and starts the game.

## Tools

These work on your own game files; do not redistribute what they produce.

- `mdbtool`: converts the game databases to SQLite, like the first start
  does (`mdbtool --game-dir DIR OUT_DIR`, `--file X.mdb`, `--list`).
  `tools/mdb/mdb2sqlite.py` is the same converter in Python (standard
  library only; optional).
- `dbitool`: inspects and extracts `IMGS\*.DBI` sprite archives.
- `fftool`: inspects and extracts the `ANMSUNIT\ANMSUNIT.FF` unit animations
  and stand-alone `.SMK` files.
- `mapview`: map viewer and hot-seat battle sandbox for every scenario
  (`mapview --game-dir DIR --data DATA_DIR --scenario s4-5-1`).

## Clean-room policy

Reverse engineering is used only to understand file formats and game
behavior, as permitted for interoperability. Findings are written up in our
own words under `docs/`, and the engine is implemented solely from those
documents. Decompiled code is never committed. See `docs/README.md`.

## License

GPL-3.0, see [LICENSE](LICENSE). Third-party components:
[THIRD_PARTY.md](THIRD_PARTY.md).
