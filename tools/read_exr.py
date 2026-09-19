#!/usr/bin/env python3
"""A minimal reader for the EXRs this renderer writes: ZIP-compressed scanline, half or float.

No third-party dependency on purpose -- the measuring tools in this repository run wherever the
build does, and an AOV nobody can read is an AOV nobody checks. Handles exactly what
`src/assets/exr.hpp` emits (ZIP or ZIPS, single part, no tiles); anything else raises.
"""
from __future__ import annotations

import struct
import sys
import zlib

HALF, FLOAT, UINT = 1, 2, 0


def _half_to_float(h: int) -> float:
    s = (h >> 15) & 1
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    if e == 0:
        v = m * 2.0 ** -24
    elif e == 31:
        v = float('inf') if m == 0 else float('nan')
    else:
        v = (m + 1024) * 2.0 ** (e - 25)
    return -v if s else v


_HALF = [_half_to_float(i) for i in range(1 << 16)]


def _unzip(block: bytes) -> bytes:
    raw = bytearray(zlib.decompress(block))
    for i in range(1, len(raw)):
        raw[i] = (raw[i - 1] + raw[i] - 128) & 0xFF
    half = (len(raw) + 1) // 2
    out = bytearray(len(raw))
    out[0::2] = raw[:half]
    out[1::2] = raw[half:]
    return bytes(out)


def read_exr(path):
    """-> (width, height, {channel_name: [float] * width * height}), rows top-down."""
    d = open(path, 'rb').read()
    magic, = struct.unpack_from('<I', d, 0)
    if magic != 20000630:
        raise ValueError(f'{path}: not an EXR')
    p = 8
    attrs = {}
    while d[p] != 0:
        name_end = d.index(b'\0', p)
        name = d[p:name_end].decode()
        p = name_end + 1
        type_end = d.index(b'\0', p)
        atype = d[p:type_end].decode()
        p = type_end + 1
        size, = struct.unpack_from('<i', d, p)
        p += 4
        attrs[name] = (atype, d[p:p + size])
        p += size
    p += 1

    chans = []
    body = attrs['channels'][1]
    q = 0
    while body[q] != 0:
        e = body.index(b'\0', q)
        cname = body[q:e].decode()
        q = e + 1
        ptype, = struct.unpack_from('<i', body, q)
        q += 16
        chans.append((cname, ptype))
    chans.sort(key=lambda c: c[0])

    x0, y0, x1, y1 = struct.unpack_from('<iiii', attrs['dataWindow'][1], 0)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    comp = attrs['compression'][1][0]
    rows_per_block = {0: 1, 1: 1, 2: 1, 3: 16}.get(comp)
    if rows_per_block is None:
        raise ValueError(f'{path}: unsupported compression {comp}')

    nblocks = (h + rows_per_block - 1) // rows_per_block
    offsets = struct.unpack_from(f'<{nblocks}Q', d, p)

    out = {c: [0.0] * (w * h) for c, _ in chans}
    for off in offsets:
        y, size = struct.unpack_from('<ii', d, off)
        block = d[off + 8:off + 8 + size]
        rows = min(rows_per_block, h - (y - y0))
        expected = sum(w * rows * (2 if t == HALF else 4) for _, t in chans)
        data = block if len(block) == expected else _unzip(block)
        k = 0
        for r in range(rows):
            dst = (y - y0 + r) * w
            for cname, ptype in chans:
                if ptype == HALF:
                    vals = struct.unpack_from(f'<{w}H', data, k)
                    k += w * 2
                    out[cname][dst:dst + w] = [_HALF[v] for v in vals]
                else:
                    out[cname][dst:dst + w] = struct.unpack_from(f'<{w}f', data, k)
                    k += w * 4
    return w, h, out


if __name__ == '__main__':
    for path in sys.argv[1:]:
        w, h, ch = read_exr(path)
        print(f'{path}: {w}x{h}')
        for name, v in sorted(ch.items()):
            lo, hi = min(v), max(v)
            print(f'  {name:8s} min={lo:.4f} max={hi:.4f} mean={sum(v)/len(v):.4f}')
