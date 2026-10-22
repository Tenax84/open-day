# PALCYCLE.PAL — palette color cycling table

`INTERF\PALCYCLE.PAL` drives the palette animation of the map graphics:
shimmering water and wall force fields, blinking mine lights and pulsing
lights on units and buildings. It is a flat list of "at step *s*, palette
entry *i* gets color *c*" records. It holds **replacement colors**, not index
ranges to rotate: nothing is shifted around inside the palette.

All integers are little-endian. Offsets are in bytes.

Status: the layout is confirmed by the game's loader and by the shipped file
(864 bytes, which is exactly the header plus 172 records). The runtime
behavior below was taken from the game's timer routine. Points marked *(?)*
are not yet confirmed.

## File layout

| Offset | Size | Content |
|---|---|---|
| `0x00` | 4 | `count`: number of records (signed 32-bit; the game reads nothing when it is `<= 0`) |
| `0x04` | `count × 5` | Records |

The game reads exactly `count` records and ignores anything after them. The
shipped file has no trailing bytes.

### Record (5 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0` | u8 | index | palette entry to overwrite |
| `+1` | u8 | red | 0–255 (full 8-bit range, not VGA 6-bit) |
| `+2` | u8 | green | |
| `+3` | u8 | blue | |
| `+4` | u8 | step | animation step at which this color is shown |

Records are grouped by palette index in the shipped file, with steps in
ascending order inside a group, but the game does not depend on any order.

## Runtime behavior

The game keeps **one global step counter** for all cycling entries (they do
not have their own phases or speeds; phase offsets are baked into the
tables).

1. When the cycler is created (at startup, before the file is loaded) it
   copies the current 256-entry hardware palette and keeps that copy as the
   **base palette** for the rest of the session. The step counter starts at 0.
2. It is driven by a Windows timer with a **100 ms** period (the nominal
   period: these timer messages are low-priority and can arrive late). Each
   tick:
   1. If the game window is not the active window, or is minimized, nothing
      happens (the step does not advance).
   2. If the cycler is disabled (see below), nothing happens.
   3. If a "skip one tick" flag is set, the flag is cleared and nothing else
      happens. The flag is set by a routine that re-applies the palette to a
      view *(?)*, presumably so that a freshly set palette is not overwritten
      at once.
   4. Otherwise it starts from a fresh copy of the **base palette**, and for
      every record whose `step` equals the current step it writes the
      record's color to entry `index`. If at least one record matched, the
      resulting palette (all 256 entries) is sent to the display.
   5. The step counter is incremented. If it is now **greater than the
      largest `step` value in the file**, it wraps to 0.

Consequences:

- The cycle length is `max(step) + 1` ticks for every entry. In the shipped
  file `max(step) = 17`, so a full cycle is **18 steps = 1.8 s**.
- Because every tick starts from the base palette, an entry that has no
  record for the current step shows its **base color** during that step (it
  does not keep its last cycled color). Shorter groups therefore have a
  "rest" phase. With the shipped file this applies to entry 237 (steps 12–17)
  and 238 (steps 13–17).
- Entries without any record are never changed.

Enabling: the cycler has an enable counter that starts at −1 (disabled).
Some views increment it when they are activated and decrement it when they
are deactivated; the cycler runs while the counter is `>= 0`. Which screens
enable it exactly is not yet documented *(?)*; it is expected to be the
strategic and tactical map views.

## Shipped file contents (observed)

172 records: 169 real ones for palette entries **235–244** (`0xEB`–`0xF4`),
plus 3 filler records `index 0, color (0,0,0), step 0` at the end. The
filler records set entry 0 to black at step 0, which is harmless because
entry 0 is black in every palette anyway.

| Entries | Steps | Colors | Used by (observed in `IMGS\*.DBI`) |
|---|---|---|---|
| 235, 236 | 18 each | dark to bright red pulse, two phases | lights on units and buildings (`UB`: `HBCP…`, `HBOM…`, …), `TERRAINS` `LEON001–003` |
| 237 | 12 (then base color) | black → pale yellow → black blink | mines (`MINE000/001`), some units |
| 238 | 13 (then base color) | orange → black → orange | `GMINE000/001`, some units |
| 239, 240, 241 | 18 each | light blue ↔ dark blue, three phases | water: shore tiles (`TERRAINS` `BORE…`), bridges/roads (`ROADS` `ROAH…`), force-field walls (`WALLS` `WALLF…`), ports (`UB` `BPRT…`) |
| 242, 243, 244 | 18 each | dark red ↔ bright red, three phases | laser walls (`WALLS` `WALLL…`), some units (`MAIR…`, `TPRT…`) |

In `INTERF\PAL.BMP` entries 235–244 are all black. The palettes embedded in
`MINES`, `ROADS`, `TERRAINS`, `UB` and `WALLS` (see [DBI.md](DBI.md)) hold
placeholder colors there instead (magenta, yellow, cyan, brown), which are
presumably the editor's markers for "this pixel cycles" *(?)*. The cycled
colors replace them on screen, so the base color mostly matters for the rest
phases of entries 237 and 238. Which palette is the actual hardware palette at
the time the cycler takes its base copy is not yet confirmed *(?)*; an
implementation should use the palette it is drawing with as the base.

One record looks like a typo in the data: entry 237, step 10 is
`(117, 85, 34)` where the symmetric sequence suggests `(85, 85, 34)`. The
game shows it as stored.

## Implementation notes

- Reimplementations should step on a fixed 100 ms clock (step =
  `elapsed_ms / 100 mod (max(step) + 1)`) rather than per rendered frame.
- Pausing while the window is inactive is a detail of the original that an
  implementation may skip.
