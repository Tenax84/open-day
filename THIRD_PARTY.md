# Third-party components

OpenDay itself is licensed under GPL-3.0 (see [LICENSE](LICENSE)). The build
fetches these components (pinned by version and SHA-256 in
[`cmake/Dependencies.cmake`](cmake/Dependencies.cmake)) and links or embeds
them into the programs:

| Component | Use | License |
|---|---|---|
| [SDL 3](https://libsdl.org) | windowing, input, audio output | zlib |
| [SQLite](https://sqlite.org) | reading the converted game databases | public domain |
| [stb_truetype](https://github.com/nothings/stb) | rasterizing the interface font | public domain / MIT |
| [stb_vorbis](https://github.com/nothings/stb) | decoding the Ogg Vorbis music | public domain / MIT |
| [Liberation Sans](https://github.com/liberationfonts/liberation-fonts) 2.1.5 | interface font, embedded in the program | SIL Open Font License 1.1 |

The Liberation fonts are distributed under the SIL Open Font License 1.1;
its text ships with the font sources (`LICENSE` in the font archive) and
must accompany binary releases. The release packages contain it, together
with the SDL3 and stb license texts, in `licenses/` (see
[`cmake/Packaging.cmake`](cmake/Packaging.cmake)). On Windows the packages
also contain the Microsoft Visual C++ runtime DLLs, which Microsoft permits
to be redistributed with applications.

The ZIP and Deflate reading (`engine/formats/zip.*`, `inflate.*`), the
Jet 3 database reader (`engine/formats/mdb.*`), and the readers of the
installation media (`engine/formats/lzma.*`, `iso9660.*`, `inno_setup.*`,
`hash.*`) are OpenDay's own code. The Inno Setup layouts were taken from
the published Inno Setup source (`Shared.Struct.pas`, Inno Setup License)
and from innoextract's documentation and version tables (zlib license);
see [`docs/formats/INNO.md`](docs/formats/INNO.md).

No file of the original game is part of OpenDay or of its releases. The
game data is read at run time from the user's own installation.
