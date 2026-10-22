# MDB — game databases (Jet 3 / Access 97)

Almost all game rules and scenario content are stored in Microsoft Jet
databases (`.mdb`). This page describes **which databases exist**, the
container format, its quirks, and **how OpenDay reads them**. The meaning of
the tables and columns is in [`../mechanics/data-model.md`](../mechanics/data-model.md).

Status: every database of the GOG release (122 files) was read completely
and the results were cross-checked between three independent readers (see
*Readers* below). Statements marked *(?)* are interpretations.

## Databases in an installation

| Location | Files | Role |
|---|---|---|
| `GLOBALS\GLOBAL.MDB` | 1 | Static rules: unit, building, automated-unit, mine, road, wall, terrain and ruin types; lookup (enum) tables; AI priorities; colors; difficulty. 34 tables. |
| `GLOBALS\GAMEINFO.MDB` | 1 | Template of the *runtime game state* (campaign progress, per-player statistics, strategic/tactical turn state, user list). 17 tables, nearly empty. *(?)* copied and filled in by the game at run time. |
| `SCENS\SKIRMISH.MDB` | 1 | Catalog of the scenarios offered in skirmish mode (one table, one row per scenario file). |
| `SCENS\DEATHM.MDB` | 1 | The same catalog for death-match scenarios. |
| `SCENS\SCEN1.ZIP` | 54 `.mdb` | Campaign tactical scenarios, variant 1 (`s<colony>-<province>-1.mdb`). |
| `SCENS\SCEN2.ZIP` | 50 `.mdb` | Campaign tactical scenarios, variant 2 (`s<colony>-<province>-2.mdb`). |
| `SCENS\DEATHM.ZIP` | 10 `.mdb` | Death-match scenarios (`dm<kind><n>-<m>.mdb`). |
| `COLONIES\COLONIES.ZIP` | 4 `.mdb` | Strategic layer: one database per colony/continent (`C1`…`C4`): provinces, adjacency, available buildings. |
| `CACHE\…` | varies | Working copies the game extracts from the ZIP archives at run time *(?)*. Not needed by OpenDay. |

`SCEN1.ZIP` and `SCEN2.ZIP` also contain stray Access lock files (`.ldb`),
which carry no data and must be ignored.

All scenario databases (campaign and death match) share one schema of about
22 tables; colony databases share another schema of 6 tables. See
[`data-model.md`](../mechanics/data-model.md) for both.

## Container format

- **Jet 3** (Access 95/97). Page size **2048 bytes**. The version byte at
  file offset `0x14` is `0`. The magic at offset 4 is the ASCII text
  `Standard Jet DB`.
- **Text encoding: Windows-1252** (the databases were authored on a western
  European system). Jet 3 stores text as single-byte code-page text, not
  Unicode, and the file does not reliably say which code page. Readers must
  be told explicitly. A reader that uses the host system's ANSI code page
  (as the Microsoft driver does on a non-western Windows) corrupts the French
  and German strings.
- **Column types used:** Boolean (Yes/No), Byte, Long Integer (32-bit
  signed; also used for every AutoNumber `Counter`), Double, Text (≤ 255
  bytes), Memo (long text) and OLE Object (long binary). The 16-bit Integer,
  Currency, Single, Date and GUID types do not occur.
- **Database password.** `GLOBAL.MDB`, `GAMEINFO.MDB` and the four colony
  databases have a Jet database password set; the scenario databases do not.
  In Jet 3 the password is only a check made by the Microsoft engine when it
  opens the file. The pages are **not** encrypted. Open-source readers
  (including OpenDay's) ignore it. OpenDay never needs it and does not store it.
- **Linked tables.** Every scenario and colony database, and `GAMEINFO.MDB`, contains
  *linked tables* (catalog object type 6) that point to lookup tables of
  `GLOBAL.MDB`, for example the player, race, unit-type-id and
  building-type-id lookups. The link target is an absolute Windows path on the
  original authors' machine, which does not exist anywhere else. Treat
  these entries as references to the `GLOBAL.MDB` table of the same name.
  They hold no data of their own.
- **Saved queries, forms, reports.** Most databases also contain Access
  editor objects: SELECT queries (sorted views and translation helpers),
  UPDATE queries (for example, setting the owner of mines, roads and walls
  to the AI), and some forms and reports. They look like editing tools
  *(?)*. Whether the game runs any of these queries is not known yet. Converters
  ignore them.
- **Relationships and indexes.** `GLOBAL.MDB` declares a few referential
  relationships (lookup enums → type tables, see data-model.md). Most tables
  have an AutoNumber primary key `Counter` (lookup tables: `Enum`).
  Relationships are informative only: several columns that are clearly
  foreign keys have no declared relationship.

### Quirks a reader must tolerate

- **Table-name case varies** between scenario files (`Ruins On Map` and
  `Ruins On map` both occur). Jet names are case-insensitive. Look tables up
  case-insensitively. SQLite is case-insensitive for table names too.
- Some scenario files have an extra, misspelled copy of a table
  (`On Off Units Available` next to `One Off Units Available`) or an extra
  saved query (`Order Goto`, `CropMap`). Only the common table set matters.
- Some column names are odd but significant: a Boolean column named only with
  dashes (`-------------`) in the unit placement tables, and a column named
  `roadtype` in `Walls On Map` that holds the **wall** type.
- Tables that have no variable-length column store their rows **without**
  the variable-column section (see *Row layout*). A reader that always expects it
  fails on, e.g., `GAMEINFO.MDB`'s `Tactic IO` and some scenario order tables.
- Wide rows (more than 256 bytes, e.g. `Unit Types`) use the Jet 3 *jump
  table* for variable-column offsets. A reader without jump-table support
  silently loses text values (observed with `access-parser`, see below).

## Readers (evaluated October 2026)

| Reader | Platform | Result |
|---|---|---|
| **OpenDay `tools/mdb/mdb2sqlite.py`** (own Jet 3 reader, Python stdlib only) | macOS, Linux, Windows | **Works.** Reads all 122 databases. Row counts and values are identical to Jackcess for every table and column (OLE values compared by length). Spot-checked OLE blobs are byte-identical to the Microsoft driver's. Converts a whole install in ~10 s. **Recommended.** |
| Jackcess 4.0.5 (Java, Apache-2.0) | any JVM | Reads all data correctly **if** the charset is set to Windows-1252. Opening tables by name fails for about half of the `GLOBAL.MDB` tables (its catalog-index lookup does not match these Jet 3 files). Opening by table-definition page works. It tries to resolve the dangling linked-table path when listing relationships. Used here as the reference for validation. |
| Microsoft Jet OLE DB 4.0 / "Microsoft Access Driver (*.mdb)" ODBC | Windows only, **32-bit only** on this system | Works, but needs the database password for the protected files and decodes text with the system ANSI code page (wrong on a Hungarian system). Not portable. Used here only as ground truth for schema, row counts and blobs. |
| `access-parser` 0.0.6 (pure Python, PyPI) | any | **Not usable.** Catalog parse errors, and it returns empty or misplaced text values in wide rows (no Jet 3 jump-table support). |
| mdbtools (C, GPL/LGPL; `brew install mdbtools`, `apt install mdbtools`) | macOS, Linux | **Not tested** (no build on the Windows workstation used). Its format documentation covers Jet 3, and it ignores Jet 3 passwords. Text decoding for Jet 3 must be set to CP1252 (`MDB_JET3_CHARSET`). Worth verifying on macOS as a second, independent reader. |

### Recommendation for the engine

The plan in `CLAUDE.md` (one-time MDB → SQLite conversion at install or
first start) works, and needs no third-party code:

1. `tools/mdb/mdb2sqlite.py` reads the user's installation
   (`--game-dir` or `OPENDAY_GAME_DIR`), including the `.mdb` members of the
   ZIP archives (read in memory, never extracted into the game folder), and
   writes one SQLite file per database into a user-chosen directory outside
   the repository. Each `.sqlite` holds the user tables with their
   original names, plus `_mdb_columns` (original Jet types) and `_mdb_links`
   (linked-table names).
2. The engine reads only SQLite (the `sqlite3` amalgamation is public domain
   and builds on every target).
3. Later, the same Jet 3 subset (≈ 300 lines) can be ported to C++ inside
   the engine if a Python step at install time is unwanted. The layout notes
   below are enough for that.

## Jet 3 layout summary (the subset OpenDay reads)

Public knowledge of the Jet format (the mdbtools project's format notes
describe it in detail), condensed to what these files need. All integers
are little-endian.

### Pages

Page *n* starts at `n × 2048`. Byte 0 is the page type: `0x01` data,
`0x02` table definition (TDEF), `0x03`/`0x04` index, `0x05` usage bitmap.
Page 0 is the database header. Page 2 is the TDEF of the system catalog
`MSysObjects`.

### Catalog (`MSysObjects`)

Relevant columns: `Id` (low 24 bits = TDEF page of a table), `ParentId`
(`0x0F000001` for objects in the "Tables" container), `Name`, `Type`
(`1` local table, `6` linked table, `5` query, other values: Access objects),
`Flags` (bits `0x80000002` mark system/hidden tables), `Database` and
`ForeignName` (link target for type 6).

### Table definition

A TDEF can span pages: the u32 at `+4` is the next TDEF page. Continuation
pages contribute their bytes after an 8-byte header.

| Offset | Type | Meaning |
|---|---|---|
| `0x0C` | u32 | row count |
| `0x17` | u16 | number of variable-length columns |
| `0x19` | u16 | number of columns |
| `0x1F` | u32 | number of real indexes |
| `0x2B` | — | 8 bytes per real index, then the column records |

Column record (18 bytes each): `+0` u8 type, `+1` u16 column number,
`+3` u16 index among variable columns, `+13` u8 flags (`0x01` fixed length,
`0x02` nullable, `0x04` AutoNumber), `+14` u16 offset in the fixed area,
`+16` u16 length. The column records are followed by the names: u8 length + bytes
(code-page text). Index definitions follow (not needed for reading data).

Type codes: `1` Boolean, `2` Byte, `3` Integer (16), `4` Long (32),
`5` Currency, `6` Single, `7` Double, `8` Date, `9` Binary, `10` Text,
`11` OLE, `12` Memo, `15` GUID, `16` Numeric.

### Data pages and rows

Data page: `+4` u32 owning TDEF page, `+8` u16 row count, `+10` u16 row
offset per row. Offset flag `0x8000` = deleted, `0x4000` = the row only
holds a 4-byte pointer (`row | page << 8`) to the real row elsewhere. Low 13
bits = start within the page. Rows grow downward from the page end: row
*i* ends where row *i − 1* starts (row 0 ends at the page end).

Every data page names its owning table, so a reader can find a table's rows
by scanning all data pages for that owner, without decoding the usage
bitmaps. Rows that are the target of an overflow pointer must not be
counted twice.

Row layout:

1. u8 number of columns in this row (older rows may have fewer than the
   current table definition; missing columns are NULL).
2. Fixed-length column data, at `1 + fixed offset`.
3. Variable-length column data.
4. **Only if the table has variable columns:** the variable-column offset
   table: one byte per variable column plus one end offset, stored
   **backwards** from the end; before it, a *jump table* with one entry per
   256 bytes of row length. Every entry is the index of the first variable
   column whose offset must be increased by another 256. The u8 count of
   variable columns in this row sits just before the null mask.
5. Null mask, `ceil(columns / 8)` bytes, at the very end. Bit set = value
   present. For Boolean columns the bit **is** the value. Booleans take no
   space in the fixed area.

### Memo and OLE values

The variable-column data is a 12-byte descriptor: u32 length whose top two
bits select storage (`0x80000000` value inline after the descriptor,
`0x40000000` value is one row at pointer `+4`, `0` chain of rows where each
row starts with the u32 pointer to the next), u32 pointer (`row | page << 8`),
u32 unused.

OLE columns in these files hold plain Windows BMP files (8-bit, 256-color),
used as overview-map images. See data-model.md.
