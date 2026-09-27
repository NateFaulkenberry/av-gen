#!/usr/bin/env python3
"""Which shots have said their piece: trims proposed from the Critic's novelty measure.

    python3 tools/gv3/trim.py ~/.creative-critic/jobs/<job>/report.json [--min-held 0.5]

The Critic measures, per shot, when it last showed new information (`measurements.novelty`: the
frame-to-earlier-frames distance of its layout descriptors, threshold 0.12). A shot that holds past
that point is a candidate to end earlier. This proposes, for `songcut.TRIMS`, how many beats each
such shot should lose, and says why a candidate is left alone:

  * it ends on a section boundary (the Director's cut puts every boundary on its downbeat, and a
    trim would move the boundary's cut off it -- or the drop's);
  * it is already a bar or shorter (the Director's shortest units: the riser's compression, the
    arrival's half bars -- a held frame is the point there);
  * trimming would leave it under a bar, or it holds less than a beat past its last new information.

The trim ends the shot on the first beat at or after `last new information + half a beat`, so the
cut comes just after the picture has finished changing. It never lengthens a shot.
"""
import argparse
import json
import math
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from gv3 import music, songcut  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("report")
    ap.add_argument("--min-held", type=float, default=music.BEAT,
                    help="seconds held after the last new information before a shot is a candidate")
    args = ap.parse_args()
    report = json.loads(pathlib.Path(args.report).read_text())
    novelty = report["measurements"]["novelty"]["shots"]
    spans = songcut.spans(json.loads(songcut.RECORD.read_text()))
    by_sid = {f"s{i + 1:02d}": s for i, s in enumerate(spans)}
    proposals, kept = {}, []
    for sid, n in sorted(novelty.items()):
        span = by_sid.get(sid)
        if span is None:
            continue
        held = n["held_after_s"]
        if held < args.min_held:
            continue
        why = None
        if span.ends_on == "section":
            why = "ends on its section's boundary"
        elif span.beats <= 4.0 + 1e-6:
            why = "a bar or shorter already"
        else:
            keep_until = n["last_new_information_s"] + 0.5 * music.BEAT
            beats = math.ceil(keep_until / music.BEAT - 1e-6)
            beats = max(beats, 4)
            trim = int(round(span.beats)) - beats
            if trim < 1:
                why = "less than a beat to take"
            else:
                proposals[span.label] = trim
                print(f"{sid} [{span.label}] {span.duration:5.2f} s, last new information at "
                      f"{n['last_new_information_s']:.2f} s, held {held:.2f} s -> trim {trim} beat(s), "
                      f"ends after {beats} beats")
        if why:
            kept.append(f"{sid} [{span.label}] held {held:.2f} s of {span.duration:.2f}: kept, {why}")
    for line in kept:
        print(line)
    print("TRIMS =", json.dumps(proposals))


if __name__ == "__main__":
    main()
