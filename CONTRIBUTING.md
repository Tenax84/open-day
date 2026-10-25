# Contributing to OpenDay

Thank you for your interest! Bug reports, testing on your system, format and
mechanics research, and code are all welcome.

## Ground rules (please read first)

OpenDay reimplements a commercial game. To keep the project legally clean,
these rules apply to **every** contribution, without exception:

1. **No original game files.** No executables, DLLs, graphics, sounds,
   videos, databases (`.MDB`), maps, saves, manuals, or any excerpt,
   screenshot crop, dump or converted version of them. This also applies to
   issue attachments, test data and release packages.
2. **No decompiled code.** No pseudocode or assembly from Ghidra, IDA or
   similar tools, not even renamed or rewritten line by line. No
   disassembler project files either.
3. **Clean-room workflow.** Whoever studies the original program writes down
   the findings **in their own words** under `docs/` (`docs/formats/*.md`,
   `docs/mechanics/*.md`): fields, offsets, algorithm steps, constants,
   observed behavior. The engine is implemented **only** from these
   documents. See [docs/README.md](docs/README.md).
4. **Test data is synthetic.** Tests build their own minimal input files.
   Tests that read a real installation are optional and take its location
   from `OPENDAY_GAME_DIR` / `OPENDAY_DATA_DIR`.
5. **Screenshots** show OpenDay only, never the original game.
6. **Trademark.** The name of the original game is used only to describe
   compatibility, never as branding.

Pull requests that break these rules cannot be merged, and offending content
has to be removed from the git history.

## Reporting bugs

Open an [issue](https://github.com/Tenax84/open-day/issues/new/choose) and
include the OpenDay version, your operating system, where your game files
come from (GOG installer, CD, installed folder) and what happened. Logs and
screenshots of OpenDay are helpful; files of the original game are not
allowed (describe them instead). Security problems go through
[SECURITY.md](SECURITY.md), not public issues.

Reports from **macOS (Apple Silicon)** and **Linux** are particularly
valuable: those builds are only checked in CI so far. The same goes for
installing from a real GOG offline installer or the original CD.

## Documentation contributions

Format and mechanics findings are contributions in their own right. Write
them in English, in your own words, and mark anything unverified (with "?"
or an *Unverified* note). Where it helps others check a finding, describe
how you confirmed it (observed behavior, test files).

## Code contributions

- **Language and build:** C++20, CMake, SDL3. See *Building from source* in
  the [README](README.md).
- **Layout:** `engine/` (platform, formats, rendering, data), `game/` (rules,
  battle, campaign), `app/` (the `openday` program), `tools/` (command-line
  tools), `tests/`, `docs/`.
- **Style:** follow the surrounding code. Code, comments and commit messages
  are in English.
- **Tests:** add a synthetic test for new format readers and rule changes,
  and make sure `ctest --test-dir build -C Release` passes.
- **Warnings:** CI builds on macOS arm64, Windows and Linux and must stay
  free of warnings.
- **Commits:** small, focused commits with a short imperative subject line
  ("Fix wall damage after loading"). Mention the `docs/` section a change
  implements where it applies.
- **Licensing:** OpenDay is GPL-3.0. By contributing, you agree that your
  contribution is licensed under the same terms. New third-party code must
  have a GPL-3.0-compatible license and be listed in
  [THIRD_PARTY.md](THIRD_PARTY.md).

## Code of conduct

Please be kind; see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
