#!/usr/bin/env python3
"""Luminance statistics for a rendered PNG, with no third-party dependencies.

Reports the numbers a value structure is actually judged on: the histogram's
placement, how much of the frame sits in each tonal band, and where the bright
mass is. Statistics are not quality; they only make a look measurable enough to
compare two versions of it.
"""
import sys, zlib, struct


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, w = 8, bytearray(), None
    while pos < len(data):
        length, = struct.unpack('>I', data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b'IHDR':
            w, h, depth, colour = struct.unpack('>IIBB', body[:10])
        elif ctype == b'IDAT':
            idat += body
        elif ctype == b'IEND':
            break
        pos += 12 + length
    assert depth == 8 and colour in (2, 6), f'unsupported PNG depth/colour {depth}/{colour}'
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, channels, out


def stats(path):
    w, h, ch, px = read_png(path)
    n = w * h
    lum = [0.0] * n
    sat_sum = 0.0
    for i in range(n):
        r, g, b = px[i * ch] / 255.0, px[i * ch + 1] / 255.0, px[i * ch + 2] / 255.0
        lum[i] = 0.2126 * r + 0.7152 * g + 0.0722 * b
        mx, mn = max(r, g, b), min(r, g, b)
        sat_sum += 0.0 if mx <= 0 else (mx - mn) / mx
    srt = sorted(lum)
    mean = sum(lum) / n

    def pct(q):
        return srt[min(n - 1, int(q * n))]

    shadow = sum(1 for v in lum if v < 0.08) / n
    mid = sum(1 for v in lum if 0.08 <= v < 0.6) / n
    high = sum(1 for v in lum if v >= 0.6) / n
    clipped = sum(1 for v in lum if v > 0.985) / n
    # Where the bright mass sits, as a fraction of the frame from the top-left.
    wsum = sum(lum)
    cx = sum(lum[i] * ((i % w) / w) for i in range(n)) / wsum if wsum > 0 else 0.5
    cy = sum(lum[i] * ((i // w) / h) for i in range(n)) / wsum if wsum > 0 else 0.5
    var = sum((v - mean) ** 2 for v in lum) / n
    return {
        'file': path.split('/')[-1], 'mean': mean, 'rms_contrast': var ** 0.5,
        'p01': pct(0.01), 'p50': pct(0.5), 'p99': pct(0.99),
        'shadow_frac': shadow, 'mid_frac': mid, 'highlight_frac': high,
        'clipped_frac': clipped, 'mean_saturation': sat_sum / n,
        'bright_centroid_x': cx, 'bright_centroid_y': cy,
    }


if __name__ == '__main__':
    for path in sys.argv[1:]:
        s = stats(path)
        print(s.pop('file'))
        for k, v in s.items():
            print(f'  {k:18s} {v:.4f}')
