# Automatic updates

OpenDay updates itself from the GitHub releases of `Tenax84/open-day`. This
is an OpenDay feature, not part of the original game.

## 1. Check

- On start (not in the `--script`/`--shot` test mode) a worker thread asks
  `GET https://api.github.com/repos/Tenax84/open-day/releases/latest`
  (headers `Accept: application/vnd.github+json`,
  `X-GitHub-Api-Version: 2022-11-28`, `User-Agent: OpenDay/<version>`).
  Unauthenticated: 60 requests per hour and address, one per start.
- Off for good with `check_updates=0` in `openday.cfg`, for one run with
  `--no-update-check`.
- `releases/latest` already skips drafts and pre-releases; they are ignored
  anyway. The release is newer when `compareVersions(tag, OPENDAY_VERSION)`
  is positive: dotted numbers compared numerically, a leading `v` ignored, a
  `-suffix` ranks below the plain version, `+build` is ignored.
- A 404 (no published release, or the repository still private) and network
  errors are silent: the menu simply shows no offer.

## 2. Offer

- When the main menu is shown and a newer release is known, the update
  dialog opens once per session, unless the user answered *No* to that same
  version before (`update_declined=<version>` in `openday.cfg`). A line at the
  bottom of the main menu stays: "OpenDay X is available - press U".
- The dialog is the campaign message box (frame 477, YES 478, NO 482).

## 3. Package and checksum

- Package name, as `cmake/Packaging.cmake` makes it:
  `OpenDay-<version>-<os>-<arch>.<ext>` with os `windows` / `macos` /
  `linux`, arch `x64` / `arm64`, ext `exe` / `dmg` / `tar.gz`.
- Expected SHA-256: the asset's `digest` (`sha256:<hex>`, given by GitHub for
  every uploaded asset); if missing, the line of the `SHA256SUMS` asset that
  the release workflow publishes. Without either the download is refused.
- The download goes to `<per-user folder>/updates/` as `<name>.part`, hashed
  while it arrives; size and hash are checked before it is renamed. The
  folder is emptied on every start.
- HTTPS only, also across redirects (the assets are served from another
  host). Windows: WinHTTP with the system proxy settings. macOS and Linux:
  the system libcurl, loaded with `dlopen` (`/usr/lib/libcurl.4.dylib`;
  `libcurl.so.4` and variants). Without libcurl there is no update check.

## 4. Installation

The program updates itself only when it can tell how it was installed;
otherwise *Yes* opens the release page in the browser.

| System | Condition | Installation | Afterwards |
|---|---|---|---|
| Windows | `Uninstall.exe` next to `openday.exe` (installed by the NSIS installer) | the installer is started with `ShellExecuteEx` (it asks for elevation) and `/D=<install folder>`; OpenDay quits. The installer removes the old version first; its last page offers *Run OpenDay*. | — |
| macOS | running from `<name>.app`, not translocated, not from `/Volumes/`, the folder of the bundle writable | `hdiutil attach` the image, `ditto` the bundle to `.OpenDay-update.app` beside the installed one, detach, rename the installed bundle to `.OpenDay-old.app`, the new one into its place, delete the old one (rolled back on failure) | `open -n <bundle> --args --no-intro`, quit |
| Linux | `openday` in a `bin` folder whose parent has `share/doc/OpenDay/LICENSE` (an unpacked release archive), both writable | the archive is unpacked in memory; its top folder is dropped, paths leaving the installation are skipped; every file is written as `<file>.openday-update` and renamed over the old one (safe for the running program) | `<prefix>/bin/openday --no-intro`, quit |

macOS: files the program downloads itself carry no quarantine attribute, so
Gatekeeper does not block the new, ad-hoc signed bundle.

## 5. States

`Idle → Checking → UpToDate | Available | Failed`;
`Available → Downloading → Ready | Failed | Available (cancelled)`.
*No* during the download cancels it. In `Ready` *Yes* starts the installer
(Windows) or the new version (macOS, Linux) and quits; *No* keeps playing
(macOS and Linux: the new version starts next time).
