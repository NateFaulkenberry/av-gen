#!/usr/bin/env python3
"""Measure GV3 cast behaviour from build/gv3/cast-v2.json (20 Hz, 226 s).

Read-only. Yaw convention (verified empirically): forward = (sin yaw, 0, cos yaw),
i.e. yaw = atan2(dx, dz) while walking (mean |err| 0.006 rad).
"""
import json, math, sys, statistics as st
from collections import defaultdict

SRC = '/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/build/gv3/cast-v2.json'
d = json.load(open(SRC))
E = d['entities']
DT = 1.0 / d['hz']
STILL = 0.10       # m/s: "stationary"
NEAR0 = 0.30       # m/s: "near zero" for pivot accounting
REV_DEG = 150.0    # a reversal
HEAD_DIST = 1.5    # metres of travel used to measure the in/out heading around a stop
REVISIT_R = 4.0    # metres: a stop within this of an earlier stop is a revisit
CUT = {'horse-11': 170.0}   # staging owns horse-11 from the beam on

ALIENS = ['rook', 'tide', 'sage', 'ember', 'vane']
ANIMALS = sorted(n for n in E if any(n.startswith(p) for p in ('bull', 'cow', 'horse')))


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def ang(a, b):
    return abs(math.degrees(wrap(a - b)))


def pct(xs, p):
    if not xs:
        return float('nan')
    xs = sorted(xs)
    k = (len(xs) - 1) * p
    f = math.floor(k); c = math.ceil(k)
    return xs[f] if f == c else xs[f] + (xs[c] - xs[f]) * (k - f)


def runs(flags):
    """[(start, end_exclusive, value)] of equal consecutive flags."""
    out = []; s = 0
    for i in range(1, len(flags) + 1):
        if i == len(flags) or flags[i] != flags[s]:
            out.append((s, i, flags[s])); s = i
    return out


def heading_over(P, idx_iter, dist):
    """Direction of travel accumulated over `dist` metres walking idx_iter (indices in order)."""
    idx = list(idx_iter)
    if len(idx) < 2:
        return None
    x0, z0 = P[idx[0]][0], P[idx[0]][2]
    for i in idx[1:]:
        dx = P[i][0] - x0; dz = P[i][2] - z0
        if math.hypot(dx, dz) >= dist:
            return math.atan2(dx, dz)
    dx = P[idx[-1]][0] - x0; dz = P[idx[-1]][2] - z0
    return math.atan2(dx, dz) if math.hypot(dx, dz) > 0.3 else None


def analyse(name):
    e = E[name]
    T = e['t']; P = e['position']; Y = e['yaw']; S = e['speed']; A = e['activity']
    n = len(T)
    if name in CUT:
        n = next(i for i, t in enumerate(T) if t >= CUT[name])
    T, P, Y, S, A = T[:n], P[:n], Y[:n], S[:n], A[:n]
    still = [s < STILL for s in S]
    R = runs(still)
    stops = [(a, b) for a, b, v in R if v]
    moves = [(a, b) for a, b, v in R if not v]
    stop_durs = [(b - a) * DT for a, b in stops]
    res = {'name': name, 'seconds': n * DT}
    res['stationary_frac'] = sum(still) / n
    res['longest_still_s'] = max(stop_durs) if stop_durs else 0
    res['stops'] = len(stops)
    res['stops_ge3s'] = sum(1 for x in stop_durs if x >= 3)
    res['stops_ge5s'] = sum(1 for x in stop_durs if x >= 5)
    res['stops_ge8s'] = sum(1 for x in stop_durs if x >= 8)
    res['still_time_in_ge5s'] = sum(x for x in stop_durs if x >= 5)
    res['stop_dur_med'] = st.median(stop_durs) if stop_durs else 0
    res['stop_dur_p90'] = pct(stop_durs, 0.9)
    # ---- reversals around stops -------------------------------------------------------------
    rev = 0; rev_short = 0; turns_at_stops = []; pivot_at_stop = []
    for a, b in stops:
        if a == 0 or b >= n:
            continue
        hin = heading_over(P, range(a, max(-1, a - 200), -1), HEAD_DIST)
        hout = heading_over(P, range(b - 1, min(n, b + 200)), HEAD_DIST)
        if hin is None or hout is None:
            continue
        hin = wrap(hin + math.pi)  # we walked backwards from the stop, so flip
        delta = ang(hout, hin)
        turns_at_stops.append(delta)
        yaw_in_stop = sum(abs(wrap(Y[i] - Y[i - 1])) for i in range(a + 1, b))
        pivot_at_stop.append(math.degrees(yaw_in_stop))
        if delta > REV_DEG:
            rev += 1
            if (b - a) * DT <= 6.0:
                rev_short += 1
    res['stop_turns'] = len(turns_at_stops)
    res['reversals'] = rev
    res['reversals_stop_le6s'] = rev_short
    res['turn_at_stop_med_deg'] = st.median(turns_at_stops) if turns_at_stops else float('nan')
    res['turns_at_stops_gt90'] = sum(1 for x in turns_at_stops if x > 90)
    # ---- reversals while moving (no stop): heading change >150 deg within 3 s -----------------
    mrev = 0
    for a, b in moves:
        i = a
        while i < b - 1:
            j = i + 1; found = False
            while j < b and (j - i) * DT <= 3.0:
                if S[i] > NEAR0 and S[j] > NEAR0 and ang(Y[j], Y[i]) > REV_DEG:
                    found = True; break
                j += 1
            if found:
                mrev += 1; i = j
            else:
                i += 5
    res['reversals_moving'] = mrev
    # ---- pivoting: yaw change at ~0 speed ------------------------------------------------------
    tot = 0.0; piv = 0.0; piv_still = 0.0; max_rate_still = 0.0
    for i in range(1, n):
        dy = abs(wrap(Y[i] - Y[i - 1]))
        tot += dy
        if max(S[i], S[i - 1]) < NEAR0:
            piv += dy
        if max(S[i], S[i - 1]) < STILL:
            piv_still += dy
            max_rate_still = max(max_rate_still, math.degrees(dy) / DT)
    res['yaw_total_deg'] = math.degrees(tot)
    res['yaw_near0_deg'] = math.degrees(piv)
    res['yaw_near0_frac'] = piv / tot if tot else 0
    res['yaw_still_frac'] = piv_still / tot if tot else 0
    res['max_yaw_rate_still_dps'] = max_rate_still
    res['turn_activity_frac'] = sum(1 for x in A if x == 'turn') / n
    # Of large direction changes, how many happened at a stop?
    res['pivot_at_stop_med_deg'] = st.median(pivot_at_stop) if pivot_at_stop else float('nan')
    # ---- turning radius while moving: yaw rate vs speed -------------------------------------
    radii = []
    for i in range(1, n):
        if S[i] > 0.5 and S[i - 1] > 0.5:
            w = abs(wrap(Y[i] - Y[i - 1])) / DT
            if w > math.radians(10):
                radii.append(S[i] / w)
    res['moving_turn_samples'] = len(radii)
    res['moving_turn_radius_med_m'] = st.median(radii) if radii else float('nan')
    # ---- path segments ------------------------------------------------------------------------
    seg_len = []; seg_chord = []; seg_dur = []
    for a, b in moves:
        L = 0.0
        for i in range(max(a, 1), b):
            L += math.hypot(P[i][0] - P[i - 1][0], P[i][2] - P[i - 1][2])
        if b - a < 3:
            continue
        seg_len.append(L); seg_dur.append((b - a) * DT)
        seg_chord.append(math.hypot(P[b - 1][0] - P[a][0], P[b - 1][2] - P[a][2]))
    res['segments'] = len(seg_len)
    res['seg_len_med'] = st.median(seg_len) if seg_len else 0
    res['seg_len_p25'] = pct(seg_len, .25)
    res['seg_len_p75'] = pct(seg_len, .75)
    res['seg_len_max'] = max(seg_len) if seg_len else 0
    res['seg_straightness_med'] = st.median([c / l for c, l in zip(seg_chord, seg_len) if l > 1]) if seg_len else 0
    res['path_total_m'] = sum(seg_len)
    mv = [S[i] for i in range(n) if S[i] >= STILL]
    res['speed_med'] = st.median(mv) if mv else 0
    res['speed_p10'] = pct(mv, .1); res['speed_p90'] = pct(mv, .9)
    # ---- revisits -----------------------------------------------------------------------------
    cents = []
    for a, b in stops:
        xs = [P[i][0] for i in range(a, b)]; zs = [P[i][2] for i in range(a, b)]
        cents.append((sum(xs) / len(xs), sum(zs) / len(zs), (b - a) * DT))
    revisit = 0; aba = 0
    for k in range(len(cents)):
        x, z, _ = cents[k]
        if any(math.hypot(x - cents[j][0], z - cents[j][1]) < REVISIT_R for j in range(0, k - 1)):
            revisit += 1
        if k >= 2 and math.hypot(x - cents[k - 2][0], z - cents[k - 2][1]) < REVISIT_R:
            aba += 1
    res['stop_revisits'] = revisit
    res['stop_ABA'] = aba
    # spatial spread
    xs = [p[0] for p in P]; zs = [p[2] for p in P]
    res['bbox_m'] = (max(xs) - min(xs), max(zs) - min(zs))
    cells = defaultdict(float)
    for p in P:
        cells[(int(math.floor(p[0] / 5)), int(math.floor(p[2] / 5)))] += DT
    res['cells_5m'] = len(cells)
    res['top3_cells_time_frac'] = sum(sorted(cells.values(), reverse=True)[:3]) / (n * DT)
    # ---- along-path grade --------------------------------------------------------------------
    grades = []; glen = 0.0; steep15 = 0.0; steep25 = 0.0
    for a, b in moves:
        i = a
        while i < b:
            j = i
            while j < b - 1 and math.hypot(P[j][0] - P[i][0], P[j][2] - P[i][2]) < 2.0:
                j += 1
            h = math.hypot(P[j][0] - P[i][0], P[j][2] - P[i][2])
            if h >= 1.0:
                g = math.degrees(math.atan2(P[j][1] - P[i][1], h))
                grades.append(g); glen += h
                if abs(g) > 15: steep15 += h
                if abs(g) > 25: steep25 += h
            i = j + 1 if j > i else i + 1
    res['grade_abs_med_deg'] = st.median([abs(g) for g in grades]) if grades else 0
    res['grade_abs_p90_deg'] = pct([abs(g) for g in grades], .9)
    res['grade_abs_max_deg'] = max([abs(g) for g in grades]) if grades else 0
    res['path_frac_gt15deg'] = steep15 / glen if glen else 0
    res['path_frac_gt25deg'] = steep25 / glen if glen else 0
    res['_stops'] = stops; res['_cents'] = cents; res['_turns'] = turns_at_stops
    res['_P'] = P; res['_Y'] = Y; res['_S'] = S; res['_T'] = T
    return res


def table(rows, cols, title):
    print('\n## ' + title)
    print('| entity | ' + ' | '.join(c for c, _ in cols) + ' |')
    print('|---' * (len(cols) + 1) + '|')
    for r in rows:
        cells = []
        for c, f in cols:
            v = r[c]
            cells.append(f.format(*v) if isinstance(v, tuple) else f.format(v))
        print('| ' + r['name'] + ' | ' + ' | '.join(cells) + ' |')


if __name__ == '__main__':
    R = {n: analyse(n) for n in ALIENS + ANIMALS}
    rows = [R[n] for n in ALIENS + ANIMALS]
    table(rows, [('stationary_frac', '{:.0%}'), ('longest_still_s', '{:.1f}'), ('stops', '{}'),
                 ('stops_ge5s', '{}'), ('stops_ge8s', '{}'), ('still_time_in_ge5s', '{:.0f}'),
                 ('stop_dur_med', '{:.1f}'), ('stop_dur_p90', '{:.1f}')], 'Stationary')
    table(rows, [('stop_turns', '{}'), ('reversals', '{}'), ('reversals_stop_le6s', '{}'),
                 ('turns_at_stops_gt90', '{}'), ('turn_at_stop_med_deg', '{:.0f}'),
                 ('reversals_moving', '{}')], 'Reversals (heading in vs out of a stop, >150 deg)')
    table(rows, [('yaw_total_deg', '{:.0f}'), ('yaw_near0_frac', '{:.0%}'), ('yaw_still_frac', '{:.0%}'),
                 ('max_yaw_rate_still_dps', '{:.0f}'), ('turn_activity_frac', '{:.1%}'),
                 ('pivot_at_stop_med_deg', '{:.0f}'), ('moving_turn_samples', '{}'),
                 ('moving_turn_radius_med_m', '{:.1f}')], 'Pivoting')
    table(rows, [('segments', '{}'), ('seg_len_med', '{:.1f}'), ('seg_len_p25', '{:.1f}'), ('seg_len_p75', '{:.1f}'),
                 ('seg_len_max', '{:.1f}'), ('seg_straightness_med', '{:.2f}'), ('path_total_m', '{:.0f}'),
                 ('speed_p10', '{:.2f}'), ('speed_med', '{:.2f}'), ('speed_p90', '{:.2f}')], 'Segments and speed')
    table(rows, [('stop_revisits', '{}'), ('stop_ABA', '{}'), ('bbox_m', '{:.0f}x{:.0f}'), ('cells_5m', '{}'),
                 ('top3_cells_time_frac', '{:.0%}')], 'Revisits')
    table(rows, [('grade_abs_med_deg', '{:.1f}'), ('grade_abs_p90_deg', '{:.1f}'), ('grade_abs_max_deg', '{:.1f}'),
                 ('path_frac_gt15deg', '{:.0%}'), ('path_frac_gt25deg', '{:.0%}')], 'Along-path grade')
    json.dump({k: {kk: vv for kk, vv in v.items() if not kk.startswith('_')} for k, v in R.items()},
              open('/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/audit/measure.json', 'w'), indent=1)
