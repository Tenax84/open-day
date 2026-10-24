#!/usr/bin/env python3
"""Convert the game's Jet 3 (Access 97) databases to SQLite.

OpenDay tool - see docs/formats/MDB.md for the file format notes and
docs/mechanics/data-model.md for what the tables mean.

Optional: the game converts the databases itself on first start, and the
native `mdbtool` (engine/data/mdb_import.*) produces the same output. This
script is kept as the reference implementation and for development.

The converter has no dependencies outside the Python 3.8+ standard library and
runs on macOS, Linux and Windows alike. It contains its own small read-only
Jet 3 reader (table definitions, data pages, overflow rows and memo/OLE
values); it does not need mdbtools, ODBC or the Microsoft Jet engine. Jet 3
database passwords do not encrypt the data and are ignored.

It never writes into the game directory. The game data stays on the user's
machine: do not commit the generated SQLite files anywhere.

Usage:
    # convert every database of an installation (the .MDB files in GLOBALS
    # and SCENS, and the .mdb members of the ZIP archives in SCENS and
    # COLONIES) into OUT_DIR, mirroring the folder layout:
    python3 mdb2sqlite.py --game-dir "/path/to/Fallen Haven Liberation Day" OUT_DIR
    OPENDAY_GAME_DIR="/path/to/game" python3 mdb2sqlite.py OUT_DIR

    # convert single files:
    python3 mdb2sqlite.py --file GLOBAL.MDB OUT_DIR

    # list tables and row counts without writing anything:
    python3 mdb2sqlite.py --list --file GLOBAL.MDB

Output: one .sqlite file per database. Every user table becomes a SQLite
table with the original (case-preserved) table and column names. Two extra
tables describe the source: `_mdb_columns` (original Jet column types) and
`_mdb_links` (tables that the database only links to in another file).
Saved queries, indexes and relationships are not converted.

Text is decoded as Windows-1252 by default (--codepage to override).
"""

import argparse
import io
import os
import sqlite3
import struct
import sys
import zipfile

PAGE_SIZE = 2048  # Jet 3

# Jet column type codes
T_BOOL, T_BYTE, T_INT, T_LONG, T_MONEY, T_FLOAT, T_DOUBLE, T_DATETIME = 1, 2, 3, 4, 5, 6, 7, 8
T_BINARY, T_TEXT, T_OLE, T_MEMO, T_GUID, T_NUMERIC = 9, 10, 11, 12, 15, 16

TYPE_NAMES = {
    T_BOOL: 'BOOLEAN', T_BYTE: 'BYTE', T_INT: 'INT16', T_LONG: 'INT32',
    T_MONEY: 'MONEY', T_FLOAT: 'FLOAT', T_DOUBLE: 'DOUBLE', T_DATETIME: 'DATETIME',
    T_BINARY: 'BINARY', T_TEXT: 'TEXT', T_OLE: 'OLE', T_MEMO: 'MEMO',
    T_GUID: 'GUID', T_NUMERIC: 'NUMERIC',
}
SQLITE_TYPES = {
    T_BOOL: 'INTEGER', T_BYTE: 'INTEGER', T_INT: 'INTEGER', T_LONG: 'INTEGER',
    T_MONEY: 'REAL', T_FLOAT: 'REAL', T_DOUBLE: 'REAL', T_DATETIME: 'REAL',
    T_BINARY: 'BLOB', T_TEXT: 'TEXT', T_OLE: 'BLOB', T_MEMO: 'TEXT',
    T_GUID: 'BLOB', T_NUMERIC: 'BLOB',
}

ROW_DELETED = 0x8000
ROW_OVERFLOW = 0x4000
ROW_OFFSET_MASK = 0x1FFF


class JetError(Exception):
    pass


class Column:
    __slots__ = ('name', 'type', 'num', 'var_index', 'fixed_offset', 'length', 'flags')

    @property
    def is_fixed(self):
        return bool(self.flags & 0x01)


class TableColumns(list):
    """Column list of a table; has_var tells whether rows carry a variable
    column section (Jet 3 omits it in tables without variable columns)."""
    has_var = True


class Jet3Database:
    """Minimal read-only reader for Jet 3 (.mdb, Access 97) files."""

    def __init__(self, data, codepage='cp1252'):
        if len(data) < PAGE_SIZE or data[0:4] != b'\x00\x01\x00\x00':
            raise JetError('not a Jet database')
        if data[4:19] != b'Standard Jet DB':
            raise JetError('not a Jet database')
        if data[0x14] != 0:
            raise JetError('not a Jet 3 database (version byte %d)' % data[0x14])
        self.data = data
        self.codepage = codepage
        self.num_pages = len(data) // PAGE_SIZE
        self._owner_pages = None

    # -- low level -------------------------------------------------------
    def page(self, n):
        if n <= 0 or n >= self.num_pages:
            raise JetError('page %d out of range' % n)
        off = n * PAGE_SIZE
        return self.data[off:off + PAGE_SIZE]

    @staticmethod
    def _row_bounds(pg, row):
        """Return (start, end_exclusive, flags) of a row on a data page."""
        nrows = struct.unpack_from('<H', pg, 8)[0]
        if row >= nrows:
            raise JetError('row %d not on page' % row)
        raw = struct.unpack_from('<H', pg, 10 + row * 2)[0]
        start = raw & ROW_OFFSET_MASK
        end = PAGE_SIZE
        # the row ends where the previous non-empty row starts (rows are
        # stored from the end of the page towards the start)
        for prev in range(row - 1, -1, -1):
            p = struct.unpack_from('<H', pg, 10 + prev * 2)[0] & ROW_OFFSET_MASK
            if p:
                end = p
                break
        return start, end, raw & ~ROW_OFFSET_MASK

    def _read_row_pointer(self, ptr):
        """Resolve a (row | page << 8) pointer to row bytes."""
        row, pgno = ptr & 0xFF, ptr >> 8
        pg = self.page(pgno)
        start, end, _ = self._row_bounds(pg, row)
        return pg[start:end]

    # -- table definitions ------------------------------------------------
    def _tdef_bytes(self, pgno):
        pg = self.page(pgno)
        if pg[0] != 0x02:
            raise JetError('page %d is not a table definition' % pgno)
        buf = bytearray(pg)
        nxt = struct.unpack_from('<I', pg, 4)[0]
        seen = {pgno}
        while nxt and nxt not in seen:
            seen.add(nxt)
            p = self.page(nxt)
            buf += p[8:]
            nxt = struct.unpack_from('<I', p, 4)[0]
        return bytes(buf)

    def table_def(self, pgno):
        b = self._tdef_bytes(pgno)
        num_rows = struct.unpack_from('<I', b, 0x0C)[0]
        num_var_cols = struct.unpack_from('<H', b, 0x17)[0]
        num_cols = struct.unpack_from('<H', b, 0x19)[0]
        num_real_idx = struct.unpack_from('<I', b, 0x1F)[0]
        pos = 0x2B + num_real_idx * 8
        cols = []
        for _ in range(num_cols):
            c = Column()
            c.type = b[pos]
            c.num = struct.unpack_from('<H', b, pos + 1)[0]
            c.var_index = struct.unpack_from('<H', b, pos + 3)[0]
            c.flags = b[pos + 13]
            c.fixed_offset = struct.unpack_from('<H', b, pos + 14)[0]
            c.length = struct.unpack_from('<H', b, pos + 16)[0]
            cols.append(c)
            pos += 18
        for c in cols:
            n = b[pos]
            c.name = b[pos + 1:pos + 1 + n].decode(self.codepage, 'replace')
            pos += 1 + n
        cols.sort(key=lambda c: c.num)
        cols = TableColumns(cols)
        cols.has_var = num_var_cols > 0
        return num_rows, cols

    # -- rows -------------------------------------------------------------
    def _pages_by_owner(self):
        if self._owner_pages is None:
            owners = {}
            for n in range(1, self.num_pages):
                off = n * PAGE_SIZE
                if self.data[off] == 0x01 and self.data[off + 1] == 0x01:
                    owner = struct.unpack_from('<I', self.data, off + 4)[0]
                    owners.setdefault(owner, []).append(n)
            self._owner_pages = owners
        return self._owner_pages

    def raw_rows(self, tdef_pg):
        """Yield the raw bytes of every live row of a table."""
        pages = self._pages_by_owner().get(tdef_pg, [])
        rows, targets = [], set()
        for n in pages:
            pg = self.page(n)
            nrows = struct.unpack_from('<H', pg, 8)[0]
            for r in range(nrows):
                start, end, flags = self._row_bounds(pg, r)
                if flags & ROW_DELETED or start == 0:
                    continue
                if flags & ROW_OVERFLOW:
                    ptr = struct.unpack_from('<I', pg, start)[0]
                    targets.add((ptr >> 8, ptr & 0xFF))
                    rows.append(('ptr', ptr, None))
                else:
                    rows.append(('row', (n, r), pg[start:end]))
        for kind, key, raw in rows:
            if kind == 'ptr':
                yield self._read_row_pointer(key)
            elif key not in targets:
                yield raw

    def _long_value(self, field):
        """Decode a memo/OLE field descriptor and return the value bytes."""
        if len(field) < 12:
            return b''
        length = struct.unpack_from('<I', field, 0)[0]
        kind, length = length & 0xC0000000, length & 0x3FFFFFFF
        ptr = struct.unpack_from('<I', field, 4)[0]
        if kind == 0x80000000:  # stored inline after the 12-byte header
            return field[12:12 + length]
        if kind == 0x40000000:  # one row on another page
            return self._read_row_pointer(ptr)[:length]
        out = bytearray()  # chain of rows, each starting with a next pointer
        seen = set()
        while ptr and ptr not in seen and len(out) < length:
            seen.add(ptr)
            chunk = self._read_row_pointer(ptr)
            ptr = struct.unpack_from('<I', chunk, 0)[0]
            out += chunk[4:]
        return bytes(out[:length])

    def decode_row(self, raw, cols):
        row_cols = raw[0]
        mask_len = (row_cols + 7) // 8
        mask = raw[len(raw) - mask_len:]
        row_end = len(raw) - 1  # index of the last byte
        offsets = []
        var_count = raw[row_end - mask_len] if getattr(cols, 'has_var', True) else 0
        if var_count:
            offsets = self._var_offsets(raw, row_end, mask_len, var_count)

        values = []
        for c in cols:
            present = c.num < row_cols and bool(mask[c.num // 8] & (1 << (c.num % 8)))
            if c.type == T_BOOL:
                values.append(1 if present else 0)
                continue
            if not present:
                values.append(None)
                continue
            if c.is_fixed:
                o = 1 + c.fixed_offset
                v = raw[o:o + c.length]
            else:
                if c.var_index >= var_count:
                    values.append(None)
                    continue
                v = raw[offsets[c.var_index]:offsets[c.var_index + 1]]
            values.append(self._convert(c, v))
        return values

    @staticmethod
    def _var_offsets(raw, row_end, mask_len, var_count):
        """Variable column offsets (Jet 3: one byte each, stored backwards,
        plus a jump table that adds 256 for every 256-byte boundary)."""
        num_jumps = (len(raw) - 1) // 256
        col_ptr = row_end - mask_len - num_jumps - 1
        if (col_ptr - var_count) // 256 < num_jumps:
            num_jumps -= 1
        offsets = []
        jumps = 0
        for i in range(var_count + 1):
            while jumps < num_jumps and i == raw[row_end - mask_len - jumps - 1]:
                jumps += 1
            offsets.append(raw[col_ptr - i] + jumps * 256)
        return offsets

    def _convert(self, c, v):
        t = c.type
        if t == T_BYTE:
            return v[0]
        if t == T_INT:
            return struct.unpack('<h', v[:2])[0]
        if t == T_LONG:
            return struct.unpack('<i', v[:4])[0]
        if t == T_MONEY:
            return struct.unpack('<q', v[:8])[0] / 10000.0
        if t == T_FLOAT:
            return struct.unpack('<f', v[:4])[0]
        if t in (T_DOUBLE, T_DATETIME):
            return struct.unpack('<d', v[:8])[0]
        if t == T_TEXT:
            return v.decode(self.codepage, 'replace')
        if t == T_MEMO:
            return self._long_value(v).decode(self.codepage, 'replace')
        if t == T_OLE:
            return self._long_value(v)
        return bytes(v)

    # -- catalog ------------------------------------------------------------
    def catalog(self):
        """Return a list of dicts for MSysObjects (the system catalog, page 2)."""
        _, cols = self.table_def(2)
        names = [c.name for c in cols]
        return [dict(zip(names, self.decode_row(raw, cols))) for raw in self.raw_rows(2)]

    def tables(self):
        """Return (user tables, linked tables) from the catalog."""
        user, linked = [], []
        for r in self.catalog():
            name = r.get('Name') or ''
            if name.startswith('MSys') or (r.get('ParentId') or 0) != 0x0F000001:
                continue
            if r.get('Type') == 1 and not ((r.get('Flags') or 0) & 0x80000002):
                user.append((name, (r.get('Id') or 0) & 0x00FFFFFF))
            elif r.get('Type') == 6:
                linked.append((name, r.get('ForeignName') or ''))
        return sorted(user, key=lambda x: x[0].lower()), linked

    def read_table(self, tdef_pg):
        _, cols = self.table_def(tdef_pg)
        return cols, [self.decode_row(raw, cols) for raw in self.raw_rows(tdef_pg)]


def qi(name):
    return '"' + name.replace('"', '""') + '"'


def convert(data, out_path, codepage):
    db = Jet3Database(data, codepage)
    user, linked = db.tables()
    tmp = out_path + '.tmp'
    if os.path.exists(tmp):
        os.remove(tmp)
    con = sqlite3.connect(tmp)
    con.execute('CREATE TABLE _mdb_columns (table_name TEXT, position INTEGER, '
                'column_name TEXT, jet_type TEXT, jet_length INTEGER)')
    con.execute('CREATE TABLE _mdb_links (table_name TEXT, foreign_name TEXT)')
    con.executemany('INSERT INTO _mdb_links VALUES (?, ?)', linked)
    summary = []
    for name, pg in user:
        cols, rows = db.read_table(pg)
        con.execute('CREATE TABLE %s (%s)' % (qi(name), ', '.join(
            '%s %s' % (qi(c.name), SQLITE_TYPES.get(c.type, 'BLOB')) for c in cols)))
        con.executemany('INSERT INTO _mdb_columns VALUES (?, ?, ?, ?, ?)', [
            (name, i, c.name, TYPE_NAMES.get(c.type, 'TYPE%d' % c.type), c.length)
            for i, c in enumerate(cols)])
        if rows:
            con.executemany('INSERT INTO %s VALUES (%s)' % (qi(name), ','.join('?' * len(cols))), rows)
        summary.append((name, len(rows)))
    con.commit()
    con.close()
    os.replace(tmp, out_path)
    return summary


def list_db(data, label, codepage):
    db = Jet3Database(data, codepage)
    user, linked = db.tables()
    print(label)
    for name, pg in user:
        cols, rows = db.read_table(pg)
        print('  %-40s %6d rows, %d columns' % (name, len(rows), len(cols)))
    for name, foreign in linked:
        print('  %-40s (linked)' % name)


def iter_install(game_dir):
    """Yield (relative output stem, bytes) for every database of an install."""
    def find(parent, name):
        if not os.path.isdir(parent):
            return None
        for e in os.listdir(parent):
            if e.lower() == name.lower():
                return os.path.join(parent, e)
        return None

    for folder in ('GLOBALS', 'SCENS'):
        d = find(game_dir, folder)
        if not d:
            continue
        for e in sorted(os.listdir(d)):
            if e.lower().endswith('.mdb'):
                with open(os.path.join(d, e), 'rb') as f:
                    yield os.path.join(folder, os.path.splitext(e)[0]), f.read()
    for folder, archive in (('SCENS', 'SCEN1.ZIP'), ('SCENS', 'SCEN2.ZIP'),
                            ('SCENS', 'DEATHM.ZIP'), ('COLONIES', 'COLONIES.ZIP')):
        d = find(game_dir, folder)
        z = find(d, archive) if d else None
        if not z:
            continue
        with zipfile.ZipFile(z) as zf:
            for info in sorted(zf.infolist(), key=lambda i: i.filename.lower()):
                base = os.path.basename(info.filename)
                if base.lower().endswith('.mdb'):
                    stem = os.path.join(folder, os.path.splitext(archive)[0],
                                        os.path.splitext(base)[0])
                    yield stem, zf.read(info)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out_dir', nargs='?', help='output directory for the .sqlite files')
    ap.add_argument('--game-dir', default=os.environ.get('OPENDAY_GAME_DIR'),
                    help='game installation directory (default: $OPENDAY_GAME_DIR)')
    ap.add_argument('--file', action='append', default=[],
                    help='convert this .mdb file instead of a whole installation (repeatable)')
    ap.add_argument('--list', action='store_true', help='only list tables and row counts')
    ap.add_argument('--codepage', default='cp1252', help='text encoding (default cp1252)')
    args = ap.parse_args(argv)

    if args.file:
        sources = []
        for p in args.file:
            with open(p, 'rb') as f:
                sources.append((os.path.splitext(os.path.basename(p))[0], f.read()))
    elif args.game_dir:
        sources = iter_install(args.game_dir)
    else:
        ap.error('give --game-dir, set OPENDAY_GAME_DIR, or use --file')

    if args.list:
        for stem, data in sources:
            list_db(data, stem, args.codepage)
        return 0

    if not args.out_dir:
        ap.error('an output directory is required')
    out_dir = os.path.abspath(args.out_dir)
    if args.game_dir:
        g = os.path.abspath(args.game_dir)
        if os.path.normcase(out_dir + os.sep).startswith(os.path.normcase(g + os.sep)):
            ap.error('refusing to write inside the game directory')
    failures = 0
    for stem, data in sources:
        target = os.path.join(out_dir, stem + '.sqlite')
        os.makedirs(os.path.dirname(target), exist_ok=True)
        try:
            summary = convert(data, target, args.codepage)
            print('%s: %d tables, %d rows' % (stem, len(summary), sum(n for _, n in summary)))
        except (JetError, struct.error, IndexError) as e:
            failures += 1
            print('%s: FAILED (%s)' % (stem, e), file=sys.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
