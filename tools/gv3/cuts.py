#!/usr/bin/env python3
"""Every cut of a render as a pair of frames -- the last before it and the first after it -- so the
edit can be judged for eye-trace and geography (research principle 16: a beat-perfect cut that
breaks either is still a bad cut).

    python3 tools/gv3/cuts.py build/gv3/v0.mov build/gv3/shots.json build/gv3/review-v0
"""
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from contact_sheet import compose, frame_at  # noqa: E402

PER_SHEET = 14
OFFSET = 0.034  # two frames either side of the cut


def main():
    video, shots_json, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    shots = json.loads(shots_json.read_text())["shots"]
    cuts = [(a, b) for a, b in zip(shots, shots[1:])]
    for k in range(0, len(cuts), PER_SHEET):
        cells, titles = [], []
        for a, b in cuts[k:k + PER_SHEET]:
            t = float(b["start"])
            titles.append(f"{a['id']} -> {b['id']}  cut at {t:.2f}s  ({a.get('label', '')[:28]} | {b.get('label', '')[:28]})")
            cells.append((frame_at(video, t - OFFSET), f"{t - OFFSET:.2f}s"))
            cells.append((frame_at(video, t + OFFSET), f"{t + OFFSET:.2f}s"))
        compose(cells, 2, 330, titles).save(out / f"cuts-{k // PER_SHEET + 1:02d}.png")
    print(f"{len(cuts)} cuts -> {out}")


if __name__ == "__main__":
    main()
