# OpenDay

Nyílt forráskódú, natív motor-újraírás a **Fallen Haven: Liberation Day** (1998) körökre osztott stratégiai játékhoz, az OpenRA / OpenXcom / fheroes2 mintájára. Az OpenDay saját kód; a játékadatokat a felhasználó saját, jogszerű (GOG) telepítéséből tölti be.

**Elsődleges cél:** a játék natív futtatása macOS-en (Apple Silicon, arm64). Másodlagos: Windows és Linux.

## Jogi és munkamódszertani szabályok (KÖTELEZŐ)

Ezek a szabályok minden munkára vonatkoznak, kivétel nélkül.

1. **Eredeti fájl nem kerül a repóba.** Se exe, se DLL, se grafika, hang, videó, adatbázis (`.MDB`), pálya, mentés, kézikönyv vagy ezek részlete, kivágása, konvertált változata. Release-csomagba sem.
2. **Visszafejtett kód nem kerül a repóba.** A Ghidra/REA pszeudokódja vagy assemblyje, akár átírva, átnevezve, „átfogalmazva” sem. A Ghidra-projektfájlok sem.
3. **Tiszta szoba munkafolyamat:**
   - Visszafejtés (Ghidra, REA, hexa szerkesztő) → **saját szavakkal írt leírás** a `docs/` alá (`docs/formats/*.md`, `docs/mechanics/*.md`): mezők, eltolások, algoritmus lépései, konstansok, viselkedés.
   - Implementáció → **kizárólag a `docs/` leírásokból**, nem a decompiler kimenetéből.
4. **Védjegy:** a „Fallen Haven” név és logó nem szerepel a projekt nevében, ikonjában, brandingjében. Leírásban szerepelhet tényként („a Fallen Haven: Liberation Day adataival működik”). Nem hivatalos projekt, és nem is kelthet ilyen látszatot.
5. **Tesztadat:** csak saját készítésű, szintetikus mintafájlok (pl. generált minimális `.dbi`). Eredeti fájlokra épülő tesztek csak opcionálisak, és a felhasználó helyi telepítésére mutató útvonalból (környezeti változó) olvasnak.
6. **Képernyőkép:** csak az OpenDay motor futásáról; az eredeti játékról nem.
7. **Harmadik féltől származó, a játékkal szállított könyvtárak** (Smacker `SMACKW32.DLL`, Miles `MSS32.DLL`, DAO/Jet 3.5, MFC) helyett nyílt alternatívák: libsmacker vagy ffmpeg, SDL audio/SDL_mixer, mdbtools (vagy egyszeri MDB → SQLite konverzió a telepítéskor).

Jogalap (nem jogi tanács): az EU 2009/24/EK irányelv 5. cikk (3) bek. (működés tanulmányozása) és 6. cikk (dekompilálás együttműködés céljából), a magyar Szjt. megfelelő rendelkezései. A játékszabályok, mechanikák mint ötletek nem védettek; a kód és az assetek igen.

## Projekt állapota

- Git-repó inicializálva; GitHub: **`Tenax84/open-day`** (2026-10-25 óta **nyilvános**). Bekapcsolva: secret scanning + push protection, Dependabot (riasztások, biztonsági frissítések, `dependabot.yml` az Actionshöz), privát sebezhetőség-bejelentés, CodeQL alapbeállítás (C/C++, Actions), Discussions; külső PR-ek workflow-ja jóváhagyásra vár; rulesetek: `main` és `v*` tagek nem törölhetők/írhatók felül (direkt push a `main`-re megengedett). Közösségi fájlok: `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, issue/PR sablonok. A `gh` CLI `Tenax84` fiókkal van bejelentkezve.
- **Eldöntve (2026-10-22):**
  - Nyelv: **C++20 + CMake + SDL3**.
  - Licenc: **GPL-3.0** (`LICENSE`).
- **Kész:** `.DBI` formátum dokumentálva (`docs/formats/DBI.md`), olvasó (`engine/formats/dbi.*`), `dbitool` (info/list/extract/sheet → PNG), szintetikus teszt + opcionális teszt a helyi telepítésen (mind az 5976 sprite dekódolódik).
- Build Windowson: `cmake -S . -B build -G "Visual Studio 18 2026" -A x64` (VS 2026 Community van telepítve, 2022 nincs).
- **Kész:** `ANMSUNIT.FF` (`docs/formats/FF.md`): konténer Smacker 2 videókkal; saját SMK-dekóder (`engine/formats/smk.*`), `fftool`. Átlátszóság: a képkocka bal felső pixelének indexe a színkulcs.
- **Kész:** MDB-k (`docs/formats/MDB.md`, `docs/mechanics/data-model.md`), `tools/mdb/mdb2sqlite.py` (csak stdlib, mind a 122 adatbázis).
- **CI:** GitHub Actions, macOS arm64 / Windows / Linux, figyelmeztetés nélkül.
- **Kész:** `PALCYCLE.PAL` (`docs/formats/PALCYCLE.md`, `engine/formats/palcycle.*`): 100 ms-os lépés, 18 lépéses ciklus, a 235–244 indexek cserélődnek.
- **Kész:** taktikai térkép terepének kirajzolása (`docs/mechanics/isometric-map.md`): `engine/data` (SQLite), `engine/render` (8 bites felület), `game/map` (betöltés + izometrikus rajzolás), `tools/mapview` (SDL3, palettaciklussal, `--screenshot`). Mind a 114 pálya hiánytalanul kirajzolódik.
- Függőségek: SDL 3.4.16 és SQLite 3.50.4 FetchContenttel (`cmake/Dependencies.cmake`), hash-sel rögzítve.
- **Kész:** pályaobjektumok (`docs/mechanics/map-objects.md`, `game/map/map_objects.*`, `rules.*`): utak (G/B/R készlet, feljárók), falak (sérülés páncélból, törött végek), aknák, romok, többcsempés épületek, álló egységek (erdei gyalogos-póz, hídon −5 px). Közös, cellánkénti mélységi rajzolás rétegsorrenddel. Mind a 114 pálya hiánytalan.
- **Kész:** egységanimációk a térképen (`docs/mechanics/unit-animation.md`): `engine/render/animation.*` (SMK → színkulcsos, kivágott képkockák + hang, gyorsítótár), `game/anim/` (klipnevek, irányok, elhelyezés, csúszási és lövedékpályák, 15 tick/s idővonal, mozgás L/M/N, lövés S/T/U + P/I, robbanás E + rom, épületrobbanás). `mapview`: kijelölés, mozgatás, támadás, X = robbantás, hang; képernyőkép-módban `--do`/`--ticks`.
- **Kész:** taktikai játékmenet magja (`docs/mechanics/tactical-movement.md`, `tactical-combat.md`): `game/battle/` – állapot (`battle_state`), járhatóság + az eredeti söprő útkeresése + AP-vágás + útvonal-előnézet (`movement`), oldalak/tűzvonal/lőtáv/találati esély (`combat`), kérés/esemény üzenetek közös `applyEvent`-tel (`messages`), szerver (`server`: mozgás megszakításokkal – reakciótűz, aknafelderítés, aknarobbanás –, lövés, területi sebzés, AP-sebzés/negAP, út/híd, terepégés, roncs/romok, körváltás, körlimit). Kliens: `game/anim/battle_view` (replika + animációk + jelenet). `mapview`: hot-seat csata. `battle_test` szintetikus tesztek.
- **Kész:** csatamenet és AI (`docs/mechanics/battle-flow.md`, `tactical-ai.md`): körsorrend (T = 1-ről, emberek slot-sorrendben, a gép utolsó; death matchben nincs gép, véletlen kezdés), objektívák 0–10 (`game/battle/objectives`), erősítések, Probability/Turn parancs-lejárat, `BattleOver` eredménnyel; a gépi játékos (`game/battle/ai`): fázisok, álló tűz, vadászat, Goto/Goto Fast csoport, Escort/Defend/Patrol/Attack, pontozás és ±1 döntetlen-szabály, tüzelőgyűrűk, move-to-fire. `PathMap` (egy keresés, sok cél). `mapview`: a gép játszik (`--no-ai` = hot-seat).
- **Kész:** felület alapjai és csata-képernyők (`docs/mechanics/interface.md`, `tactical-interface.md`, `docs/formats/INTERFACE-SMK.md`): szöveg Liberation Sans-szal (beágyazva, stb_truetype, élsimítás nélkül), string-tábla a nyelvi DLL-ből (`formats/pe_resources`), UI-atlasz és vezérlők (`engine/ui`), hang/zene (`engine/platform/audio`, stb_vorbis). Játékprogram `app/` (`openday`): intro, főmenü, felbontás, szkirmis-lista, töltőkép, telepítés, taktikai/várakozó nézet (panel, minitérkép, egységinfó, info card, csempe-kurzorok, kétkattintásos mozgás, tűzmód, GROUP/CENTER/MOVE ALL/léptetés/GUARD/END TURN), GOAL, in-game opciók, csatajelentés; 640×480 és 1024×768. Telepítés a szerveren (`DeployRequest`/`UndeployRequest`, zónák, keret). `--script`/`--shot` tesztmód.
- **Kész:** kampány (`docs/mechanics/campaign.md`, `city-building.md`, `army-tech.md`, `strategic-interface.md`): `game/campaign/` (kolónia-adatok, állapotgép 0–9, nehézségi interpoláció M/C/D(d), provinciacsata-előkészítés, városcsaták: első támadás + ellentámadás, zónák és támadó-elhelyezés, tornyok egységgé és vissza, jutalmak, kolóniaváltás, saját szöveges mentésformátum), városépítés (elhelyezési szabályok 384+kód, szegmensek, hidak, bontás, mozgatás, feloldások), tech-szintek és fejlesztés. Csatamotor: vásárolt erősítés + körönkénti jövedelem, tech-épületek birtoklása. `app/`: új kampány menü, betöltés/mentés (10 hely), videók, kontinens-képernyő (gyarmattérkép-animáció, kijelölés, győzelmi nézet), Intelligence, városképernyő, Army képernyő (stratégiai/szkirmis/telepítési), csata előtti áttekintő térkép, kampány-csatajelentés. `campaign_test` (szintetikus + `OPENDAY_DATA_DIR`).
- **Kész (2026-10-24):**
  - Automata egységek (`docs/mechanics/automated-units.md`, `game/battle/automated.*`, `server_automated.cpp`): célzás, ár, `DeployOnce`, AA, kategóriánkénti hatások, `AUTOUNIT` klipek, gépi hívások (véletlen, ütemezett, vészledobás, ellentámadás-szállítás), az automata-nézet (507).
  - Csataeredmény (`battle-flow.md` 5): számlálók `DamageCause` alapján, pontszám (a szándékolt képlet, tudatos javítás), feladás, jelentés.
  - Szabályjavítások: `Jammer`, AI 42×42-es keresési ablak és 7 kör, Overwatch AP-tartalék, csoportmozgás, kétkattintásos lövés, forgatás kattintással, ikonok az info cardon.
  - Grafika: színtábla, aknák (001 = élesített, láthatóság), sérült épület-sprite (≤ 33 %), rajzolási módok + „draw on top”, takarási táblák, tickenkénti rendezés, aknarobbanás-klip, térképkeret.
  - Enciklopédia (`encyclopedia.md`, `game/info/`, `app/encyclopedia_screen.cpp`), taktikai (41) és városi (578) térképképernyő (`app/map_screen.*`). Az EXE-ből jövő bitmapek futásidőben a felhasználó telepítéséből töltődnek.
  - Mentés csata közben (`savegame.md`, `game/battle/battle_save.*`, 2-es szöveges formátum, atomikus írás, RNG is), az eredeti `savegame.NN` import (`game/campaign/savegame_import.*`).
  - Natív adat-előkészítés: saját ZIP/inflate + Jet-olvasó (`engine/formats/zip|inflate|mdb`, `engine/data/mdb_import`), első indításkor automatikus konverzió a felhasználói mappába; `tools/mdbtool`; a Python-eszköz opcionális. Játékmappa-keresés (paraméter, env, `openday.cfg`, alapértelmezett helyek, mappaválasztó), kis-/nagybetű-független fájlkeresés.
  - Csomagolás: CMake install + CPack (macOS `.app` zip, Windows zip, Linux tar.gz), `.github/workflows/release.yml` (`v*` tag); kézi indítás (`gh workflow run release.yml`) = próbafuttatás, minden lefut a kiadás létrehozásáig. README újraírva. Git-történet átvizsgálva: tiszta.
- **Kész (2026-10-25):** automatikus frissítés a GitHub Release-ekből (`docs/updates.md`): `engine/update` (release-JSON, verzió-összehasonlítás, csomagnév), `engine/platform/http` (WinHTTP; macOS/Linux: rendszer-libcurl `dlopen`-nel), `formats/json|sha256|tar`, `app/updater.*` (szál, letöltés + SHA-256 a `digest`-ből vagy `SHA256SUMS`-ból, telepítés: Windows NSIS-telepítő, macOS `.app`-csere a DMG-ből, Linux kicsomagolás a helyére, újraindítás), dialógus a főmenüben (U), `check_updates` / `update_declined` / `--no-update-check`. Amíg a repó privát, az API 404-et ad → nincs ajánlat. A macOS/Linux telepítési ág csak CI-ban fordul, élesben még nem próbálta senki.
- **Kész (2026-10-25):** játékfájlok a telepítési médiumból (`docs/formats/INNO.md`, `ISO9660.md`): saját LZMA/LZMA2-dekóder (`formats/lzma`), MD5/SHA-1 (`formats/hash`), ISO 9660 + Joliet, nyers 2352 bájtos `.bin` (`formats/iso9660`), Inno Setup 5.0–6.4.3 olvasó (`formats/inno_setup`: beágyazott és külső `-N.bin` szeletek, tárolt/zlib/LZMA/LZMA2, x86-hívásszűrő, ellenőrzőösszeg, GOG Galaxy-darabok), `engine/data/game_source` (játékmappa felismerése, biztonságos útvonalak, `.partial` mappa + átnevezés). `openday --install-from FILE` és „Installer or CD image...” gomb → `<felhasználói mappa>/game`. `tools/installtool`, `lzma_test`, `install_test` (szintetikus ISO és Inno). Helyben ellenőrizve: Inno 5.5.9–6.4.3 telepítők bájtra egyeznek az innoextract-tal, 16 szintetikus ISCC-telepítő (Galaxy-darabokkal), az eredeti játékmappából készült ISO-ból indul a játék. Valódi GOG offline telepítővel és az eredeti CD-vel még nem próbálta senki; Inno 6.5+ (új formátum) és a RAR-os régi GOG-telepítők nem támogatottak.
- **Kész (2026-10-26):** „Download from GOG...” az első indítás ablakában (`docs/gog-download.md`, az open-annihilation megoldásának átvétele): bejelentkezés a GOG saját oldalán – Windowson WebView2-panelben az ablakon belül (`engine/platform/web_sign_in*`, a WebView2 SDK FetchContenttel, hash-sel; `OPENDAY_WEBVIEW2`, `OPENDAY_WEBVIEW2_DIR`), máshol böngészőben + a végcím a vágólapról –, majd a GOG Galaxy (nyilvános) kliensazonosítójával token, `api.gog.com/products/1119459074?expand=downloads`, a fiók Windows-telepítőjének letöltése a Letöltések mappába (`.part` folytatással, `Range`), `installGameFiles`. `engine/update/gog.*` (hálózat nélküli részek, `update_test`), `app/gog_download.*` (szál), `platform/http`: `onHead` (státusz, Content-Range, végső cím). `first_run=1` az `openday.cfg`-ben: a következő indítás a mentett és a szokásos helyeket kihagyva az első ablakot mutatja; a helyi `reset.bat` (nincs a repóban, `.git/info/exclude`) ezt állítja be, és törli a másolt játékot és a letöltött telepítőt (`keep-installer`). Windowson kipróbálva: a panel megnyílik, bezárva visszatér; a valódi GOG-fiókos letöltést még nem próbálta senki.
- **Kész (2026-10-26):** beállító ablak a játék stílusában (`interface.md` „Setup window”, `app/setup_ui.*`): egyetlen 640×480-as képernyő a programablakban (skálázva, éles szöveggel), saját palettával rajzolt terepmintás panelek, címsáv, olajzöld gombok, fekete szövegdobozok; lapok: első választás (súgó a gomb alatt), GOG-letöltés (a WebView2-panel az árnyékolt lap fölött), másolás, konvertálás; üzenetek és hibák játékstílusú dialógusban (`SetupUi::dialog`), a rendszer fájl-/mappaválasztója az ablak fölött. Ugyanez az ablak megy tovább a játék ablakaként (`SetupUi::release`). Kilépéskor nincs külön hibaüzenet.
- **Kész (2026-10-26):** beépített debug MCP-szerver (`docs/debug-mcp.md`): `openday --mcp` (`--mcp-port N`) → Streamable HTTP, csak `127.0.0.1:47800/mcp`, Origin-ellenőrzés. `app/mcp_server.*` (socketek, JSON-RPC, kapcsolatonként szál), `app/debug_tools.*` (a hívások a főciklusban futnak, képkockák között; várakozó hívások), `app/battle_control.hpp` (a `BattleScreen` valósítja meg; a parancsok a felület útját járják). Eszközök: képernyőkép (tömörített 8 bites PNG: `formats/deflate`, `encodePngIndexed`), kattintás/billentyű/szöveg, `wait`/`wait_idle`, `set_speed`, szkirmis indítása, `battle_state`/`unit`/`map`/`path`/`events`, telepítés, mozgás, lövés, körvége, `battle_force_result`. Eseménynapló: `describeEvent` (`OPENDAY_LOG` is ezt írja). A Claude Code-ban helyi hatókörrel regisztrálva (`claude mcp add --transport http openday http://127.0.0.1:47800/mcp`); futásidejű hibakereséshez a játékot `--mcp`-vel kell indítani, utána `/mcp` újracsatlakozás. Windowson kipróbálva egy teljes szkirmis-körrel.
- **Kész (2026-10-26):** éles szöveg (`interface.md` „Sharp text”, `sharp_text` beállítás, az OpenDay-beállítások új *Display* lapján a „Resizable window” mellett): `render/text_layer` felveszi a képkockába rajzolt szövegeket (előtte/utána pixelek), a láthatóságot a kész képkockából számolja, és a megjelenítő másolatból kiveszi őket; `app/sharp_text` glifatlasszal, élsimítva, ablakfelbontásban rajzolja vissza, csak a látható pixelekre. A képernyőképek (MCP, `--shot`) továbbra is a pixeles szöveget mutatják.
- **Kész (2026-10-26):** skálázás (`interface.md` „Scaling”): a képkocka-textúra mindig `SDL_SCALEMODE_PIXELART` (sharp bilinear). A korábbi `smooth_scaling` és `pixel_perfect` opciót a HD felület miatt töröltük (a cfg-kulcsokat figyelmen kívül hagyjuk).
- **Kész (2026-10-26):** HD felület (`interface.md` „HD interface”, `hd_interface`, alapból be, a *Display* lapon): `render/image_layer` felveszi az atlaszelemek kirajzolását (`blit`, `drawTiled`), a láthatóságot a kész képkockából számolja; `app/hd_interface` ablakfelbontásban rárajzolja a nagy felbontású változatot: kizárólag a csomagból (`<felhasználói mappa>/hd-interface/640|1024/NNNN.png`, stb_image); ami nincs benne, az eredeti pixelekkel marad (az xBRZ-tartalékot 0.5.0 után töröltük). `openday --export-interface [DIR]` kiírja az elemeket PNG-be a csomag elkészítéséhez. 1,01-es szorzó alatt nem rajzol. `openday --make-hd-interface [PROGRAM] [--upscale-model NAME]` (`app/hd_pack.*`) a felhasználó saját `realesrgan-ncnn-vulkan`-jával elkészíti a csomagot (alapból `realesr-animevideov3`, 4×; az üres/fekete eredmények újra 32-es csempékkel; ami így sem megy, eredeti marad). GT 730-on mind a 888 elem 72 s. Automatikus csomag (`app/hd_pack_auto.*`, „Automatic pack”): ha az opció be van kapcsolva (induláskor vagy bekapcsoláskor) és a csomag hiányzik (nincs PNG a `640` és az `1024` mappában), a háttérben elkészül; ha nincs felskálázó, a Real-ESRGAN v0.2.5.0 hordozható változata letöltődik a saját GitHub-kiadásából (méret + SHA-256 rögzítve) a `<felhasználói mappa>/realesrgan` alá; kész → a képek azonnal megjelennek; hiba esetén az opció kikapcsol, és játékstílusú üzenetablak (`makeMessageDialog`) mutatja az okot; a `hd_pack` kulcs elavult. Windowson kipróbálva (letöltés, majd a játék futása közben a csomag kb. 80 s alatt); macOS/Linuxon még senki.
- **Kész (2026-10-26):** saját programikon (`assets/icon/`: 1024 px mester, `openday.ico`, `openday.icns`, `openday-256.png`; a Codexszel készült, az eredeti logóra nem hasonlít; `tools/icon/make_icons.py` állítja elő a többit a mesterből, Pillow kell). Windows: `app/openday.rc` (exe-ikon; az SDL ebből veszi az ablakét), NSIS-telepítő ikonja; macOS: `CFBundleIconFile` a bundle-ben; Linux: beágyazva, `SDL_SetWindowIcon` (`app/window_icon.*`). Windowson kipróbálva (Intéző, címsor); a macOS/Linux ág és az NSIS-ikon csak CI-ban fordul.
- **Kész (2026-10-26):** teljes képernyő (`interface.md` „Full screen”, `fullscreen`, alapból ki): F11 bárhol vált (a képernyők nem kapják meg), a *Display* lapon „Full screen” sor; asztali mód, keret nélkül; a rendszer saját váltását (macOS zöld gomb) is követi; induláskor visszaáll. Windowson kipróbálva.
- Nyitott: hálózati játék (szkirmis, death match, kampány); a death match emiatt nincs a menüben. macOS/Linux build és csomag csak CI-ban ellenőrizhető; valódi Macen még nem próbálta senki. A `TechReward` a lerombolt épületeknél, az AA-célzás a kamera szélén, a falak törött végének visszaállítása betöltéskor.
- Build: a `build-main` könyvtár az aktuális; az opcionális tesztekhez a konfiguráláskor kell az `OPENDAY_GAME_DIR` és `OPENDAY_DATA_DIR` környezeti változó.

## Tervezett repó-higiénia

- `.gitignore` már az első commitban: `*.MDB *.mdb *.DBI *.FF *.SMK *.ZIP *.EXE *.DLL *.WAV *.ogg *.PAL *.BMP`, `data/`, `gamedata/`, Ghidra-projektek (`*.gpr`, `*.rep/`), REA evidence/cache.
- Ha mégis felkerül eredeti fájl: nem elég törölni, a git-történetből is ki kell írtani (git filter-repo) push előtt.
- GitHub Actions: natív build macOS arm64 / Windows / Linux; a Release csak a motort tartalmazza.
- README: felelősségkizárás (nem hivatalos, saját példány kell), telepítési útmutató (hol van a GOG-telepítés).

## Az eredeti játék (referencia)

**Telepítés helye:** `C:\Program Files (x86)\GOG Galaxy\Games\Fallen Haven Liberation Day` (GOG, game id 1119459074). A motor ezt az útvonalat konfigurálhatóan kérje (pl. `OPENDAY_GAME_DIR`).

**Program:**
- `EXE\LIBDAY.EXE` – ~1,1 MB, PE32, i386, GUI. Microsoft Visual C++ + MFC 4.2 (`MFC42.DLL`, `MSVCRT.DLL`).
  - SHA-256: `c138adb009c3fdb7b9090572d68f52ec59e3019ca8d5fc53fac9a16c52ba5389`
  - Belső név: „Fallen Haven II” (az INI szekciója), az osztálynevek `CFH2...` előtaggal.
  - **~790 RTTI osztálynév** maradt benne (pl. `CFH2ClientApp`, `CFH2View`, `CInterface`, `CMenuInterface`, `CNetMsgHandler_template<...>`, `CStratPhaseMsg`, `CTacticPhaseMsg`, `CArmyPhaseMsg`, `CAnimPhaseMsg`, `CLoadScenarioServerMsg`, `CCampaignSaveMsg`, `CCheatUpgradeAPMsg`). Ebből következik az architektúra: **kliens–szerver felépítés üzenetkezeléssel még egyjátékos módban is**, fázisokra bontott kör (stratégiai, hadsereg, taktikai, animáció, eredmény).
  - Importok: `DDRAW` (DirectDraw), `WINMM`, `GDI32`, `USER32`, `ADVAPI32`, `OLEAUT32` (DAO-hoz), `mss32` (Miles hang), `smackw32` (Smacker videó), `AL21MFC.DLL`, `ImMultR5.dll` (hálózat), nyelvi DLL.
- Nyelvi DLL: `LIBDAYEN.DLL` (a program `LibDayFr.dll` és `LibDayGr.dll` nevet is ismer). Az INI `Language=` kulcsa választja ki.
- `EXE\ddraw.dll` – a GOG által mellékelt DirectDraw-wrapper (nem az eredeti része).
- `EXE\LIBDAY.INI` – beállítások: `Cheats`, `InWindow`, `Intro`, `NoIsoAnim`, `LogNetwork`, `Resolution` (0 = 640×480, 1 = 1024×768), felhasználói opciók (`ScrollSpeed`, `Overwatch`, `Autofire`, `ShowMoveAnim`), `[Installation]` útvonalak.
- `DAO\` – DAO 3.5 / Jet 3.5 DLL-ek; a GOG `_install.cmd` regisztrálja őket.

**Adatfájlok (megfigyelt fejlécek):**

| Mappa / fájl | Formátum | Megjegyzés |
|---|---|---|
| `GLOBALS\GLOBAL.MDB`, `GAMEINFO.MDB` | Access/Jet 3.x (`Standard Jet DB`) | globális játékadatok (egységek, épületek stb. – feltérképezendő) |
| `SCENS\SKIRMISH.MDB`, `DEATHM.MDB` | Jet | szkirmis / deathmatch beállítások |
| `SCENS\SCEN1.ZIP` (55), `SCEN2.ZIP` (51), `DEATHM.ZIP` (10) | ZIP, Deflate | benne pályánként egy `.mdb` (pl. `s1-1-1.mdb`, `dmc3-6.mdb`) |
| `COLONIES\COLONIES.ZIP` | ZIP | `C1..C4.mdb` – kolóniák |
| `CACHE\` | Jet | a játék által kibontott/futásidejű MDB-k |
| `IMGS\*.DBI` | saját: magic `dbi\0`, utána u32 = 3 (verzió?), u32 = darabszám?, u32 = 0x36/0x26 | grafika: `TERRAINS`, `UB` (34 MB, valószínűleg egységek/épületek), `ROADS`, `WALLS`, `RUINS`, `MINES`, `SYMBOLS`, `LISTBOX` |
| `ANMSUNIT\ANMSUNIT.FF` | saját: magic `MFF\0`, ~110 MB, belső útvonalnevek (`BDN\P3\B...`) | egységanimációk konténere |
| `INTERF\PAL.BMP` | 8 bites BMP, csak paletta | fő 256 színű paletta |
| `INTERF\PALCYCLE.PAL` | saját, 864 bájt | palettaciklus (víz/fény animáció?) |
| `INTERF\*.SMK`, `VIDEO\*.SMK`, `COLANMS\*.SMK`, `ENCYCL\ENCYCL.SMK` | Smacker 2 (`SMK2`) | felület-háttér (640×480, 1024×768), átvezetők (640×400), kolónia-animációk, enciklopédia |
| `SOUNDS\*.WAV` | WAV | felület- és játékhangok |
| `EXE\MUSIC\Track02..10.ogg` | Ogg Vorbis | zene (GOG pótolta a CD audiót) |
| `SAVEGAME\savegame.NN` | saját | mentések – feltérképezendő |

A „?”-lel jelölt mezőértelmezések feltételezések; ellenőrzésük az első RE-feladatok része.

## Visszafejtési eszközök (telepítve, 2026-10-22)

- **Ghidra 12.1.4:** `C:\tools\ghidra_12.1.4_PUBLIC`
- **JDK 21 (Temurin):** `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot`
- **REA** (`rea-agents` 6.7.0, npm global, https://github.com/morluto/rea): MCP-szerverként a Claude Code-ba regisztrálva, Ghidra providerrel, plusz a `reverse-engineer-anything` skill.
  - Felhasználói környezeti változók: `JAVA_HOME`, `GHIDRA_INSTALL_DIR`, `REA_ANALYSIS_PROVIDER=ghidra`.
  - Ellenőrzés: `rea doctor --provider ghidra --json`.
  - Kipróbálva: a 32 bites `LIBDAY.EXE` elemezhető (`rea inspect ... --provider ghidra`).
  - A Windowsos Ghidra-támogatás a REA-ban kísérleti; ha valami nem megy, a Ghidra GUI a tartalék.
- A REA kimenete (pszeudokód) **csak** a `docs/` leírások forrása, lásd a tiszta szoba szabályt.

## Tervezett architektúra (vázlat, a nyelvválasztás után pontosítandó)

- `tools/` – formátum-kibontók, viewerek (DBI, FF, PAL), MDB → SQLite/JSON export.
- `engine/` – platformréteg (SDL: ablak, input, 8 bites paletta-render, hang, Smacker-lejátszás).
- `game/` – játéklogika a dokumentált mechanikák alapján; az eredeti kliens–szerver üzenetes felépítését érdemes követni (a mentések és a hálózati játék miatt is).
- `docs/formats/`, `docs/mechanics/` – a tiszta szoba dokumentáció.
- Ütemezés: 1) adatformátumok és viewerek → 2) stratégiai térkép megjelenítése → 3) taktikai csata megjelenítése → 4) szabályok, MI → 5) felület, mentés → 6) hálózat.

## Kommunikáció

A felhasználó magyarul kommunikál; válaszolj magyarul. A kód, a kódkommentek és a commitüzenetek angolul készülnek (nyilvános GitHub-projekt); a `docs/` alatti formátumleírások szintén angolul.
