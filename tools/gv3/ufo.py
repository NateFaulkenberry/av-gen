#!/usr/bin/env python3
"""The UFO activity of Glowmere Valley 3: five set pieces, compiled into the project by the engine.

    python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py
    python3 tools/gv3/ufo.py --no-trace     # compile only; the moments are the plan's nominal ones

The owner's brief §9: "UFO activity is part of the world", not "the video is about one UFO scene";
more events, varied in place, framing, scale, distance, timing, animal count and relation to the
music, building to the riser's centrepiece. The five are a Director plan in the engine's own format,
`ufo.plan.json` beside this file (revision plan §3, stream-reports/setpieces.md):

  E1  survey     the scout, far up the valley: a thin beam sweeps a field and lifts nothing, and it
                 leaves on bar 13, the riff's phrase end
  E2  flyby      the saucer crosses the sky in the first pull-back (bar 15 + 0.3 s)
  E3  abduction  the scout lifts one animal far up the valley, at the top of the lift's crane (bar 37)
  E4  abduction  the scout lifts two animals together in the meadow west of the river (bar 57): the
                 two cows it finds there, named (`east-cow` cow-12 first, `west-cow` cow-23), because
                 the engine lays a pair out along +x/-x in the order it takes them, and taking the
                 western cow first sent each across the other's path mid-lift -- one body through the
                 other at 107.0 s, the owner's "two cows stuck together, abducted as one" (gv3-int
                 r2b) -- and 4 m apart in height, since 57.1 looks along that axis and sees them one
                 above the other
  E5  abduction  the centrepiece: the saucer takes the elder's own horse -- the beam on bar 93, the
                 horse gone on the drop, its gold glow riding the lift (two cues on E5's lift)

This is the generator's last step, run after `make_glowmere_valley_3.py` has written the project:

 1. **Compile.** `avgen_cast_trace --plan --save-project` compiles the plan against the project exactly
    as the Director installs an approved plan (ADR-929), and saves it to a scratch file.
 2. **Take what the plan made, and nothing else.** A headless save writes the whole run back: every
    registered parameter's value as it stands (745 more of them, rounded to float), the scene's value
    where the engine never applied the project's, a clamped value where the project's was out of range.
    None of that is the plan's. So only what the plan produced is taken into the generator's project:
    `staging` (the set pieces' scenarios, beside the scene's actors), `directingPlans` (the plan and
    the fingerprints of what it made, which the Director panel's "UFO set pieces" rows edit), and the
    plan's markers and cue events in `sequence`. Everything else stays byte for byte as generated.
 3. **Trace the project that renders.** The spliced project is loaded afresh and the whole film played
    on the CPU at the render's 60 fps, and `build/gv3/ufo-beats.json` records what every set piece
    actually did -- each moment's time, where the craft was, the animals taken, how still the craft
    held -- and which aliens stood watching each beam, when (`watches`), for the cut (framing,
    reaction shots, Song Mode's peaks) and the evaluator. `--trace-file` rebuilds it from a trace
    already taken of the compiled project. The raw trace is
    `build/gv3/cast-ufo.json`, the cast's whole-film trace for the ADR-910 gate.

It refuses, loudly: a plan item the engine could not build (the trace tool's exit 2), a set piece that
did not happen (no beam, an animal not taken), and a centrepiece off its music (the beam off bar 93's
frame, the horse not gone within a frame of the drop).
"""

import argparse
import json
import math
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
if __package__:
    from . import music
else:  # run as a script
    sys.path.insert(0, str(HERE.parent))
    from gv3 import music  # noqa: E402
PLAN = HERE / "ufo.plan.json"
PROJECT = ROOT / "examples" / "world" / "glowmere-valley-3.json"
TOOL = ROOT / "build" / "release" / "tools" / "avgen_cast_trace"
BUILD = ROOT / "build" / "gv3"
TRACE = BUILD / "cast-ufo.json"
BEATS = BUILD / "ufo-beats.json"
FILM_SECONDS = 226.0
FPS = 60.0

# The beats of a set piece a viewer does not see (stage/setpiece.hpp): `rest` hides the craft at t = 0,
# `transit` is its hidden move to where it appears, `hover` the hold before the beam.
HIDDEN_BEATS = ("rest", "transit", "hover")
# What a set piece must have done for the film to be the one the plan asked for.
EXPECTED_ANIMALS = {"e3-far-lift": 1, "e4-river-pair": 2, "e5-centrepiece": 1}

# The aliens' answers to the beams they hear (cast.py's REACTIONS), measured for the cut: a reaction
# shot ("Vane sees it") has to be where an alien actually stands watching. A watch is the body
# standing (ADR-910's still, under 0.1 m/s) and facing the craft within WATCH_FACING degrees, for at
# least WATCH_MIN seconds, in the WATCH_AFTER seconds after the beam lights (cast.EVENT_MEMORY).
ALIENS = ("rook", "tide", "sage", "ember", "vane")
WATCH_FACING = 15.0
WATCH_STILL = 0.1
WATCH_MIN = 0.5
WATCH_AFTER = 20.0


def event_name(key, moment):
    """The world and bus event a set piece raises at a moment (ADR-930): `setpiece/<key>/<moment>`."""
    return f"setpiece/{key}/{moment}"


def plan():
    return json.loads(PLAN.read_text())


def dump(obj):
    return json.dumps(obj, indent=2) + "\n"


# ---- 1. compile --------------------------------------------------------------------------------------
def compile_plan(project_path, scratch):
    """The plan compiled against the project and saved to `scratch`; returns the plan report."""
    report_trace = scratch.with_suffix(".report.json")
    cmd = [str(TOOL), "--project", str(project_path), "--plan", str(PLAN), "--save-project", str(scratch),
           "--seconds", "0.05", "--fps", f"{FPS:g}", "--hz", f"{FPS:g}", "--out", str(report_trace)]
    run = subprocess.run(cmd, capture_output=True, text=True)
    if run.returncode not in (0, 2) or not report_trace.exists():
        raise RuntimeError(f"avgen_cast_trace --plan failed ({run.returncode}):\n{run.stderr[-4000:]}")
    report = json.loads(report_trace.read_text()).get("plan", {})
    report_trace.unlink()
    if run.returncode == 2 or report.get("blocked"):
        raise RuntimeError("the UFO plan has items the engine could not build: "
                           f"{report.get('blocked')}\n{json.dumps(report.get('issues'), indent=1)}")
    return report


# ---- 2. splice ---------------------------------------------------------------------------------------
def splice(project, saved):
    """Take what the plan produced from the saved project into the generator's (see the module doc)."""
    plans = [p for p in saved.get("directingPlans", []) if p.get("id") == plan()["id"]]
    if len(plans) != 1:
        raise RuntimeError(f"the saved project carries {len(plans)} plan(s) named '{plan()['id']}'")
    produced = plans[0].get("produced", [])
    marker_ids = {p["id"] for p in produced if p["domain"] == "sequence.marker"}
    event_ids = {p["id"] for p in produced if p["domain"] == "sequence.event"}
    scenario_ids = {p["id"] for p in produced if p["domain"] == "staging.scenario"}
    unknown = {p["domain"] for p in produced} - {"sequence.marker", "sequence.event", "staging.scenario"}
    if unknown:
        raise RuntimeError(f"the plan produced content this step does not carry over: {sorted(unknown)}")

    staging = saved.get("staging")
    if staging is None or {s["name"] for s in staging.get("scenarios", [])} != scenario_ids:
        raise RuntimeError("the saved staging is not the plan's set pieces")
    project["staging"] = staging

    seq = project.setdefault("sequence", {})
    ssaved = saved.get("sequence", {})
    ours = [m for m in ssaved.get("markers", []) if f"{m.get('name')}@{m.get('time', 0.0):.3f}" in marker_ids]
    if len(ours) != len(marker_ids):
        raise RuntimeError(f"found {len(ours)} of the plan's {len(marker_ids)} markers in the save")
    names = {m["name"] for m in ours}
    kept = [m for m in seq.get("markers", []) if m.get("name") not in names]
    seq["markers"] = sorted(kept + ours, key=lambda m: m["time"])
    events = [e for e in ssaved.get("events") or [] if e.get("id") in event_ids]
    if len(events) != len(event_ids):
        raise RuntimeError(f"found {len(events)} of the plan's {len(event_ids)} cue events in the save")
    seq["events"] = [e for e in seq.get("events") or [] if e.get("id") not in event_ids] + events

    project["directingPlans"] = [p for p in project.get("directingPlans", []) if p.get("id") != plan()["id"]] + plans
    return project


# ---- 3. trace ----------------------------------------------------------------------------------------
def trace(project_path, out=TRACE):
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(TOOL), "--project", str(project_path), "--seconds", f"{FILM_SECONDS:g}", "--fps", f"{FPS:g}",
           "--hz", "20", "--out", str(out)]
    run = subprocess.run(cmd, capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(f"avgen_cast_trace failed ({run.returncode}):\n{run.stderr[-4000:]}")
    return json.loads(out.read_text())


def beats_of(trace_doc, report):
    """What each set piece did, from the trace: its moments, the craft's place at each, the animals it
    took, how still the craft held; the plan's nominal moments beside them; and Song Mode's events
    (ADR-930: `{"name", "subject", "seconds"}` from `setPieces[].beats`)."""
    nominal = {p["key"]: p for p in report.get("setPieces", [])}
    pieces = []
    events = []
    for sp in trace_doc.get("setPieces", []):
        moments = {b: t for b, t in sp.get("beats", {}).items() if b not in HIDDEN_BEATS}
        placed = nominal.get(sp["id"], {})
        pieces.append({
            "id": sp["id"], "craft": sp["craft"], "template": placed.get("template"),
            "placedMoment": placed.get("placedMoment"), "framingMetres": placed.get("framingMetres"),
            "moments": moments, "nominal": placed.get("moments", {}),
            "craftAt": {b: p for b, p in sp.get("craftAt", {}).items() if b not in HIDDEN_BEATS},
            "animals": sp.get("animals", []), "holding": sp.get("holding"),
            "finished": sp.get("finished"), "ended": sp.get("ended"), "failures": sp.get("failures", []),
        })
        for beat, t in sorted(moments.items(), key=lambda kv: kv[1]):
            events.append({"name": event_name(sp["id"], beat), "subject": sp["craft"], "seconds": t})
    events.sort(key=lambda e: e["seconds"])
    return {"source": str(trace_doc.get("source")), "fps": trace_doc.get("fps"), "plan": str(PLAN.name),
            "setPieces": pieces, "songEvents": events, "watches": watches(trace_doc)}


def watches(trace_doc):
    """Each alien's watches of each beam: {set piece: {alien: [{from, to, seconds, metres}]}}, in
    film seconds at the trace's rate (20 Hz, so a window is good to 0.05 s). An alien with no entry
    did not stand facing that beam at all. What the picture shows, not what the alien heard: one
    that happens to stand facing a far beam it cannot hear (tide and E3, 230 m) counts too."""
    out = {}
    for sp in trace_doc.get("setPieces", []):
        beam = sp.get("beats", {}).get("beam")
        at = (sp.get("craftAt") or {}).get("beam")
        if beam is None or at is None:
            continue
        per = {}
        for name in ALIENS:
            e = trace_doc.get("entities", {}).get(name)
            if e is None:
                continue
            t, p, yaw = e["t"], e["position"], e["yaw"]
            runs, run = [], None
            for i in range(1, len(t)):
                if not beam <= t[i] <= beam + WATCH_AFTER:
                    continue
                dx, dz = at[0] - p[i][0], at[2] - p[i][2]
                off = abs(math.degrees((yaw[i] - math.atan2(dx, dz) + math.pi) % (2.0 * math.pi) - math.pi))
                speed = math.hypot(p[i][0] - p[i - 1][0], p[i][2] - p[i - 1][2]) / max(t[i] - t[i - 1], 1e-9)
                if off <= WATCH_FACING and speed < WATCH_STILL:
                    run = [t[i], t[i], math.hypot(dx, dz)] if run is None else [run[0], t[i], run[2]]
                elif run is not None:
                    runs.append(run)
                    run = None
            if run is not None:
                runs.append(run)
            kept = [{"from": round(a, 3), "to": round(b, 3), "seconds": round(b - a, 3), "metres": round(d, 1)}
                    for a, b, d in runs if b - a >= WATCH_MIN]
            if kept:
                per[name] = kept
        out[sp["id"]] = per
    return out


def frame(t):
    return round(t * FPS)


def check(beats):
    """Every set piece happened, and E5 sits on its music. The musical instants are the engine's own
    grid (ADR-896), which the plan's "bar 93" and "bar 97" resolve against."""
    problems = []
    by_id = {p["id"]: p for p in beats["setPieces"]}
    for key, count in EXPECTED_ANIMALS.items():
        p = by_id.get(key)
        if p is None:
            problems.append(f"{key} is not in the trace")
            continue
        if "beam" not in p["moments"]:
            problems.append(f"{key} never lit its beam (it found {len(p['animals'])} animal(s))")
        taken = [a for a in p["animals"] if a.get("retired", -1.0) >= 0.0]
        if len(taken) != count:
            problems.append(f"{key} took {len(taken)} animal(s), not {count}")
    for key in ("e1-far-survey", "e2-flyby"):
        p = by_id.get(key)
        if p is None or p.get("ended") != "finished":
            problems.append(f"{key} did not finish")
    e5 = by_id.get("e5-centrepiece")
    if e5 is not None and "beam" in e5["moments"]:
        bar93 = e5["nominal"].get("beam")
        if bar93 is not None and not 0 <= frame(e5["moments"]["beam"]) - frame(bar93) <= 1:
            problems.append(f"E5's beam lit at {e5['moments']['beam']:.3f} s, not on bar 93 ({bar93:.3f} s)")
    # The horse is gone on the drop: its dissolve ends, and the next step retires it, within a frame of
    # the first frame of the drop (bar 97 on the production's grid, which the cut is laid on).
    if e5 is not None:
        drop = math.ceil(music.bar(97) * FPS - 1e-6)
        horse = [a for a in e5["animals"] if a.get("retired", -1.0) >= 0.0]
        if horse and abs(frame(horse[0]["retired"]) - drop) > 1:
            problems.append(f"the horse retired at {horse[0]['retired']:.3f} s, not on the drop "
                            f"(frame {drop}, {drop / FPS:.3f} s)")
    return problems


def compile_into(project_path=PROJECT, traced=True, trace_file=None):
    """`trace_file`: take the film's set pieces and watches from that whole-film trace of this
    project instead of tracing it again (it must be of the project as compiled here)."""
    project_path = pathlib.Path(project_path).resolve()
    if not TOOL.exists():
        raise RuntimeError(f"{TOOL} is not built (the shared engine build: revision/briefs.md, Phase 3)")
    # Beside the original, so the project's relative paths (its scene, the song) resolve the same.
    scratch = project_path.with_name(project_path.stem + ".ufo-compiling.json")
    try:
        report = compile_plan(project_path, scratch)
        saved = json.loads(scratch.read_text())
    finally:
        if scratch.exists():
            scratch.unlink()
    project = splice(json.loads(project_path.read_text()), saved)
    project_path.write_text(dump(project))
    if not traced:
        return {"setPieces": [{"id": p["key"], "craft": p["craft"], "nominal": p.get("moments", {})}
                              for p in report.get("setPieces", [])]}
    film = json.loads(pathlib.Path(trace_file).read_text()) if trace_file else trace(project_path)
    beats = beats_of(film, report)
    BEATS.write_text(json.dumps(beats, indent=1) + "\n")
    problems = check(beats)
    if problems:
        raise RuntimeError("the film is not the plan:\n  " + "\n  ".join(problems))
    return beats


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--project", default=str(PROJECT))
    ap.add_argument("--no-trace", dest="traced", action="store_false",
                    help="compile and install only; skip the whole-film trace (about 15 minutes)")
    ap.add_argument("--trace-file", default=None,
                    help="read the film from this whole-film trace of the compiled project instead of tracing")
    args = ap.parse_args()
    try:
        beats = compile_into(args.project, traced=args.traced, trace_file=args.trace_file)
    except RuntimeError as e:
        print(f"ufo: {e}", file=sys.stderr)
        return 1
    for p in beats["setPieces"]:
        moments = p.get("moments") or p["nominal"]
        text = ", ".join(f"{m} {t:.3f}" for m, t in sorted(moments.items(), key=lambda kv: kv[1]))
        taken = ", ".join(f"{a['entity']} {a['retired']:.3f}" for a in p.get("animals", [])
                          if a.get("retired", -1.0) >= 0.0)
        hold = p.get("holding") or {}
        print(f"  {p['id']:15s} {p['craft']:7s} {text}" + (f"; took {taken}" if taken else "")
              + (f"; held within {hold['maxDrift']:.2f} m" if hold.get("frames") else ""))
    for key, per in beats.get("watches", {}).items():
        if per:
            print(f"  {key} watched by " + "; ".join(
                f"{name} " + ", ".join(f"{w['from']:.2f}-{w['to']:.2f} ({w['metres']:.0f} m)" for w in ws)
                for name, ws in per.items()))
    print(f"compiled {PLAN.name} into {pathlib.Path(args.project).name}"
          + (f"; the film's set pieces are in {BEATS.relative_to(ROOT)}" if args.traced else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
