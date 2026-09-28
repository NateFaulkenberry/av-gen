#!/usr/bin/env python3
"""Terrain slope under the farm animals' stops, estimated from the trace itself.

Every grounded sample of every walker is a (x, y, z) point on (or within `maxFloat` of) the
terrain. Around each stop, fit a plane y = a x + b z + c to all samples from all walkers within R
metres (excluding samples the stop itself contributes, which are one point), when those samples
span two dimensions. The plane gives the local gradient; the animal's yaw at rest gives its facing.
"Facing into the hill" = facing within 45 deg of straight uphill on ground steeper than 12 deg.
"""
import json, math
from collections import defaultdict

SRC = '/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/build/gv3/cast-v2.json'
d = json.load(open(SRC))
E = d['entities']
WALKERS = [n for n in E if n not in ('visitor', 'visitor-beam')]
ANIMALS = sorted(n for n in WALKERS if n.split('-')[0] in ('bull', 'cow', 'horse'))
CUT = {'horse-11': 170.0}
R = 5.0

# spatial hash of all grounded samples
cell = 5.0
grid = defaultdict(list)
for n in WALKERS:
    e = E[n]
    last = None
    for t, p, v in zip(e['t'], e['position'], e['visible']):
        if not v or (n in CUT and t >= CUT[n]):
            continue
        if last is not None and abs(p[0] - last[0]) < 0.05 and abs(p[2] - last[2]) < 0.05:
            continue  # standing: one point is enough
        last = p
        grid[(int(p[0] // cell), int(p[2] // cell))].append(p)


def near(x, z):
    out = []
    cx, cz = int(x // cell), int(z // cell)
    for i in range(cx - 1, cx + 2):
        for j in range(cz - 1, cz + 2):
            for p in grid.get((i, j), ()):
                if (p[0] - x) ** 2 + (p[2] - z) ** 2 <= R * R:
                    out.append(p)
    return out


def fit(points, x0, z0):
    # least squares on (x - x0, z - z0, 1)
    n = len(points)
    if n < 8:
        return None
    sxx = sxz = szz = sx = sz = sy = sxy = szy = 0.0
    for p in points:
        x = p[0] - x0; z = p[2] - z0; y = p[1]
        sxx += x * x; sxz += x * z; szz += z * z; sx += x; sz += z; sy += y; sxy += x * y; szy += z * y
    # centre
    mx, mz, my = sx / n, sz / n, sy / n
    cxx = sxx / n - mx * mx; cxz = sxz / n - mx * mz; czz = szz / n - mz * mz
    cxy = sxy / n - mx * my; czy = szy / n - mz * my
    det = cxx * czz - cxz * cxz
    # need 2-D spread: smallest eigenvalue of the xz covariance
    tr = cxx + czz
    lam_min = tr / 2 - math.sqrt(max(0.0, tr * tr / 4 - det))
    if lam_min < 0.6:  # ~0.8 m std in the thin direction
        return None
    a = (cxy * czz - czy * cxz) / det
    b = (czy * cxx - cxy * cxz) / det
    return a, b


rows = []
tot = {'stops': 0, 'fitted': 0, 'steep12': 0, 'into': 0, 'into_time': 0.0, 'still_time': 0.0, 'steep_time': 0.0}
for n in ANIMALS:
    e = E[n]
    T, P, Y, S = e['t'], e['position'], e['yaw'], e['speed']
    m = len(T) if n not in CUT else next(i for i, t in enumerate(T) if t >= CUT[n])
    stops = []
    i = 0
    while i < m:
        if S[i] < 0.1:
            j = i
            while j < m and S[j] < 0.1:
                j += 1
            stops.append((i, j))
            i = j
        else:
            i += 1
    st = {'name': n, 'stops': 0, 'fitted': 0, 'steep12': 0, 'into': 0, 'into_time': 0.0, 'still_time': 0.0,
          'steep_time': 0.0, 'slopes': []}
    for a, b in stops:
        dur = (b - a) * 0.05
        st['stops'] += 1
        st['still_time'] += dur
        x, z = P[a][0], P[a][2]
        f = fit(near(x, z), x, z)
        if f is None:
            continue
        st['fitted'] += 1
        ga, gb = f
        slope = math.degrees(math.atan(math.hypot(ga, gb)))
        st['slopes'].append(slope)
        if slope > 12:
            st['steep12'] += 1
            st['steep_time'] += dur
            up = math.atan2(ga, gb)  # uphill direction in the yaw convention (x = sin, z = cos)
            # yaw over the stop (median sample)
            yaw = Y[(a + b) // 2]
            diff = abs(math.degrees((yaw - up + math.pi) % (2 * math.pi) - math.pi))
            if diff < 45:
                st['into'] += 1
                st['into_time'] += dur
    rows.append(st)
    for k in tot:
        tot[k] += st[k]

print('| animal | spawn y | stops | slope fitted | median slope at stops | stops on >12 deg | stops facing uphill (+-45) on >12 deg | still s on >12 deg | of which facing uphill s |')
print('|---|---|---|---|---|---|---|---|---|')
for st in rows:
    sl = sorted(st['slopes'])
    med = sl[len(sl) // 2] if sl else float('nan')
    print(f"| {st['name']} | {E[st['name']]['position'][0][1]:.1f} | {st['stops']} | {st['fitted']} | {med:.1f} | {st['steep12']} | {st['into']} | {st['steep_time']:.0f} | {st['into_time']:.0f} |")
print('total', tot)
