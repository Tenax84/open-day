# Security policy

## Supported versions

Only the latest release receives fixes. Please check that a problem still
occurs with the newest release (or the `main` branch) before reporting it.

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Report them
privately through GitHub instead:
[Security → Report a vulnerability](https://github.com/Tenax84/open-day/security/advisories/new).

Include what you can of the following:

- the affected version and operating system,
- what an attacker can achieve and under which conditions,
- steps to reproduce, ideally with a small, self-made input file.

Please do **not** attach files of the original game (or modified copies of
them); describe the bytes that matter instead, or build a minimal synthetic
file.

You should get an answer within a week. Once a fix is released, the advisory
is published and you are credited unless you prefer otherwise.

## Scope

OpenDay is a game engine, but it handles untrusted input in several places.
Problems in these areas are especially welcome:

- **The updater** (`engine/update`, `app/updater.*`, `engine/platform/http`):
  it downloads release packages from GitHub, verifies their SHA-256 and
  installs them. Anything that lets a package be installed without a matching
  checksum, or makes the updater write outside the install folder, is in
  scope.
- **Installer and disc image reading** (`engine/formats/inno_setup`,
  `iso9660`, `lzma`, `inflate`, `zip`, `engine/data/game_source`): these
  parse files the user selects. Memory-safety bugs, endless loops and path
  traversal (writing outside the target folder) are in scope.
- **Game data and save files** (`engine/formats/*`, `engine/data/mdb_import`,
  save loading): crashes or memory corruption caused by malformed files.

Out of scope: cheating in single player, problems that need an attacker who
can already modify the OpenDay program files, and vulnerabilities in the
original game.
