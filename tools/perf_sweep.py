#!/usr/bin/env python3
"""Headless frame-cost sweep over shots, scenes, binaries and arms (QA pass W1).

Every run is one `avgen --headless ... --bench-json` process under tools/gpu-lock.sh, started at a
film second (`--range t:`), so it measures the frames the engine's own benchmark measures: the
median of the per-frame wall clock after its 12-frame warm-up, with the CPU and GPU splits, the GPU
pass medians and the workload counters from the same window. Runs are interleaved (every arm of a
round runs before the next round starts) so drift lands on every arm alike, and each (arm, point)
keeps all its repeats so the spread is reported beside the median.

It proves the state it claims to measure where the log lets it: the camera label the engine
printed for the start second is recorded next to the shot it was meant to be, and the run's GPU
error count and load errors are recorded. Read them.

    # every shot of a project's cut, 3 repeats
    python3 tools/perf_sweep.py shots examples/world/_qa-gv3-r7b.json --size 640x360 --reps 3 --out sweep.json
    # named points (project@second), several arms (binary and/or extra args)
    python3 tools/perf_sweep.py points "gv3=examples/world/_qa-gv3-r7b.json@120" \
        --arm base=build/release/src/avgen --arm nowater="build/release/src/avgen --disable water" --out ab.json
    # summarise a result file as a markdown table
    python3 tools/perf_sweep.py report sweep.json
"""
import argparse, json, os, re, shlex, statistics, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BIN = os.path.join(REPO, 'build', 'release', 'src', 'avgen')


def scene_of(project_path):
    p = json.load(open(project_path))
    ref = p.get('assets', {}).get('scene', {}).get('path')
    ref = ref.get('path') if isinstance(ref, dict) else ref
    return json.load(open(os.path.join(os.path.dirname(project_path), ref)))


def shot_points(project_path, frames, fps):
    """One point per shot of the scene's cut, started where the whole measured window fits inside
    the shot when it can (warm-up included), else at the shot's start."""
    s = scene_of(project_path)
    cd = s.get('cameraDirection', {})
    cams = {c['id']: c.get('name', '') for c in cd.get('cameras', [])}
    window = frames / float(fps)
    out = []
    for i, sh in enumerate(cd.get('shots', [])):
        a, b = float(sh['start']), float(sh['end'])
        t = a + 0.05 if b - a < window + 0.1 else a + min(0.25 * (b - a), (b - a) - window - 0.05)
        out.append({'name': f'shot{i + 1:02d}', 'project': project_path, 'second': round(t, 3),
                    'shotStart': a, 'shotEnd': b, 'fitsWindow': (b - a) >= window + 0.1,
                    'label': sh.get('label', ''), 'camera': cams.get(sh.get('camera'), '')})
    return out


def parse_point(spec):
    """`name=project@second`, or `name[armA,armB]=project@second` to run only those arms on it --
    which is how a historical binary is paired with its own commit's copy of a project."""
    name, rest = spec.split('=', 1)
    only = None
    if name.endswith(']') and '[' in name:
        name, only = name[:-1].split('[', 1)
        only = only.split(',')
    path, _, sec = rest.partition('@')
    return {'name': name, 'project': path, 'second': float(sec or 0), 'arms': only}


def run_one(arm_cmd, point, size, frames, fps, tmpdir, extra):
    bench = os.path.join(tmpdir, f"b-{os.getpid()}-{time.time_ns()}.json")
    argv = shlex.split(arm_cmd)
    kind = '--composition' if point['project'].endswith('.scene.json') else '--project'
    cmd = [os.path.join(REPO, 'tools', 'gpu-lock.sh')] + argv + [
        '--headless', kind, point['project'], '--frames', str(frames), '--fps', str(fps), '--size', size,
        '--range', f"{point['second']}:", '--log', 'info', '--bench-json', bench] + extra
    # The contention check, per run: other avgen processes (any worktree) and the 1-minute load
    # average, at the start and the end. A run taken beside another GPU or CPU job is not evidence
    # (tools/gpu-lock.sh); these fields let a reader find and re-run those.
    def others():
        ps = subprocess.run(['ps', '-Ao', 'pid,args'], capture_output=True, text=True).stdout
        return sorted({l.split()[1].rsplit('/', 1)[-1] for l in ps.splitlines()[1:]
                       if ('avgen' in l.split()[1] if len(l.split()) > 1 else False)})
    load0, others0 = os.getloadavg()[0], others()
    t0 = time.time()
    proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    load1, others1 = os.getloadavg()[0], others()
    log = proc.stdout + proc.stderr
    rec = {'rc': proc.returncode, 'processSeconds': round(time.time() - t0, 2),
           'load': [round(load0, 1), round(load1, 1)], 'otherAvgen': sorted(set(others0) | set(others1))}
    m = re.findall(r'camera: (.*?) \((shot|[a-z ]+)\) at ([0-9.]+) s', log)
    rec['cameraLog'] = [f'{a} ({b}) at {c}' for a, b, c in m][:4]
    m = re.search(r'GPU errors: (\d+)', log)
    rec['gpuErrors'] = int(m.group(1)) if m else None
    rec['errorLines'] = [l[:200] for l in log.splitlines() if '[error]' in l][:10]
    m = re.findall(r'cpu\(scene\)=([0-9.]+)ms', log)
    rec['engineUpdateMsLastFrame'] = float(m[-1]) if m else None
    # Cumulative over the run (the headless loop never clears probe2): first and last logged frame.
    m = re.findall(r'sightlines=(\d+)/(\d+)m', log)
    if m:
        rec['sightlinesFirstLast'] = [[int(a), int(b)] for a, b in (m[0], m[-1])]
    try:
        doc = json.load(open(bench))
        r = doc['records'][0]
        rec['wall'] = {k: r['wallMs'].get(k) for k in ('p50', 'p95', 'p99', 'min', 'max')}
        rec['cpu'] = {k: r['cpuFrameMs'].get(k) for k in ('p50', 'p95', 'p99')}
        rec['gpu'] = {k: r['gpuMs'].get(k) for k in ('p50', 'p95', 'p99')}
        rec['passes'] = r.get('gpuPassMedianMs')
        rec['cpuStages'] = r.get('cpuStageMedianMs')
        rec['counters'] = r.get('counters')
        os.remove(bench)
    except Exception as exc:  # a crashed run has no record; say so rather than skip it
        rec['benchError'] = str(exc)
        rec['logTail'] = log.splitlines()[-15:]
    return rec


def sweep(points, arms, reps, size, frames, fps, extra, out_path):
    tmpdir = os.environ.get('TMPDIR', '/tmp')
    results = {'size': size, 'frames': frames, 'fps': fps, 'reps': reps, 'extra': extra,
               'arms': arms, 'points': points, 'runs': []}
    for rep in range(reps):
        for pt in points:
            for arm, cmd in arms.items():
                if pt.get('arms') and arm not in pt['arms']:
                    continue
                rec = run_one(cmd, pt, size, frames, fps, tmpdir, extra)
                rec.update({'rep': rep, 'point': pt['name'], 'arm': arm})
                results['runs'].append(rec)
                w = rec.get('wall', {}).get('p50')
                g = rec.get('gpu', {}).get('p50')
                print(f"rep{rep} {pt['name']:>10} {arm:>10} wall={w} gpu={g} upd={rec.get('engineUpdateMsLastFrame')} "
                      f"cam={rec.get('cameraLog', [''])[:1]}", flush=True)
                json.dump(results, open(out_path, 'w'), indent=1)
    return results


def summarise(results):
    rows = {}
    for r in results['runs']:
        rows.setdefault((r['point'], r['arm']), []).append(r)
    out = []
    for (pt, arm), rs in rows.items():
        def med(path):
            v = [x for x in (path(r) for r in rs) if x is not None]
            return (statistics.median(v), min(v), max(v)) if v else (None, None, None)
        wall = med(lambda r: r.get('wall', {}).get('p50'))
        cpu = med(lambda r: r.get('cpu', {}).get('p50'))
        gpu = med(lambda r: r.get('gpu', {}).get('p50'))
        wall95 = med(lambda r: r.get('wall', {}).get('p95'))
        wall99 = med(lambda r: r.get('wall', {}).get('p99'))
        upd = med(lambda r: r.get('engineUpdateMsLastFrame'))
        c = rs[-1].get('counters') or {}
        info = next((p for p in results['points'] if p['name'] == pt), {})
        out.append({'point': pt, 'arm': arm, 'n': len(rs), 'wallP50': wall, 'wallP95': wall95[0],
                    'wallP99': wall99[0], 'cpuP50': cpu[0], 'gpuP50': gpu, 'engineUpdate': upd[0],
                    'fps': 1000.0 / wall[0] if wall[0] else None,
                    'spreadPct': 100.0 * (wall[2] - wall[1]) / wall[0] if wall[0] else None,
                    'draws': c.get('draws'), 'tris': c.get('submittedTriangles'),
                    'visibleInstances': c.get('visibleInstances'), 'shadowDraws': c.get('shadowDraws'),
                    'lights': c.get('clusteredLights'), 'entities': c.get('entities'),
                    'label': info.get('label', ''), 'second': info.get('second'),
                    'camera': rs[-1].get('cameraLog', [''])[:1],
                    'gpuErrors': sorted({r.get('gpuErrors') for r in rs}, key=str),
                    'passes': (rs[-1].get('passes') or [])[:6]})
    return out


def report(results):
    rows = summarise(results)
    f = lambda v, d=1: '-' if v is None else f'{v:.{d}f}'
    print(f"size {results['size']}, {results['frames']} frames @ {results['fps']} (12 warm-up), "
          f"{results['reps']} repeats; ms are medians over repeats of each run's p50")
    print('| point | arm | second | wall p50 (min-max) | FPS | spread % | cpu | gpu | engine update | draws | tris | vis inst | label |')
    print('|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|')
    for r in rows:
        w = r['wallP50']
        print(f"| {r['point']} | {r['arm']} | {f(r['second'], 2)} | {f(w[0])} ({f(w[1])}-{f(w[2])}) | {f(r['fps'])} | "
              f"{f(r['spreadPct'], 0)} | {f(r['cpuP50'])} | {f(r['gpuP50'][0])} | {f(r['engineUpdate'])} | "
              f"{r['draws']} | {r['tris']} | {r['visibleInstances']} | {r['label'][:48]} |")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='mode', required=True)
    for mode in ('shots', 'points'):
        p = sub.add_parser(mode)
        p.add_argument('targets', nargs='+', help='shots: a project; points: name=project@second')
        p.add_argument('--arm', action='append', default=[], help='name=command (binary plus args)')
        p.add_argument('--reps', type=int, default=3)
        p.add_argument('--size', default='640x360')
        p.add_argument('--frames', type=int, default=42)
        p.add_argument('--fps', type=int, default=30)
        p.add_argument('--only', help='shots: comma-separated shot numbers (1-based)')
        p.add_argument('--extra', default='', help='extra avgen args for every run')
        p.add_argument('--out', required=True)
    r = sub.add_parser('report')
    r.add_argument('file')
    a = ap.parse_args()
    if a.mode == 'report':
        report(json.load(open(a.file)))
        return 0
    arms = dict(x.split('=', 1) for x in a.arm) or {'base': DEFAULT_BIN}
    if a.mode == 'shots':
        points = shot_points(a.targets[0], a.frames, a.fps)
        if a.only:
            keep = {int(x) for x in a.only.split(',')}
            points = [p for i, p in enumerate(points) if i + 1 in keep]
    else:
        points = [parse_point(x) for x in a.targets]
    res = sweep(points, arms, a.reps, a.size, a.frames, a.fps, shlex.split(a.extra), a.out)
    report(res)
    return 0


if __name__ == '__main__':
    sys.exit(main())
