# Inno Setup installers (GOG release)

The GOG release of the game is an Inno Setup installer
(`setup_*.exe`, sometimes with `setup_*-1.bin`, `-2.bin`, ... next to it).
OpenDay reads the game files out of it without running it
(`engine/formats/inno_setup.*`, `engine/data/game_source.*`).

This is a third-party container format, not part of the original game.
Sources: the published Inno Setup source code (`Shared.Struct.pas` at the
`is-6_4_3` and earlier tags of github.com/jrsoftware/issrc) and the
documentation and version tables of innoextract (zlib license); checked
against installers built with Inno Setup 5.5.9 (ANSI and Unicode), 5.6.1,
6.0.5, 6.2.2, 6.3.3 and 6.4.3 (byte-identical output to innoextract where it
supports the version; every file's stored checksum verified).

All integers are little-endian. "Version" below means the *setup data
version* (from the data id), which can be older than the Inno Setup release.

## 1. Finding the setup data

The setup program is a PE executable with a small loader. An **offset
table** says where the setup data lives:

- Loaders before 5.1.5: at file offset 0x30 the bytes `Inno`, then u32 *T*
  and u32 `~T`; the table is at offset *T*.
- 5.1.5 and later: the table is the resource `RT_RCDATA` (type 10) with id
  11111.

The table starts with a 12-byte magic that also tells its layout:

| Magic | Loader | Fields after the magic |
|---|---|---|
| `rDlPtS02` 87 `eVx` | 1.2.10 | (not supported) |
| `rDlPtS04` 87 `eVx` | 4.0.0 | (not supported) |
| `rDlPtS05` 87 `eVx` | 4.0.3 | total size, exe offset, exe compressed size, exe size, exe CRC, **header**, **data** |
| `rDlPtS06` 87 `eVx` | 4.0.10 | same, then a CRC of the table |
| `rDlPtS07` 87 `eVx` | 4.1.6 | total size, exe offset, exe size, exe CRC, **header**, **data**, table CRC |
| `rDlPtS` CD E6 D7 `{` 0B `*` or `nS5W7dT` 83 AA 1B 0F `j` | 5.1.5 | u32 revision, then by revision |

Revision 1: u32 total size, exe offset, exe size, exe CRC, **header**,
**data**, table CRC. Revision 2 (Inno Setup 6.5 and later): u64 total
size, u64 exe offset, u32 exe size, u32 exe CRC, u64 **header**, u64
**data**, u32 padding, u32 table CRC.

**header** is the file offset of the setup headers ("setup-0"). **data** is
the offset of the embedded file data ("setup-1"); 0 means the data is in
external slice files (section 5).

## 2. Data id and header blocks

At *header*: a 64-byte, zero-padded ASCII id such as
`Inno Setup Setup Data (5.5.7) (u)`. The version is the largest
`(a.b.c[.d])` group (old ids may contain two, "… with ISX (x)"). `(u)` or
`(U)` marks a Unicode build; from 6.3.0 every build is Unicode.

Two **blocks** follow. Each block:

| Bytes | Content |
|---|---|
| 4 | CRC-32 of the next 5 bytes |
| 4 | stored size *S* (including the CRCs below) |
| 1 | 1 = compressed |
| *S* | sub-blocks: u32 CRC-32 of the data, then up to 4096 data bytes |

The data of the sub-blocks, joined, is (if compressed) an LZMA stream in
the form of section 6 (5 property bytes, no size). Block 1 holds the
setup header and all entry lists; block 2 holds the file locations. Each
block ends exactly where its last entry ends.

Some setup data ids were used for more than one layout. The reader tries
the alternatives in order and keeps the first that parses to the exact end
of both blocks: 5.3.10 → 5.3.10.1; 5.4.2 → 5.4.2.1; 5.5.0 → 5.5.0.1;
5.5.7 → 5.5.7.1 → 5.6.0 (the ".1" variants are modified builds).

### Field types

- **String**: u32 byte length, then the bytes. In Unicode builds UTF-16LE;
  otherwise the system code page (read as Windows-1252). Some fields are
  always 8-bit ("AnsiString"); their storage is the same.
- **Enum**: one byte; a value beyond the known count means the layout
  guess is wrong.
- **Set** of *n* flags: ⌈*n*/8⌉ bytes, bit *i* = flag *i* (in the order
  listed); a 3-byte set is padded to 4 bytes.
- **WinVer**: 20 bytes (minimum and "only below" Windows versions).
- **Condition**: six strings — components, tasks, languages, check, after
  install, before install.

## 3. Block 1

### 3.1 Setup header (version ≥ 5.0.0)

Strings, in order (conditions in brackets):

1. app name, versioned name, app id, copyright, publisher, publisher URL,
   [≥ 5.1.13] support phone, support URL, updates URL, app version,
   default dir name, default group name, base file name
2. [< 5.2.5] license, info before, info after; [< 5.2.5 after the next
   seven] compiled code
3. uninstall files dir, uninstall name, uninstall icon, app mutex, default
   user name, default organisation, default serial
4. readme file, contact, comments, modify path
5. [≥ 5.3.8] create-uninstall-registry-key; [≥ 5.3.10] uninstallable;
   [≥ 5.5.0] close-applications filter; [≥ 5.5.6] setup mutex;
   [≥ 5.6.1] changes environment, changes associations; [≥ 6.3.0]
   architectures allowed, architectures installed in 64-bit mode;
   [≥ 6.4.2] close-applications filter excludes
6. [≥ 5.2.5] license, info before, info after;
   [5.2.1 ≤ v < 5.3.10] uninstaller signature; [≥ 5.2.5] compiled code

Then:

- [non-Unicode] 32 bytes: lead-byte set.
- 16 × u32 counts: languages, messages, permissions, types, components,
  tasks, directories, files, **file locations**, icons, INI entries,
  registry entries, install deletes, uninstall deletes, runs, uninstall
  runs.
- WinVer; [< 6.4.0.1] 2 × u32 back colours; [< 5.5.7] u32 image back
  colour; [< 5.0.4] u32 small image back colour.
- [≥ 6.0.0] enum wizard style (2), 2 × u32 wizard size percent.
- [≥ 5.5.7] enum image alpha format (3).
- Password check: [≥ 6.4.0] 4 bytes + 44 bytes (key derivation salt,
  iterations, base nonce); [≥ 5.3.9] 20-byte SHA-1 + 8-byte salt; else
  16-byte MD5 + 8-byte salt.
- u64 extra disk space, u32 **slices per disk**.
- enum uninstall log mode (3), enum dir-exists warning (3), enum
  privileges required (4 from 5.3.7, else 3), [≥ 6.0.0] set(2) privilege
  overrides, enum language dialog (3), enum language detection (3).
- enum **compression**: [≥ 5.3.9] stored, zlib, bzip2, LZMA, LZMA2;
  before: stored, zlib, bzip2, LZMA.
- Architectures: [5.1.0 ≤ v < 5.6.0] 2 × set(4); [5.6.0 ≤ v < 6.3.0]
  2 × set(5); none from 6.3.0.
- [5.2.1 ≤ v < 5.3.10] 2 × u32 (signed uninstaller).
- [≥ 5.3.3] enum disable dir page (3), enum disable group page (3).
- Uninstall display size: [≥ 5.5.0] u64, [≥ 5.3.6] u32.
- [5.3.10.1, 5.4.2.1, 5.5.0.1] one extra byte.
- **Options** set. Its members, in order: disable startup prompt,
  [< 5.3.10] uninstallable, create app dir, [< 5.3.3] disable dir page,
  [< 5.3.3] disable program group page, allow no icons, always restart,
  always use personal group, [< 6.4.0.1] window visible / show caption /
  resizable / start maximized, enable dir-doesn't-exist warning,
  **password**, allow root directory, disable finished page, [< 5.6.1]
  changes associations, [< 5.3.8] create uninstall reg key, use previous
  app dir, [< 6.4.0.1] back colour horizontal, use previous group, update
  uninstall log app name, use previous setup type, disable ready memo,
  always show components list, flat components list, show component sizes,
  use previous tasks, disable ready page, always show dir on ready page,
  always show group on ready page, allow UNC path, user info page, use
  previous user info, uninstall restart computer, restart if needed by run,
  show tasks tree lines, allow cancel during install, wizard image stretch,
  append default dir name, append default group name, **encryption used**,
  [5.0.4 ≤ v < 5.6.1] changes environment, [≥ 5.1.7, non-Unicode] show
  undisplayable languages, [≥ 5.1.13] setup logging, [≥ 5.2.1] signed
  uninstaller, [≥ 5.3.8] use previous language, [≥ 5.3.9] disable welcome
  page, [≥ 5.5.0] close applications, restart applications, allow network
  drive, [≥ 5.5.7] force close applications, [≥ 6.0.0] app name has
  constants, use previous privileges, wizard resizable, [≥ 6.3.0]
  uninstall logging.

### 3.2 Entry lists

In this order, each repeated by its count from the header:

| Entry | Layout |
|---|---|
| Language | 2 strings (name, language name), [5.5.7.1] string, 4 font names, data, license, info before, info after; u32 language id; [non-Unicode] u32 code page, [Unicode < 5.3.0] u32; 4 × u32 font sizes; [5.5.7.1] u32; [≥ 5.2.3] u8 right-to-left |
| Message | 2 strings, i32 language |
| Permission | 1 string |
| Type | 4 strings, WinVer, set(1), enum(4), u64 size |
| Component | 5 strings, u64, i32 level, u8 used, WinVer, set(5), u64 size |
| Task | 6 strings, i32 level, u8 used, WinVer, set(5) |
| Directory | string, condition, u32 attributes, WinVer, u16 permission, set(5 from 5.2.0, else 3) |
| **File** | source, **destination**, font name, [≥ 5.2.5] assembly name; **condition**; WinVer; u32 **location**; u32 attributes; u64 external size; u16 permission; set(*n*); enum(2) **type** (0 = user file, 1 = uninstaller) |
| Icon | 6 strings, condition, [≥ 5.3.5] string, [≥ 6.1.0] 16 bytes, WinVer, i32 index, i32 show command, enum(3), u16 hot key, set(3 + [5.0.3 ≤ v < 6.3.0] + [≥ 5.4.2] + [≥ 5.5.0] + [≥ 6.1.0]) |
| INI | 4 strings, condition, WinVer, set(5) |
| Registry | 3 strings, condition, WinVer, u32 hive, u16 permission, enum(7 from 5.2.5, else 6), set(12 from 5.1.0, else 10) |
| Delete (both lists) | string, condition, WinVer, enum(3) |
| Run (both lists) | 5 strings, [≥ 5.1.13] verb, description, condition, WinVer, i32 show command, enum(3), set(7 + 2 [≥ 5.1.10] + [≥ 5.2.0] + [≥ 6.1.0] + [≥ 6.3.0]) |

File option count *n*: 24, +1 from 5.0.3, +1 from 5.1.0, +2 from 5.1.2,
+3 from 5.2.0, +1 from 5.2.5 (32 for every GOG installer).

After the lists: wizard images (large, then small), each [≥ 5.6.0] u32
count then that many strings, before 5.6.0 one string; [compression zlib or
bzip2] the decompressor DLL (string); [encryption used, < 6.4.0] the
decryption DLL (string).

Destinations carry Inno constants: `{app}\IMGS\TERRAINS.DBI` is installed
below the chosen folder; `{tmp}\…` files are setup helpers. The game files
are the `{app}` ones.

## 4. Block 2: file locations

One entry per stored file:

| Field | Type |
|---|---|
| first slice, last slice | u32, u32 |
| chunk offset (in the first slice) | u32 |
| offset in the decompressed chunk | u64 |
| stored size | u64 |
| chunk stored size (after the chunk magic) | u64 |
| checksum of the stored bytes | [≥ 6.4.0] SHA-256 (32), [≥ 5.3.9] SHA-1 (20), else MD5 (16) |
| time stamp (FILETIME) | u64 |
| file version (MS, LS) | 2 × u32 |
| options | see below |

Options: from 6.4.3 a set of 5: version info valid, time stamp in UTC,
**call filter**, **encrypted**, **compressed**. Before: a set of version
info valid, version info not valid, time stamp in UTC, is uninstaller,
**call filter**, touch, **encrypted**, **compressed**, [≥ 5.1.13] solid
break, [5.5.7 ≤ v < 6.3.0] sign, sign once; then [6.3.0 ≤ v < 6.4.3] an enum
sign mode (4).

A compressed file uses the header's compression method; otherwise its
chunk is stored. The call filter is the 4.1.8 variant before 5.2.0, the
5.2.0 variant before 5.3.9 and the 5.3.9 variant from then on (section 7).

## 5. Slices and chunks

With embedded data, the chunk offsets count from *data* in the setup
program. Otherwise the data is in slice files next to the setup program,
named `<base>-<n>.bin` (*n* from 1), or `<base>-<disk><letter>.bin` when
slices per disk > 1 (letter `a` for the first slice of a disk). *base* is the
setup program's own name or the header's base file name. A slice file
starts with `idska32` 1A (or `idska16` 1A), then u32 the slice's total size;
its data follows (chunk offsets count from the file start). A chunk that
does not fit continues after the 12-byte header of the next slice.

Old GOG installers (around 2013–2015) store a RAR archive in the `.bin`
instead (it starts with `Rar!`); they are not supported.

A **chunk** starts with `zlb` 1A, then (if encrypted) an 8-byte salt, then
*chunk stored size* bytes, compressed as a whole. Several files can share
one chunk ("solid" compression); each file is the byte range [offset,
offset + size) of the decompressed chunk. Chunk compression: stored; zlib
(a zlib stream: 2-byte header, Deflate, Adler-32); LZMA (section 6); LZMA2
(one dictionary-size byte *p*: size = (2 | (*p* & 1)) << (*p*/2 + 11), 40
means 4 GiB − 1; then an LZMA2 stream); bzip2 (not supported). Encrypted
chunks are refused.

## 6. LZMA streams

The 5 property bytes: *d* = *pb* × 45 + *lp* × 9 + *lc*, then u32
dictionary size. The raw LZMA stream follows without a size field; it may
end with an end marker, and readers stop once they have what they need.
LZMA2 is the chunked container of LZMA (control byte per chunk:
0 = end, 1/2 = uncompressed chunk with / without dictionary reset,
0x80–0xFF = LZMA chunk with reset level in bits 5–6). Decoding follows the
public LZMA specification (`engine/formats/lzma.*`).

## 7. Call filter

Executables may be stored with relative x86 `CALL`/`JMP` targets made
absolute (better compression). The filter is undone on each file's stored
bytes, starting at the file's first byte, before the checksum is checked.

- **5.2.0 / 5.3.9 variants:** walk the bytes; at an `E8` or `E9` byte at
  position *i*, unless fewer than 5 bytes remain in its 64 KiB block
  (*i* mod 65536 > 65531), take the next 4 bytes. If their last byte is 00
  or FF, read the first three as a 24-bit value, subtract (*i* + 5) mod
  2²⁴, and write the three bytes back; in the 5.3.9 variant also invert the
  fourth byte if bit 23 of the result is set. Continue after the 4 bytes.
  Fewer than 4 bytes left: stop.
- **4.1.8 variant** (data versions before 5.2.0): a position counter *c*
  starts at 5 and grows by one after every byte. At an `E8`/`E9` byte, a
  32-bit value *a* is set to −*c*; each of the next 4 bytes *b* becomes the
  low byte of *a* + *b*, after which *a* = (*a* + *b*) >> 8.

## 8. GOG Galaxy files

Installers whose publisher, publisher URL, support URL or updates URL
mention `GOG.com` / `www.gog.com` may split a file into parts:

- The first part's *before install* is
  `before_install('<md5>', '<name>', <parts>)` (or
  `before_install_dependency(…)`): MD5 of the whole file, its real name and
  the number of parts.
- Each part's *after install* is `after_install('<md5>', <stored size>,
  <size>)` (or `after_install_dependency(…)`).
- Each part's stored bytes are a zlib stream; inflated and joined in order
  they give the file. The parts' own destinations are hash names and are
  dropped.

Arguments are quoted with `'` (a doubled `''` inside is one quote) or bare
numbers. When the name has no Inno constant, it is taken relative to the
part's destination folder (normally `{app}`).

## 9. Not supported

Setup data before 5.0.0 and from Inno Setup 6.5 on (new data layouts and
64-bit offsets), encrypted installers, bzip2 compression, RAR-based GOG
installers. The reader names the version in its error message.
