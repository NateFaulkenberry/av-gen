#!/usr/bin/env python3
"""Drum recall of the live response model (response.kick/snare/hat, ADR-1060/1062) on the test material's drum loop,
alone and under each other part of the full mix -- the measurement behind the art agent's report that kicks vanish
under a pad bed.

    python3 tools/sonic_vfx/drum_recall.py [--avgen PATH]

Renders five mixes of `make_test_material.perf_drumloop` (32 kicks four-on-the-floor, 16 snares or claps on the
backbeat, 16th hats) to assets/audio/sonic-vfx-dr-*.wav (gitignored): the drums alone, drums + lead, drums + bass,
drums + pads (at the full mix's -10 dB) and the full mix. Each goes through the engine's `--sonic-trace` (no GPU), and
a hit counts as found when its envelope (`response.<class>Env`) passes 0.3 within -30..+90 ms of the placed hit.
"""
import argparse
import csv
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review, variant  # noqa: E402

spec = importlib.util.spec_from_file_location("mtm", os.path.join(HERE, "make_test_material.py"))
mtm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mtm)


def mixes():
    pads, _, _ = mtm.perf_pads()
    bass, _, _ = mtm.perf_bass()
    lead, _, _ = mtm.perf_lead()
    _, hits, _ = mtm.perf_drumloop()
    lead = [(b + 4.0, ln, p, v, vo) for b, ln, p, v, vo in lead if b + 4.0 < 30.0]
    pads = [(b, ln, p, int(v * 0.45), vo) for b, ln, p, v, vo in pads if b < 30.0]
    return hits, {"drums": [], "+lead": lead, "+bass": bass, "+pads": pads, "full": pads + bass + lead}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--avgen", default=review.AVGEN)
    ap.add_argument("--project", default=os.path.join(REPO, "examples", "sonic-vfx", "salt-flat-mirage.json"))
    a = ap.parse_args()
    hits, ms = mixes()
    work = tempfile.mkdtemp(prefix="drum-recall-")
    print("| mix | kick | snare/clap | hat |")
    print("|---|---|---|---|")
    for name, notes in ms.items():
        L, R = mtm.render(notes, hits, 32.0)
        wav = os.path.join(REPO, "assets", "audio", "sonic-vfx-dr-%s.wav" % name.strip("+"))
        mtm.msm.write_wav(wav, L, R)
        proj = os.path.join(work, name.strip("+") + ".json")
        variant.make_variant(a.project, wav, None, proj)
        out = os.path.join(work, name.strip("+") + ".csv")
        subprocess.run([a.avgen, "--project", proj, "--sonic-trace", out], capture_output=True)
        r = csv.reader(open(out))
        h = next(r)
        rows = [list(map(float, x)) for x in r if x]
        T = [x[0] for x in rows]
        cells = []
        for kind in ("kick", "snare", "hat"):
            col = [x[h.index("response.%sEnv" % kind)] for x in rows]
            truth = [b * mtm.BEAT for (b, k, v) in hits if k == kind or (kind == "snare" and k == "clap")]
            found = sum(any(t - 0.03 <= tt <= t + 0.09 and col[i] > 0.3 for i, tt in enumerate(T)) for t in truth)
            edges = [T[i] for i in range(1, len(T)) if col[i] > 0.3 and col[i - 1] <= 0.3]
            false = sum(1 for e in edges if not any(t - 0.03 <= e <= t + 0.09 for t in truth))
            cells.append("%d/%d (+%d)" % (found, len(truth), false))
        print("| %s | %s |" % (name, " | ".join(cells)))


if __name__ == "__main__":
    main()
