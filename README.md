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

- **macOS (arm64):** open the `.dmg` and drag `OpenDay.app` onto the
  Applications folder (the command-line tools are inside the app, in
  `OpenDay.app/Contents/MacOS`). The app is signed ad hoc only, not notarized: on first start,
  right-click it and choose *Open* (on macOS 15 and later: try to open it once,
  then *System Settings → Privacy & Security → Open Anyway*), or run
  `xattr -dr com.apple.quarantine /Applications/OpenDay.app` in Terminal.
- **Windows (x64):** run the installer `.exe`; it installs into
  `Program Files\OpenDay` with a Start menu entry and an uninstaller. The
  installer is not code-signed, so SmartScreen may warn: choose *More info →
  Run anyway*. Silent install: `OpenDay-<version>-windows-x64.exe /S`
  (optionally `/D=<folder>` as the last argument). Uninstalling keeps your
  settings, saves and converted data in `%APPDATA%\OpenDay`.
- **Linux (x64):** unpack the `.tar.gz` and start `bin/openday`.

### Getting the game files

OpenDay needs the game files of your own copy. Any of these works:

- **The GOG installer** (`setup_fallen_haven_liberation_day_*.exe`, together
  with its `.bin` files if it has any) or **an image of the game CD**
  (`.iso`, or the `.bin` of a `.bin`/`.cue` pair). OpenDay does not run the
  installer: it reads the files out of it and copies the game into the
  per-user folder (`game` in the table below). Choose *Installer or CD
  image...* in the window OpenDay shows when it finds no game files, or
  start it once with `--install-from <file>`. This works the same on
  Windows, macOS and Linux.
- **Download from GOG...** in the same window: sign in to GOG on GOG's own
  page (on Windows in a panel inside the window, elsewhere in your web
  browser, copying the address of the empty page GOG ends on), and OpenDay
  downloads the Windows installer of your copy into your downloads folder,
  resuming a broken-off download, and copies the game files out of it. The
  sign-in is kept in memory for this one download; OpenDay never sees your
  password. See [docs/gog-download.md](docs/gog-download.md).
- **An installed game folder**, the one that contains `GLOBALS`, `IMGS`,
  `INTERF`, `SCENS`, `ANMSUNIT` and so on. On Windows, OpenDay finds the
  usual GOG install locations by itself. A copy of the folder (or one
  extracted with [innoextract](https://constexpr.org/innoextract/)) can be
  put into the per-user `game` folder:

  - Windows: `%APPDATA%\OpenDay\game`
  - macOS: `~/Library/Application Support/OpenDay/game`
  - Linux: `~/.local/share/openday/game`

  or chosen with *Game folder...* in the same window, or passed with
  `--game-dir`. Upper or lower case file names both work.

OpenDay looks for the game in this order: `--install-from`, `--game-dir`,
the `OPENDAY_GAME_DIR` environment variable, the folder remembered from an
earlier start, the `game` folder above, and the usual GOG locations
(including Wine prefixes and CrossOver bottles). If none holds the game, a
message explains what to do and lets you choose a folder, an installer or a
CD image, or download the installer from GOG.

Installers made with Inno Setup 5.0 to 6.4 are read (GOG builds its offline
installers with Inno Setup); old GOG installers that keep their files in
a RAR archive, and password-protected installers, are not. A CD image must
contain the game folders themselves; a disc that holds only a compressed
setup cannot be used this way.

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
openday [--game-dir DIR | --install-from FILE] [--data DIR] [--convert] [--no-intro] [--mute] [--scale N] [--cheats] [--no-update-check] [--mcp] [--mcp-port N]
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | the game folder (remembered for later starts) |
| `--install-from FILE` | copy the game files out of the GOG installer or a CD image into the per-user `game` folder, then start |
| `--data DIR` | folder of the converted databases (default: `data` in the per-user folder) |
| `--convert` | convert the databases again |
| `--no-intro` | skip the intro videos |
| `--mute` | no sound or music for this run |
| `--scale N` | window size as a multiple of 640×480 |
| `--cheats` | Tab + W / Tab + L win or lose a battle |
| `--no-update-check` | do not look for a newer release this time |
| `--mcp` | start the debug server (MCP over HTTP on `127.0.0.1:47800`, see [docs/debug-mcp.md](docs/debug-mcp.md)) |
| `--mcp-port N` | the debug server on port N |

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

### Updates

On start OpenDay asks GitHub whether a newer release exists and, if so,
offers it in the main menu. *Yes* downloads the package of your system,
checks its SHA-256 and installs it: on Windows the installer runs (OpenDay
closes meanwhile), on macOS the app in the Applications folder is replaced,
on Linux the unpacked folder is updated; then the new version starts. Where
that is not possible (e.g. a self-built copy or a read-only folder), the
release page opens instead. *No* skips that version. Turn the check off with
`check_updates=0` in `openday.cfg` or for one run with `--no-update-check`.
Details: [docs/updates.md](docs/updates.md).

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
- `installtool`: shows what a GOG installer or CD image contains and copies
  the game files out of it (`installtool info FILE`,
  `installtool install FILE GAME_DIR`; `inno-list` / `inno-extract` for
  every file of an Inno Setup installer).
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

## Contributing

Bug reports, tests on macOS and Linux, research notes and code are welcome;
please read [CONTRIBUTING.md](CONTRIBUTING.md) first. Questions and ideas go
to [Discussions](https://github.com/Tenax84/open-day/discussions); security
problems are reported privately as described in [SECURITY.md](SECURITY.md).

## License

GPL-3.0, see [LICENSE](LICENSE). Third-party components:
[THIRD_PARTY.md](THIRD_PARTY.md).
