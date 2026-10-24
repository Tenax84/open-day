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

- Git-repó inicializálva; GitHub: **`Tenax84/open-day`** (privát, később nyilvános). A `gh` CLI `Tenax84` fiókkal van bejelentkezve.
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
- Nyitott: aknák 000/001 utótagja, játékos-slot → szín tábla (az animációk P0–P4 változatánál is), tornyok iránya, a mozgó egység takarási táblái; telepítés, vásárolt erősítés, ellentámadás, automata egységek (légicsapás, ledobás, AA), eredmény-statisztika/pontszám, tech-épületek, mentés; az AI útkeresési ablaka; sérült épület-sprite, aknarobbanás-animáció; a `Jammer` valódi jelentése.
- Következő jelöltek: a felület (INTERF SMK hátterek, menük, taktikai HUD, eligazítás/kiértékelés), vagy a telepítés + automata egységek, vagy a stratégiai térkép (kolóniák, provinciák).

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
