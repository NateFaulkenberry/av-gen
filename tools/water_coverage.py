#!/usr/bin/env python3
# Does the water surface cover the ground it should?
#
# The instrument behind the "cut-off river" diagnosis. A wide shot of glowmere-valley-2 showed the
# river water ending along a hard diagonal, and the eye cannot tell three things apart there: water
# that was never built, water that was built and not drawn, and water that correctly stops because
# the ground has come up above the waterline. Colour cannot separate them either -- a water fragment
# a hundred metres out is dimmed by aerial perspective until it reads as terrain.
#
# So this asks the frame two independent questions per pixel and crosses them.
#
#   1. Is there water here?  Render the scene TWICE with the water painted two different flat
#      colours (the shallowColor/deepColor/maxOpacity parameters -- ADR-350) and diff. A pixel a
#      water fragment touched changes; a pixel it did not is bit-identical. That is true however
#      dim the water is, which a colour threshold is not. Bloom spreads the difference, so treat a
#      positive as "water touched this pixel" and not as "water covers this pixel".
#
#   2. Should there be water here?  Unproject the depth AOV to a world point and compare it with the
#      authored river: inside the feature width, and below the interpolated path level (a river with
#      waterDepth 0 has its surface exactly at the path level).
#
# Reading the answer. Coverage near the camera is the trustworthy half. At a grazing angle a metre
# of depth error becomes many metres of height error, so the far field is reported but should not be
# believed to better than "most of it is there"; distinguish fog from absence by re-running with
# several thresholds -- a cutoff distance that moves with the threshold is fog.
#
# The camera has to be reconstructed by hand because a render writes no camera alongside the frame.
# Check the fit before trusting the map: `--calibrate` prints the height of every water pixel
# relative to the waterline, which is zero for a correct model. It is how the projection convention
# here was chosen -- horizontal FOV from sensorWidth/focalLength and a depth AOV holding view-space
# z, which fit to a median of -0.20 m where the vertical-FOV and ray-length readings did not.
#
#   tools/water_coverage.py --a red/frame_000000.png --b blue/frame_000000.png \
#       --depth red/frame_000000.depth.exr --scene examples/world/x.scene.json \
#       --river glowmere-run-2 --eye -85.22,51.79,-52.30 --target -6.02,6.59,47.98 --out map.png
#
# The AOV export refuses to run with supersampling, so the arms must set render.supersample to 1.

import argparse, json, math, struct, sys, zlib


def read_exr_scanline_zip(path):
    """The one EXR shape avgen's depth AOV writes: ZIP, 32-bit float, scanline. Enough of the
    format to read it, and no more -- this is not a general reader and will refuse anything else."""
    d = open(path, 'rb').read()
    if d[:4] != b'v/1\x01':
        raise ValueError(f'{path}: not an OpenEXR file')
    i, hdr = 8, {}
    while True:
        j = d.index(b'\0', i); name = d[i:j].decode(); i = j + 1
        if not name:
            break
        j = d.index(b'\0', i); i = j + 1
        size = int.from_bytes(d[i:i + 4], 'little'); i += 4
        hdr[name] = d[i:i + size]; i += size
    cb, k, chans = hdr['channels'], 0, []
    while k < len(cb) and cb[k] != 0:
        j = cb.index(b'\0', k); nm = cb[k:j].decode(); k = j + 1
        chans.append((nm, int.from_bytes(cb[k:k + 4], 'little'))); k += 16
    x0, y0, x1, y1 = struct.unpack('<4i', hdr['dataWindow'])
    W, H = x1 - x0 + 1, y1 - y0 + 1
    comp = hdr['compression'][0]
    if comp != 3:
        raise ValueError(f'{path}: compression {comp}, expected 3 (ZIP)')
    bpp = {0: 1, 1: 2, 2: 4}
    rows, nblocks = 16, (H + 15) // 16
    offs = struct.unpack('<%dQ' % nblocks, d[i:i + 8 * nblocks])
    out = {nm: [0.0] * (W * H) for nm, _ in chans}
    for b in range(nblocks):
        o = offs[b]
        by = int.from_bytes(d[o:o + 4], 'little', signed=True)
        n = int.from_bytes(d[o + 4:o + 8], 'little')
        raw, nrows = d[o + 8:o + 8 + n], min(rows, H - by)
        if n < nrows * sum(bpp[t] for _, t in chans) * W:
            buf = bytearray(zlib.decompress(raw))
            p = buf[0]
            for q in range(1, len(buf)):          # undo the byte delta
                p = (p + buf[q] - 128) & 0xff
                buf[q] = p
            half = (len(buf) + 1) // 2            # undo the two-halves interleave
            res = bytearray(len(buf))
            res[0::2], res[1::2] = buf[:half], buf[half:]
            buf = bytes(res)
        else:
            buf = raw
        pos = 0
        for r in range(nrows):
            y = by + r
            for nm, t in chans:
                nb = bpp[t] * W
                if t == 2:
                    out[nm][y * W:(y + 1) * W] = struct.unpack('<%df' % W, buf[pos:pos + nb])
                pos += nb
    return W, H, out


def chaikin(pts, iterations):
    """The engine's own smoothing (WorldMap::prepare), so distances here mean what they mean there."""
    out = [list(p) for p in pts]
    for _ in range(iterations):
        if len(out) < 3:
            break
        nxt = [out[0]]
        for a, b in zip(out, out[1:]):
            nxt.append([a[k] * 0.75 + b[k] * 0.25 for k in range(3)])
            nxt.append([a[k] * 0.25 + b[k] * 0.75 for k in range(3)])
        nxt.append(out[-1])
        out = nxt
    return out


def closest_on_path(path, x, z):
    best, level = 1e30, 0.0
    for a, b in zip(path, path[1:]):
        dx, dz = b[0] - a[0], b[2] - a[2]
        L = dx * dx + dz * dz
        t = 0.0 if L < 1e-12 else max(0.0, min(1.0, ((x - a[0]) * dx + (z - a[2]) * dz) / L))
        dist = math.hypot(x - (a[0] + dx * t), z - (a[2] + dz * t))
        if dist < best:
            best, level = dist, a[1] + (b[1] - a[1]) * t
    return best, level


def find_river(scene_path, name):
    doc = json.load(open(scene_path))
    found = []

    def walk(o):
        if isinstance(o, dict):
            for f in o.get('features', []) or []:
                if f.get('water') and (name is None or f.get('name') == name):
                    found.append(f)
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(doc)
    if not found:
        raise SystemExit(f'no water feature {name!r} in {scene_path}')
    return found[0]


def vec(s):
    return [float(v) for v in s.split(',')]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--a', required=True, help='frame with the water painted colour A')
    ap.add_argument('--b', required=True, help='the same frame with the water painted colour B')
    ap.add_argument('--depth', required=True, help='depth AOV EXR for either arm')
    ap.add_argument('--scene', required=True)
    ap.add_argument('--river', default=None, help='water feature name (default: the first one)')
    ap.add_argument('--eye', type=vec, required=True)
    ap.add_argument('--target', type=vec, required=True)
    ap.add_argument('--focal', type=float, default=30.0)
    ap.add_argument('--sensor-width', type=float, default=36.0)
    ap.add_argument('--threshold', type=float, default=12.0, help='sum of |channel| a water pixel must move')
    ap.add_argument('--step', type=int, default=2)
    ap.add_argument('--out', default=None, help='write a green/blue/red coverage map here')
    ap.add_argument('--calibrate', action='store_true',
                    help='report water-pixel height relative to the waterline instead; 0 means the '
                         'camera model is right')
    args = ap.parse_args()

    from PIL import Image
    W, H, ch = read_exr_scanline_zip(args.depth)
    D = ch['R']
    A = Image.open(args.a).convert('RGB')
    B = Image.open(args.b).convert('RGB')
    if A.size != (W, H) or B.size != (W, H):
        raise SystemExit(f'frames are {A.size}/{B.size} but the depth AOV is {(W, H)}')
    A, B = A.load(), B.load()

    feat = find_river(args.scene, args.river)
    path = chaikin(feat['path'], int(feat.get('smoothing', 0)))
    width = float(feat['width'])
    depth_below = float(feat.get('waterDepth', 0.0) or 0.0)

    P, T = args.eye, args.target
    fwd = [T[k] - P[k] for k in range(3)]
    n = math.sqrt(sum(v * v for v in fwd)); fwd = [v / n for v in fwd]
    right = [-fwd[2], 0.0, fwd[0]]
    n = math.hypot(right[0], right[2]); right = [right[0] / n, 0.0, right[2] / n]
    up = [right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
          right[0] * fwd[1] - right[1] * fwd[0]]
    tx = (args.sensor_width * 0.5) / args.focal
    ty = tx * H / W

    out = Image.new('RGB', (W, H)) if args.out else None
    o = out.load() if out else None
    bands, offsets = {}, []
    for y in range(0, H, args.step):
        for x in range(0, W, args.step):
            dep = D[y * W + x]
            colour = (18, 18, 18)
            if dep < 1e6:
                ndx, ndy = (x + 0.5) / W * 2 - 1, 1 - (y + 0.5) / H * 2
                d = [fwd[k] + right[k] * ndx * tx + up[k] * ndy * ty for k in range(3)]
                p = [P[k] + d[k] * dep for k in range(3)]
                dist, level = closest_on_path(path, p[0], p[2])
                surface = level + depth_below
                wet = dist < width and p[1] < surface - 0.10
                a, b = A[x, y], B[x, y]
                touched = sum(abs(a[k] - b[k]) for k in range(3)) >= args.threshold
                if touched and args.calibrate and dist < width:
                    offsets.append(p[1] - surface)
                rng = math.dist(P, p)
                if wet:
                    band = bands.setdefault(int(rng // 20) * 20, [0, 0])
                    band[0] += 1
                    band[1] += 1 if touched else 0
                colour = ((0, 170, 0) if touched and wet else (0, 70, 210) if touched
                          else (255, 0, 0) if wet else (38, 38, 38))
            if o:
                for dy in range(args.step):
                    for dx in range(args.step):
                        if x + dx < W and y + dy < H:
                            o[x + dx, y + dy] = colour
    if out:
        out.save(args.out)
        print(f'wrote {args.out}  (green: water where the bed is under the waterline; '
              f'blue: water elsewhere, inflated by bloom; red: bed under the waterline, no water)')

    if args.calibrate:
        if not offsets:
            raise SystemExit('no water pixels: check the two arms really differ')
        offsets.sort()
        n = len(offsets)
        print(f'camera fit over {n} water pixels -- height relative to the waterline, '
              f'which a correct model puts at 0:')
        print('   p10 %+.2f m   median %+.2f m   p90 %+.2f m   IQR %.2f m'
              % (offsets[n // 10], offsets[n // 2], offsets[n * 9 // 10],
                 offsets[n * 3 // 4] - offsets[n // 4]))
        return

    print('coverage of the ground the water should be standing on, by distance from the camera')
    print('a cutoff that moves when --threshold changes is aerial perspective, not absence')
    print('%11s %9s %9s' % ('range m', 'samples', 'covered'))
    for lo in sorted(bands):
        total, hit = bands[lo]
        print('%5d-%5d %9d %8.1f%%' % (lo, lo + 20, total, 100.0 * hit / total))


if __name__ == '__main__':
    main()
