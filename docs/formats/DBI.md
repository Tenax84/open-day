# DBI — indexed sprite archive

`IMGS\*.DBI` files are archives of 8-bit palettized sprites: isometric terrain
tiles, roads, walls, ruins, map symbols, units/buildings (`UB.DBI`) and
list-box icons. Each file holds a palette, a fixed number of equally sized
slots (one sprite per slot) and a name → slot index at the end.

All integers are little-endian. Offsets are in bytes.

Status: the layout below decodes **all 5976 sprites** of the 8 files shipped
with the game without errors, and the results look correct visually.
Fields marked *(?)* are interpretations that are not yet confirmed.

## File layout

| Offset | Size | Content |
|---|---|---|
| `0x000` | 32 | File header |
| `0x020` | 1024 | Palette, 256 × 4 bytes |
| `0x420` | `count × 8192` | Sprite slots |
| `0x420 + count × 8192` | `count × 12` | Name index (runs to end of file) |

The file size is always exactly `0x420 + count × 8204`.

## File header (32 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `0x00` | char[4] | magic | `"dbi\0"` |
| `0x04` | u32 | version | always `3` |
| `0x08` | u32 | count | number of slots, and number of index entries |
| `0x0C` | u32 | width | nominal sprite width (*?*: possibly a maximum); `54` for map graphics, `38` for `LISTBOX` |
| `0x10` | u32 | height | nominal sprite height; `56` for map graphics, `38` for `LISTBOX` |
| `0x14` | u32 | transparent *(?)* | `1` if the sprites use transparent runs, `0` for `LISTBOX`, whose sprites are fully opaque |
| `0x18` | u32[2] | reserved | always `0` |

In all shipped files every sprite has exactly the header's width and height.

## Palette (1024 bytes)

256 entries of 4 bytes each, in the order **blue, green, red, reserved**
(the Windows `RGBQUAD` layout). The reserved byte is always `0`.

Entries 0–9 and 246–255 hold the standard Windows static system colors. The
rest matches the main game palette `INTERF\PAL.BMP` (the palettes are
identical, or differ in at most 11 entries, depending on the file). Sprite
pixel values are indices into this palette. Index `0` (black) is an ordinary
opaque color. Transparency comes from run types (see below), not from a
color key.

## Sprite slots

Every slot is **8192 bytes**, regardless of the sprite's actual size. Slot `i`
starts at `0x420 + i × 8192`. A slot holds a sprite record followed by
unused space. **The unused space is not zeroed.** It holds leftover bytes
from the tool that created the file, sometimes even fragments of records
from other DBI files. A decoder must never read past the record size (see
below).

### Sprite record header (20 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0x00` | char[8] | name | NUL-padded ASCII. A name **may use all 8 bytes, with no terminator**. Bytes after the first NUL are garbage. |
| `+0x08` | u16 | width | pixels |
| `+0x0A` | u16 | height | pixels (number of rows) |
| `+0x0C` | u16 | data size | byte length of the row data that follows (see *Record size quirk*) |
| `+0x0E` | u16 | transparent *(?)* | always equal to the file header's flag at `0x14` |
| `+0x10` | u32 | reserved | always `0` |

### Row data

Starting at `+0x14`, there are `height` row records stored consecutively,
top row first:

| Type | Name | Notes |
|---|---|---|
| u32 | row length | byte length of this row record, **including** these 4 bytes |
| … | runs | runs follow until `row length` bytes are used up |

Each run starts with a 4-byte header:

| Type | Name | Notes |
|---|---|---|
| u16 | n | pixel count |
| u16 | kind | `1` = transparent run, `2` = literal run |

- **kind 1 (transparent):** skip `n` pixels. No payload.
- **kind 2 (literal):** `n` payload bytes follow, one palette index per pixel.

Runs go from left to right. Their pixel counts add up to the sprite width.
Fully opaque files (`LISTBOX`) contain only literal runs, typically one per
row.

### Record size quirk

The *data size* field does not always match the sum of the row lengths:

- When the last run of the last row is a **literal** run, or is a transparent
  run that fits, *data size* is the sum of the row lengths **plus 1**. The
  extra byte is garbage. Exception: in `LISTBOX` it is exactly the sum.
- When the sprite **ends with a transparent run**, the last run is often
  truncated. *data size* is then the sum of the row lengths **minus 3**, and
  only the first byte of the final run header is inside the record. The bytes
  where the rest of that header would be are slot garbage. They may read as kind `0`, kind `2` or arbitrary
  values.

Decoding rule that handles every shipped file:

1. Let `end = record_start + 0x14 + data_size`.
2. Walk each row by its *row length*.
3. If a run header would extend past `end`, stop decoding the row. The
   remaining pixels of the row are transparent. (This only happens in the
   last row.)
4. A literal run whose payload would extend past `end` means the file is
   corrupt.

## Name index

The file ends with `count` entries of 12 bytes each:

| Type | Name | Notes |
|---|---|---|
| char[8] | name | same encoding as the record name. Garbage after the first NUL. |
| u32 | slot | slot number, `0 … count-1` |

Each slot appears exactly once. Compare names up to the first NUL, or up to 8
bytes if there is none. The entries are **not sorted** in any obvious order,
which suggests a dump of a hash table *(?)*. A reader should build its own
lookup map. Names are unique within a file.

## Naming conventions (observed, incomplete)

- `LISTBOX`: four-letter code plus `999`, e.g. `BDN999`, `HART999`.
- `TERRAINS`: terrain family plus number, e.g. `ARID342`.
- `ROADS`: `ROA…` / `NORD…` prefixes. `MINES`: `MINE000`, `GMINE000`, …
- `UB`: unit and building sprites, e.g. `BDN0600`. Vehicles appear in groups
  of 8 facing directions × 4 faction color variants.

What the name parts mean (faction, direction, animation frame) still needs to
be documented together with the game database (`GLOBAL.MDB`), which
presumably refers to these names.

## Writing (for synthetic test files)

A minimal valid file: header with `count = N`, any palette, `N` slots of 8192
bytes (record plus zero padding), and `N` index entries. Writers should set
*data size* to the exact sum of row lengths. Readers following the rule above
accept that as well.
