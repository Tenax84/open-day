# FF — unit animation archive (MFF)

`ANMSUNIT\ANMSUNIT.FF` (about 110 MB) holds every unit animation, the building
destruction animations and a set of larger "auto unit" clips. It is the only
`.FF` file in the installation.

The archive is a plain container: a directory of named entries followed by
the entry data. **Every entry is a complete, unmodified Smacker 2 video file**
(`SMK2`). The game opens an entry by looking up its name and handing a file
handle positioned at the entry's offset to the Smacker library, so the
embedded files are exactly what a standalone `.SMK` file would contain.

All integers are little-endian. Offsets are in bytes.

Status: the layout below covers all **5146 entries** of the shipped file.
Every entry decodes completely: 73 836 frames and all audio, and the video
bitstream of every frame is used up to its last 0–3 padding bytes. The
results look correct visually. Fields marked *(?)* are interpretations that
are not yet confirmed.

## File layout

| Offset | Size | Content |
|---|---|---|
| `0x00` | 8 | Header |
| `0x08` | `count × 264` | Directory |
| `0x08 + count × 264` | rest of file | Entry data, one Smacker file after another |

In the shipped file the entry data is contiguous, with no gaps and no padding.
The first entry starts right after the directory, and the last entry ends
exactly at the end of the file.

## Header (8 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `0x00` | char[4] | magic | `"MFF\0"` |
| `0x04` | u32 | count | number of directory entries (`5146`) |

## Directory entry (264 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0x000` | char[256] | name | internal path, NUL-terminated. All bytes after the terminator are zero. |
| `+0x100` | u32 | offset | absolute file offset of the entry's data |
| `+0x104` | u32 | reserved | always `0` |

**There is no size field.** An entry runs from its offset up to the next
higher offset of any entry, and the last one runs to the end of the file. In
the shipped file this derived size always equals the size computed from the
embedded Smacker header (header + frame tables + trees + frame sizes).

The **directory order is not sorted** and does not match the data order
(*?*: possibly a dump of a hash table, as in the DBI name index). The data
itself is stored roughly in alphabetical name order. A reader should build its
own name lookup. Names are unique, upper case, and use `\` as separator. The
game appends `.smk` to an extension-less name before the lookup, so the
lookup is presumably case-insensitive *(?)*.

## Internal path naming

All names end in `.SMK`. There are three groups.

### Unit animations (5009 entries)

Pattern: `UNIT\Pn\UNITnAd.SMK`, e.g. `BDN\P3\BDN3I0.SMK`.

| Part | Meaning |
|---|---|
| `UNIT` | 3–4 letter unit code, repeated in the folder and the file name. The same codes appear in `IMGS\LISTBOX.DBI` (e.g. `BDN999`, `HART999`). |
| first letter of `UNIT` | `H` and `T` presumably Human and Tauran units *(?)*. `B` and `M` codes also occur (*?*: other or shared unit sets). |
| `Pn` / `n` | color variant `0`–`4`, repeated in the folder and the file name. Most `B`, `M`, `T` units have `P0`–`P4`. `H` units have only `P1`–`P4`. Some units have only `P0`. *(?)*: `P1`–`P4` are player colors, `P0` is neutral. |
| `A` | action letter, see below |
| `d` | facing, `0`–`7`, for actions that depend on direction. Actions with a single animation use `0`. Which number is which compass direction is still to be documented. |

Action letters, from the counts per unit and from looking at the frames:

| Letter | Directions | Seen as | Notes |
|---|---|---|---|
| `M` | 8 | movement / idle loop | 28 frames for vehicles, usually with a ring frame (looping) |
| `T` | 8 | firing | the first frames show a muzzle flash |
| `E` | 1 | destruction (explosion) | |
| `I` | 1 | small impact / hit *(?)* | |
| `P` | 1 | *(?)* | often (almost) empty, 1 frame |
| `S`, `U` | 8 | *(?)* | always occur together. Infantry and some vehicles. |
| `L`, `N` | 8 | *(?)*: take-off and landing | always occur together. Flying or diving units: jetpacks, submarines, drones. |

Exception: `TBP\P0\TBPE.SMK` has no direction digit.

### Buildings — `BUILDING\CODE.SMK` (74 entries)

One 26-frame animation per building, e.g. `BUILDING\HBAR.SMK`. It shows an
explosion, presumably building destruction *(?)*. Size 420–432 × 312–428.

### Large clips — `AUTOUNIT\CODEnn.SMK` (61 entries), `MINE\*.SMK` (2)

`AUTOUNIT` clips are 900 × 788, 960 × 848 or 1024 × 768 pixels, with 11–131
frames, in variants `01`–`03` (some also `11`–`13`). They show for example an
aircraft dropping something onto the map (*?*: air strikes / air-delivered
units). `MINE\MINE.SMK` and `MINE\GMINE.SMK` are 216 × 224, 9 frames.

## Observed Smacker properties

| Property | Values in the shipped file |
|---|---|
| Signature | always `SMK2` |
| Frame size | 216 × 224 for all unit animations (4888 entries). 432 × 448 (104), 208 × 164 (16) and the building/clip sizes above. |
| Frame rate | always 15 fps (header value `-6666`) |
| Flags | `0` or `1` (ring frame) only. No interlaced or doubled video. 1488 entries have a ring frame. |
| Palette | only in frame 0 |
| Audio | track 0 only, in 3552 entries: 22 050 Hz, 8-bit, mono, compressed. In 1959 entries all audio is in frame 0, the others spread it over several frames. |
| Frames | 1–131 per entry, most often 28, 7, 4, 12 |

**Palette.** 5141 entries carry the same palette. The rest differ from it in
1–4 entries. It matches `INTERF\PAL.BMP` within ±4 per component in 252 of 256
entries (exceptions: 50, 148, 235, 236). Smacker stores 6-bit components, so
exact equality is not expected.

**Transparency.** The frames have a solid background color. The game draws
unit frames with a color-keyed blit whose key is **the palette index of the
top-left pixel of the current frame**. All pixels with that index are
transparent, including enclosed holes inside the sprite. The background index
varies between entries: most often 198 (very dark red), 245 (pink), 148
(grey) or 83 (brown). It is constant within an entry except in 156 frames,
mostly in `AUTOUNIT` clips. The opaque content is small and roughly centered
in the 216 × 224 frame, typically within about x 80–135, y 70–130.

## Smacker 2 format (as used here)

Smacker is a publicly documented third-party format. This summary is the
subset that OpenDay's decoder (`engine/formats/smk.cpp`) implements. All
fields are little-endian.

### Header (0x68 bytes)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `0x00` | char[4] | signature | `SMK2` (`SMK4` uses a different full-block coding, not supported) |
| `0x04` | u32 | width | |
| `0x08` | u32 | height | |
| `0x0C` | u32 | frames | not counting the ring frame |
| `0x10` | i32 | frame rate | `> 0`: milliseconds per frame; `< 0`: units of 10 µs (`-6666` = 66.66 ms); `0`: 10 fps |
| `0x14` | u32 | flags | bit 0: one extra "ring" frame follows, which turns the last frame back into the first, for looping; bit 1: interlaced; bit 2: doubled |
| `0x18` | u32[7] | audio sizes | per track, the largest decoded audio chunk *(?)*. Here it equals the decoded size when all audio is in one frame. |
| `0x34` | u32 | trees size | byte length of the Huffman table block |
| `0x38` | u32[4] | table sizes | memory hints for the four tables (not needed for decoding) |
| `0x48` | u32[7] | audio rate/flags | bits 0–23 sample rate; bit 31 compressed; bit 30 track present; bit 29 16-bit; bit 28 stereo; bits 26–27 codec (0 = the DPCM scheme below) |
| `0x64` | u32 | reserved | `0` |

After the header: `frames (+1 with ring frame)` u32 frame sizes, then the
same number of u8 frame types, then the table block, then the frames back
to back. In a frame size, the low two bits are flags (bit 0: key frame) and
must be masked off. Frame type bit 0 means "palette chunk present", and
bits 1–7 mean "audio chunk for track 0–6 present".

### Bitstreams and Huffman tables

All bitstreams are read starting with the **least significant bit** of each
byte. Multi-bit values are read least significant bit first.

The table block holds four 16-bit tables in this order: **MMap** (two-color
block masks), **MClr** (two-color block colors), **Full** (pixel pairs) and
**Type** (block types). Each table is coded as follows:

1. 1 bit: table present. If `0`, the table always yields `0` and nothing more
   is stored.
2. A **low-byte tree** and a **high-byte tree**, each coded as 1 bit
   "present". If present, the tree follows, then one extra bit that is ignored.
   An 8-bit tree is coded recursively: bit `1` = inner node, followed by its
   `0` subtree and then its `1` subtree. Bit `0` = leaf, followed by its 8-bit
   value. An absent tree yields `0` without reading bits.
3. Three 16-bit **escape values**.
4. The **main tree**, coded like the byte trees, except that each leaf value is
   read as one code from the low-byte tree and then one from the high-byte
   tree (low | high << 8).
5. One extra bit that is ignored.

When a leaf value equals escape value *i* (checked in order 0, 1, 2), that
leaf becomes **recent-value slot i**. When an escape value does not occur in
the tree, its slot exists but is unreachable. At the start of every frame,
all three slots of every table are set to `0`. Whenever a value *v* is read
from the table (from any leaf, including a slot), and *v* differs from the
current content of slot 0, the slots shift: slot 2 ← slot 1, slot 1 ← slot 0,
slot 0 ← *v*. So reading a slot leaf repeats one of the last three distinct
values.

Decoding a value from a tree: start at the root, read one bit per inner node
(`0` = first subtree, `1` = second), and stop at a leaf. A tree whose root is
a leaf reads no bits.

### Frame

A frame consists of these parts, in order:

1. **Palette chunk** (if frame type bit 0): the first byte × 4 is the
   chunk length including that byte. The new palette starts as a copy of the
   previous one (all black before the first). Then, until 256 entries are
   done or the chunk ends:
   - `1nnnnnnn`: keep the next `n+1` entries unchanged.
   - `01nnnnnn s`: copy `n+1` entries from the **previous** palette,
     starting at index `s`, to the current position.
   - `00rrrrrr g b`: one literal color. Each component is 6 bits and is
     widened to 8 bits as `(c << 2) | (c >> 4)`.
2. **Audio chunks** for each flagged track: u32 length (including itself),
   then the data.
3. **Video**: the rest of the frame, one bitstream.

### Video

The image is covered by 4 × 4 blocks in raster order. The decoder keeps the
previous frame's pixels. Repeat until all blocks are done: read a value *t*
from the Type table. Bits 2–7 select a run length: indices 0–58 mean 1–59,
and 59–63 mean 128, 256, 512, 1024, 2048. Apply the block kind `t & 3` to
that many blocks (stop at the last block):

| Kind | Name | Per block |
|---|---|---|
| 0 | two-color | read a color pair `c` from MClr (low byte = color 0, high byte = color 1), then a 16-bit mask from MMap. Row by row from the top, 4 bits per row starting with the lowest bit, left to right: bit set = color 1. |
| 1 | full | for each of the 4 rows: one Full value gives pixels 2 and 3 (low, high byte), the next gives pixels 0 and 1. |
| 2 | skip | keep the previous frame's pixels |
| 3 | solid | fill with color `t >> 8` |

### Audio (compressed DPCM)

An audio chunk holds a u32 decoded byte count, then a bitstream: 1 bit
"data present", 1 bit stereo, 1 bit 16-bit. Then 1, 2 or 4 byte trees
(coded as in the table block, each with its "present" bit and trailing bit):
one per channel for 8-bit audio, or a low-byte and a high-byte tree per
channel for 16-bit audio. Then the first sample of each channel, last channel
first (8 bits each, or 16 bits with the first 8 bits as the high byte).
Every following sample, with channels interleaved, is the previous sample
of its channel plus a delta read from that channel's tree(s), wrapping
around. 8-bit samples are unsigned, 16-bit samples are signed. Only 8-bit
mono is present in the game data. The stereo and 16-bit paths are
implemented as described but untested on real data. An uncompressed track
(bit 31 clear) stores raw PCM after the chunk length.

## Validation

`fftool check` and the optional `ff_game_files` test decode the whole
archive:

- 5146 / 5146 entries, 73 836 frames (ring frames excluded), no errors.
- At most 31 unread bits at the end of any frame's video data. The video
  stream always ends in the frame's 4-byte padding.
- 128 478 057 audio samples decoded. Where all audio sits in one chunk, the
  decoded length equals the header's audio size. The decoded length also
  matches the animation length, e.g. 20 572 samples = 0.93 s for a 14-frame
  animation.
- Rendered frames show recognizable units, explosions and buildings.

## Writing (for synthetic test files)

`writeFf` stores entries in the given order with consecutive offsets.
`writeSmk` produces valid Smacker 2 files with balanced Huffman trees, every
block coded with run length 1, escape values that never occur (so the
recent-value cache is unused), a full palette chunk whenever the palette
changes, and optionally one compressed 8-bit mono audio chunk in frame 0.
The test suite also contains a hand-made stream that exercises the
recent-value cache.
