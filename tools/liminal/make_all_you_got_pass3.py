#!/usr/bin/env python3
"""Generates examples/liminal/all-you-got-pass3{,.scene,.rig}.json: the art pass 3 music video for the owner's song
*All You Got* (docs/prototypes/liminal-space/03-art-pass-3-addendum.md governs; pass 2 is kept reproducible by its
own generator).

A neighbourhood builds itself out of the dark; we go in through the lit window of one house on the downbeat. Its
rooms are where a mannequin sits, lies and stands, never moving while we look, in a new place each time we look
back. We climb the stairs with the words on them and step off the top into nothing; land in the basement among what
the house cannot let go of; go up into the breaking rooms upstairs; the roof flies off and the house grows into a
tree of rooms; a room of objects asks IS THAT ALL YOU GOT?; the house dances, a wave of colour runs through it, the
door opens onto a line-drawn landscape that celebrates; it crashes, dawn comes, and it ends on the road's four
dashes in the dark where it began.

    python3 tools/liminal/make_all_you_got_pass3.py                 # the film, validated
    python3 tools/liminal/make_all_you_got_pass3.py --no-validate   # without the spatial validator

The checks it prints: words not seen when they appear (must be 0), words without a clear wall, the figures' spans
and swaps (each swap with both figures off screen), and the spatial validator's summary (section 15's workflow:
generate, validate, fix, render, critic, refine, validate again).
"""

from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import kit as K  # noqa: E402
import liminal_space as ls  # noqa: E402

ls.instrument_kit(K)

import film3 as F3  # noqa: E402
import film3_house  # noqa: E402
import film3_intro  # noqa: E402
import film3_late  # noqa: E402
import film3_upper  # noqa: E402
import make_all_you_got_pass2 as P2  # noqa: E402
import pass2_grid as G  # noqa: E402
import world3 as W  # noqa: E402
from film2 import Film  # noqa: E402

OUT = os.path.join(ROOT, "examples", "liminal")
STEM = "all-you-got-pass3"
END = 258.0
RULES = os.path.join(HERE, "space-rules3.json")
t = G.t
BEAT1 = G.BAR1 / 4.0

PARAMS = dict(P2.PARAMS)
PARAMS.update({"camera/breath/amount": 0.0})


def merge_replace_tracks(film):
    """One replace track per target (a second replace track would hold its first value over everything before it):
    merge the keys of every replace track on the same target and component, in time order."""
    out, seen = [], {}
    for tr in film.tracks:
        if tr["mode"] != "replace":
            out.append(tr)
            continue
        key = (tr["target"], tr["component"])
        if key in seen:
            first = seen[key]
            times = {k["time"] for k in first["keys"]}
            for k in tr["keys"]:
                if k["time"] in times:
                    raise RuntimeError(f"two replace tracks key {key} at {k['time']}")
            first["keys"] = sorted(first["keys"] + tr["keys"], key=lambda k: k["time"])
        else:
            seen[key] = tr
            out.append(tr)
    film.tracks = out


def build(end=END, validate=True):
    film = Film(end=end)
    P2.palettes(film)
    base = [("post/bloom/intensity", 0.35), ("camera/exposure/compensation", 0.0), ("post/grade/hueShift", 0.0),
            ("post/lens/chromaticAberration", 0.0), ("post/lens/distortion", 0.0), ("temporal/mosh/amount", 0.0),
            ("temporal/mosh/shift", 0.0), ("post/sweep/intensity", 0.0), ("post/sweep/wash", 0.0), ("palette/saturation", 1.0),
            ("camera/breath/amount", 0.0)]
    for target, v in base:
        film.track(target, [(0.0, v), (end, v)])
    b = F3.Builder3(film, W.add_world, P2.PALETTE_INDEX)
    # the palette's slow voice
    for tt, name, ramp in ((0.0, "P0boot", 0.0), (t(9), "P1compile", 0.0), (t(17), "P2allyougot", 0.0), (t(25), "P3night", 0.0),
                           (t(32, 4), "P4ember", BEAT1), (t(41), "P5void", 0.0), (t(41, 4.5), "P6violet", BEAT1 * 0.5),
                           (t(49, 4), "P7tension", 0.0), (t(66), "P8growth", BEAT1), (t(75), "P9jewels", 0.0), (t(83), "P10dance", 0.0)):
        b.palette_at(tt, name, ramp=ramp)
    film3_intro.build(b)
    film3_house.build(b)
    film3_upper.build(b)
    # the late section's palette (after 90.3 the wave sets P11; then the summit, the dawn)
    film3_late.build(b)
    b.palette_at(t(91), "P11open")
    b.palette_at(t(107), "P12summit", ramp=G.BAR2)
    b.palette_at(t(114), "P13dawn", ramp=G.BAR2 * 2.0)
    # a tear at every scene change that has no corruption of its own (section 21: transitions, scene changes)
    covered = (t(17), t(42), t(49, 4), t(53, 4), t(57, 4), 255.0)
    for i, tc in enumerate(sorted(set(b.cuts))):
        if tc <= 0.0 or any(abs(tc - c) < 0.2 for c in covered):
            continue
        cl = b.clap(f"cut{i:02d}", tc, release=0.14)
        film.route(cl, "temporal/mosh/amount", 0.32)
        film.route(cl, "temporal/mosh/shift", 9.0)
        film.route(cl, "post/lens/chromaticAberration", 0.35)
    b.breath_forbidden()
    b.write_figures(end)
    b.stamp_figure_spans()
    film.track("palette/position", sorted(b.pal, key=lambda k: k[0]) + [(end, b.pal[-1][1], "step")])
    film.track("palette/value", sorted(b.val, key=lambda k: k[0]) + [(end, 1.0)])
    mb = [(0.0, 0.35, "step")]
    for tc in sorted(set(b.cuts)):
        if tc <= 0.0:
            continue
        mb += [(tc - 0.002, 0.35, "step"), (tc - 0.001, 0.0, "step"), (tc + 0.045, 0.35, "step")]
    film.track("post/motionBlur/amount", mb)
    P2.style_fonts(b.words)
    bad = film.check_words(b.words)
    print(f"words placed without a clear wall and line of sight: {getattr(b, 'unplaced', [])}")
    print(f"words not seen when they appear: {len(bad)} of {len(b.words)}")
    for row in bad:
        print("  ", row)
    from liminal_text import place_words
    out = place_words(b.words, grid="song")
    film.nodes += out["nodes"]
    film.events += out["events"]
    film.routes += out["routes"]
    film.bindings += out["bindings"]
    merge_replace_tracks(film)
    print("figures:")
    print("\n".join(b.report_figures()))
    path = film.write(OUT, STEM, P2.GRID, P2.environment(stem=STEM), PARAMS)
    print(f"wrote {path}: {len(film.shots)} shots, {len(film.nodes)} nodes, {len(film.routes)} routes, "
          f"{len(film.events)} grid events, {len(b.words)} words, {len(film.lights)} lights")
    if validate:
        rep = ls.validate(path, rules=json.load(open(RULES)))
        with open(os.path.join(OUT, f"{STEM}.validation.txt"), "w") as fh:
            fh.write(ls.text(rep))
        s = rep["summary"]
        print(f"spatial validator: {s['errors']} errors, {s['warnings']} warnings; pass {s['pass']}")
        print(f"  the report: examples/liminal/{STEM}.validation.txt")
    return path, b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--end", type=float, default=END)
    ap.add_argument("--no-validate", action="store_true")
    a = ap.parse_args()
    build(a.end, validate=not a.no_validate)


if __name__ == "__main__":
    main()
