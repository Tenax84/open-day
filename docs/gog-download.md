# Download from GOG

When OpenDay finds no game files, the first page of its setup window
offers **Download from GOG...** beside *Game folder...* and *Installer or
CD image...*. A user who owns *Fallen Haven: Liberation Day* on GOG signs
in on GOG's own page, and
OpenDay downloads that account's Windows installer and copies the game out
of it, as it does from an installer chosen by hand (`docs/formats/INNO.md`).
Nothing in the installer is run. This is an OpenDay feature, not part of the
original game.

Code: `engine/update/gog.*` (addresses and answers, no network; tested in
`update_test`), `app/gog_download.*` (the requests, on a worker thread),
`engine/platform/web_sign_in*` (the sign-in panel), `app/setup.cpp` (the
page of the setup window, `docs/mechanics/interface.md`, "Setup window").

## 1. Sign-in

- The page: `https://auth.gog.com/auth?client_id=46899977096215655&redirect_uri=<landing>&response_type=code&layout=client2`.
  The client is GOG Galaxy's, the one GOG's sign-in page accepts for an
  installed program; its secret is published and sent with the code.
- The landing address: `https://embed.gog.com/on_login_success?origin=client`.
  After the sign-in GOG goes to it with `&code=<code>` added.
- **Panel** (Windows, when built with the WebView2 SDK and the WebView2
  runtime is installed — part of Windows 11 and current Windows 10): the
  page opens in a framed panel in the middle of the setup window (about
  560 × 720 at 100 %, smaller if the window is), over the first page, which stays as it was
  (not shaded, its buttons not taking the pointer), in
  the window's colours (title band "Sign in to GOG" with a close button). The
  navigation to the landing address is cancelled before it loads, and its
  address is kept: the user copies nothing. A page asking for a window of
  its own (sign-in with Google and the like) gets a second pane in the
  first one's place. Escape closes the pane it is pressed in; in the first
  pane, and the close button always, close the panel, which takes the user
  back to the first page. The browser keeps its files in
  `%LOCALAPPDATA%\OpenDay\Sign-in browser` and deletes every cookie as the
  page loads and as it lands, so no sign-in outlasts a panel.
- **Web browser** (macOS, Linux, or when the panel cannot start): the page
  opens in the default browser. GOG ends on an empty white page; the user
  copies its address from the address bar. The page notices a sign-in
  address on the clipboard by itself (checked twice a second, never one
  that was there when the page was opened); **Paste address** takes the
  clipboard too. **Open GOG again** reopens the page, **GOG store page**
  opens the game's store page, **Cancel** or Escape goes back.
- The code is read from the whole address (`code=` in its query) or taken
  as it is; it must be at least 16 characters of letters, digits, `-` and
  `_`. A code is used once.

## 2. Requests

All GET, over HTTPS (`engine/platform/http`: WinHTTP, else the system's
libcurl), `User-Agent: OpenDay`:

1. `https://auth.gog.com/token?client_id=…&client_secret=…&grant_type=authorization_code&code=<code>&redirect_uri=<landing>`
   → `access_token`. Any other answer: "GOG did not accept the sign-in".
   The token is kept in memory, for this download only.
2. `https://api.gog.com/products/1119459074?expand=downloads` (no sign-in
   needed) → `downloads.installers[]`: the first with `os` = `windows` and
   `language` = `en`, else the first Windows one. Its `files[]` each have a
   `downlink` (`https://api.gog.com/…`) and a `size`. The first file is the
   installer; any further ones are its `.bin` parts. (In 2026 the game has
   one file, `en1installer0`, 423 624 704 bytes.)
3. For each file, its `downlink` with `Authorization: Bearer <token>` →
   `downlink`, the file's real address. 401, 403 or 404: "This GOG account
   does not have Fallen Haven: Liberation Day".
4. The file itself, into the downloads folder (`SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)`,
   else `<home>/Downloads`, else `<per-user folder>/downloads`).

## 3. The file

- Its name is the last part of the real address's path, unescaped, when it
  is a plain file name (letters, digits, `. _ - ( )`, not starting with a
  dot, at most 160 characters); after the download, the name in the final
  address of the redirects wins.
- A whole file already there with the size GOG gives is taken as it is.
- While incomplete it is `<name>.part`. A part from an earlier try is
  resumed with `Range: bytes=<size>-`: a 206 whose `Content-Range` starts
  there continues it, a 200 starts it again. A 416 deletes the part (the
  next try starts afresh). Every redirect must stay on HTTPS, and the final
  address on `gog.com`, `gog-cdn.com`, `gogcdn.net` or `gog-statics.com`
  (or a subdomain).
- Before writing, the free space is checked against what is still to come.
  A file may have at most 8 GiB.
- When the part is whole it is renamed. A cancelled or broken-off download
  leaves the part; the next try resumes it.

## 4. The page

While downloading it shows the phase (signing in, asking for the
installer, downloading `<file>` into `<folder>`), the megabytes arrived and
a bar, and **Cancel**. A failure is told in one sentence (sign-in not
accepted, not owned, no Windows installer, network, unreadable answer,
disk, no room, cancelled) in a dialog with **Try again** and **Close**; the
reason in English goes to stderr. When the installer is whole, the copy
begins as with *Installer or CD image...* (`installGameFiles`).

## 5. Building

The panel is built with MSVC on Windows: `cmake/Dependencies.cmake`
fetches the `Microsoft.Web.WebView2` NuGet package (a zip file; pinned by
version and SHA-256) unless `OPENDAY_WEBVIEW2_DIR` names an unpacked one;
only its header and `WebView2LoaderStatic.lib` are used, so nothing is
added to the package but the license text. `-DOPENDAY_WEBVIEW2=OFF` builds
without the panel; elsewhere `web_sign_in::available()` is false and the
web browser is used.

## 6. Trying it again

`first_run=1` in `openday.cfg` makes the next interactive start skip the
remembered game folder and the usual install locations and show the first
page, until a game folder is chosen (the key is then removed). A local
`reset.bat` (not part of the repository) deletes the copied game, the
downloaded installer and `game_dir`, and sets it.
