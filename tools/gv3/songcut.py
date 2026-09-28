"""The cut's times and arcs: the engine Director's Song Mode cut of this project (ADR-920 to 923).

The first pass placed every cut itself, on a grid, with durations chosen in Python. The revision
does not: **when each shot starts and ends, and how a section's cutting travels, is the Director's
decision**, made by `avgen_song_cut` from the project's own section timeline, its measured audio,
its heroes and its events. What stays authored is what the Director cannot know -- the picture:
`shots.py` gives every span the Director lays down one composition (a rig), and refuses a span it
has no composition for.

What GV3 authors *about* pacing is the Director's input, in the engine's own vocabulary:

  * **The treatments** (`TREATMENTS`, `TREATMENT_OF`): one shot intent per section -- a built-in
    where one says what the passage should feel like, GV3's own where none does. They are written
    into the project's shot language, so an artist finds them in the Sequence panel (a section's
    inspector -> treatment), with the Director's reading of each beside it ("cuts: ...").
  * **The settings** (`SETTINGS`): the Auto-director panel's Song mode and its shot timing, written
    as the project's `autoDirector` block, so pressing Direct in the app cuts the same film.
  * **The events** (`events()`): the UFO set pieces' moments, E1-E5, as Song Mode plan events
    (ADR-922), so a peak section goes to the set piece playing in it rather than to whoever's turn
    it is. Written as the project's `songPlan.events`.

And two edits on top of the Director's cut, each with the evidence that asked for it:

  * **The grid.** The Director cuts on the analysed track's tracked beats, which sit about 9 ms late
    (worst 19 ms, ADR-896; the song stream's report). It reports each cut as a bar and a beat, and
    the cut is placed on that bar and beat of the production's fitted grid (`music.py`, 7 ms RMS
    against a blind tracker): the Director decides *which* beat, the fitted grid says *when* it is.
    `make_glowmere_valley_3.install_cut` then leads every cut by one frame, as the first pass did.
  * **Trims** (`TRIMS`): a shot the evaluator's novelty measure says has said its piece ends earlier,
    on a beat, and the next shot starts there.

Reproducible: the generator re-runs the Director on a scratch copy of the project it is building
(CPU, a few seconds) and checks the cut against `song_cut.json`, the Director's cut the compositions
were written against. When the two disagree -- another stream changed an input the Director reads --
it says which spans moved and keeps the recorded cut, so the film stays the one that was reviewed;
`--recut` adopts the Director's new cut and rewrites the record.
"""

import copy
import hashlib
import json
import os
import pathlib
import subprocess
import sys

from . import music

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "build" / "release" / "tools" / "avgen_song_cut"
WORLD = ROOT / "examples" / "world"
SCRATCH = ROOT / "build" / "gv3" / "director"
RECORD = pathlib.Path(__file__).resolve().with_name("song_cut.json")

# ---- the Director's settings -----------------------------------------------------------------------
# The Auto-director panel -> Shot mode: Song -> Shot timing. The song stream's recommendation for GV3
# (stream-reports/song.md): a band of 1.8-7.5 s, so a steady section cuts between a bar and four bars,
# and a build may go down to one beat ("shortest build" 0.45 s is under a beat, so the beat is the
# floor). The first pass's band, 4.6-6.5 s, allowed only 10-14 beats and no acceleration at all.
SETTINGS = {"mode": "song", "autonomy": "expressive", "minShot": 1.8, "maxShot": 7.5, "minBuildShot": 0.45,
            "seed": 1}


# ---- the treatments ----------------------------------------------------------------------------------
def _intent(ident, name, description, focus, strength, tightest, widest, movement, energy, variation, cut,
            density, cameras, arc):
    return {"id": ident, "name": name, "description": description, "focus": focus, "focusStrength": strength,
            "framing": {"tightest": tightest, "widest": widest}, "movement": movement, "energy": energy,
            "variation": variation, "cutFrequency": cut, "visualDensity": density,
            "cameras": {"fewest": cameras[0], "most": cameras[1]}, "arc": arc}


# GV3's own treatments, where no built-in says what the passage should do. Each is measured against
# the built-in the first pass used (the song stream's recommended cut, `audit/data/song/`) and the
# plan's pacing target (03-revision-plan.md section 3); the iteration log (revision/phase3/cut.md)
# has the Director's cut under each. Dials are the engine's (src/song/shot_intent.hpp): what a
# passage should feel like, never a camera or a length.
TREATMENTS = [
    # groove 2 (bars 17-32). The built-in "slow environmental exploration" cut four 4-bar takes, the
    # first pass's own shape, and the evaluator found s08 holding 2.5 s of its 7.4 after its last new
    # information. The valley wakes here: 2-bar cuts, with one longer travel.
    _intent("gv3_valley_waking", "GV3: the valley wakes",
            "The light returns: the world first, cut on the groove's two-bar lines, one longer travel.",
            "environment", 0.4, "medium", "very-wide", 0.5, 0.55, 0.3, 0.45, 0.5, (1, 2), "steady"),
    # the lift (bars 33-40). The built-in "increasing movement" went to half-bar cuts after two bars,
    # as frantic as the riser it should lead to. A build that accelerates less: two 2-bar shots (the
    # first crane; the crane that finds E3's column), then bar cuts into the arrival.
    _intent("gv3_lift", "GV3: the lift",
            "Start on long moves and tighten to bar cuts by the boundary: the body arriving.",
            "mixed", 0.5, "medium", "very-wide", 0.6, 0.65, 0.55, 0.5, 0.0, (1, 3), "rising"),
    # the lead forward (bars 73-80). The built-in "building tension" accelerated to half bars, the
    # third section in a row to end that way. The lead carries the aurora: 2-bar takes, tightening to
    # bars only at the end, before the suspension stops everything.
    _intent("gv3_lead_aurora", "GV3: the lead, the aurora",
            "Hold on the sky and whoever looks up at it; tighten only at the end.",
            "hero", 0.3, "medium", "very-wide", 0.55, 0.6, 0.4, 0.35, 0.3, (1, 3), "rising"),
    # the suspension (bars 81-88). "Suspended" holds one 8-bar shot. The saucer's approach has two
    # stages -- over the rim, far; on beyond the elder's cap, near -- and the first pass's two locked-off
    # 4-bar wides both kept introducing information to their last frame (s22 0.36 s, s23 0.44 s held
    # after). Locked off, two holds.
    _intent("gv3_suspension", "GV3: the suspension",
            "Locked off and held: two long frames while the visitor comes over the rim.",
            "mixed", 0.5, "wide", "very-wide", 0.05, 0.2, 0.0, 0.0, 0.3, (1, 1), "steady"),
    # the submerged break (bars 89-92). "Held tension" held one 4-bar shot, and the first pass's
    # 2-bar shot from under the elder showed nothing new after its first frame. Two 2-bar holds.
    _intent("gv3_submerged", "GV3: the submerged break",
            "Dark and heavy under the water's lid: two close, still frames of the saucer settling.",
            "hero", 0.5, "close", "medium", 0.25, 0.4, 0.3, 0.45, 0.45, (1, 2), "steady"),
    # the riser (bars 93-96). "Increasing movement" cut 8 -> 4 -> 2 -> 2 beats, weaker than the first
    # pass's compression. The roll's 8ths, 16ths and 32nds: the fastest a rising arc cuts, down to the
    # beat -- the Director lays it as 4, 4, 2, 2, 2, 1, 1 beats, the first pass's riser exactly.
    _intent("gv3_riser_roll", "GV3: the riser's roll",
            "Every angle on the lift, and the cutting doubles with the roll into the drop.",
            "mixed", 0.5, "close", "wide", 0.8, 0.8, 0.8, 1.0, 1.0, (1, 3), "rising"),
    # the drop (bars 97-120). "Large-scale dynamic coverage" cut the whole drop at a bar or two, to the
    # end. The light rebuilt: a 2-bar payoff on the crash, bar cuts through the rebuilt valley, and
    # the cutting relaxing into long wides for the final, thinner phrase -- the plan's "1-2 bar cuts in
    # the first phrase, then 4-bar wides", and the room for the last wide's rhyme with the first.
    _intent("gv3_rebuilt", "GV3: the light rebuilt",
            "Land on the crash, cut on the bar through the rebuilt valley, and open out to finish.",
            "environment", 0.5, "wide", "very-wide", 0.85, 1.0, 0.6, 0.9, 0.0, (1, 3), "falling"),
]

# Each segment's treatment, by id: a built-in (src/song/shot_intent.cpp) or one of GV3's above.
TREATMENT_OF = {
    "cold-open": "atmospheric_establishing",
    "riff-groove": "groove_coverage",
    "first-pullback": "held_tension",
    "groove-2": "gv3_valley_waking",
    "lift": "gv3_lift",
    "arrival": "rising_reveal",
    "melodic-plateau": "environmental_performance_exploration",
    "lead-forward": "gv3_lead_aurora",
    "suspension": "gv3_suspension",
    "submerged-break": "gv3_submerged",
    "riser": "gv3_riser_roll",
    "drop": "gv3_rebuilt",
    "tail": "hard_transition",
}


def shot_language():
    """The project's shot language: GV3's own treatments, beside the engine's built-ins."""
    return {"intents": copy.deepcopy(TREATMENTS)}


# ---- the events --------------------------------------------------------------------------------------
# The UFO set pieces' moments (E1-E5, plan section 3), as the engine compiled them from
# `docs/glowmere-valley-3/revision/audit/data/setpieces/gv3-ufo.plan.json` (`avgen_cast_trace --plan`
# on a scratch copy with the scout craft, 2026-09-27: the plan's sequence markers). Used until the
# project carries set pieces of its own -- gv3-cast compiles the plan into it, and then the project's
# `setpiece/<id>/<moment>` markers are read instead, so the Director hears the set pieces the film
# actually plays. The subject is the craft's body: a peak goes to it only if it is a hero.
NOMINAL_SET_PIECES = [
    ("setpiece/e1-far-survey/approach", "scout", 6.2318), ("setpiece/e1-far-survey/beam", "scout", 13.8651),
    ("setpiece/e1-far-survey/sweep", "scout", 14.9818), ("setpiece/e1-far-survey/depart", "scout", 22.6318),
    ("setpiece/e2-flyby/cross", "visitor", 26.6364), ("setpiece/e2-flyby/depart", "visitor", 29.8530),
    ("setpiece/e3-far-lift/approach", "scout", 55.9515), ("setpiece/e3-far-lift/beam", "scout", 65.5848),
    ("setpiece/e3-far-lift/lift", "scout", 66.9515), ("setpiece/e3-far-lift/depart", "scout", 71.8848),
    ("setpiece/e4-river-pair/approach", "scout", 93.8708), ("setpiece/e4-river-pair/beam", "scout", 102.5041),
    ("setpiece/e4-river-pair/lift", "scout", 103.8708), ("setpiece/e4-river-pair/depart", "scout", 111.9041),
    ("setpiece/e5-centrepiece/approach", "visitor", 148.3060), ("setpiece/e5-centrepiece/beam", "visitor", 170.3393),
    ("setpiece/e5-centrepiece/lift", "visitor", 172.7560), ("setpiece/e5-centrepiece/depart", "visitor", 177.6993),
]
CRAFT_BODY = {"saucer": "visitor", "scout": "scout"}


def events(project, scene):
    """Song Mode's plan events: the project's own set-piece moments if it has any, else the plan's."""
    bodies = dict(CRAFT_BODY)
    for staging in (scene.get("staging", {}), project.get("staging", {})):
        for actor in staging.get("actors", []):
            bodies[actor["name"]] = actor.get("body", actor["name"])
    crafts = {}
    for plan in project.get("directingPlans", []):
        for sp in plan.get("setPieces", []):
            crafts[sp["key"]] = sp.get("craft", "")
    found = []
    for m in project.get("sequence", {}).get("markers", []):
        name = m.get("name", "")
        if name.startswith("setpiece/") and name.count("/") == 2:
            key = name.split("/")[1]
            found.append({"name": name, "subject": bodies.get(crafts.get(key, ""), crafts.get(key, "")),
                          "seconds": round(float(m["time"]), 4)})
    if found:
        return sorted(found, key=lambda e: e["seconds"]), "the project's set pieces"
    return ([{"name": n, "subject": s, "seconds": t} for n, s, t in NOMINAL_SET_PIECES],
            "the UFO plan's nominal moments (no set pieces compiled into this project yet)")


# ---- trims -------------------------------------------------------------------------------------------
# {span label: beats}: end that span this many beats early, the next span starting there. Each from
# the Critic's novelty ("last new information") on a render of the cut; revision/phase3/cut.md has
# the job and the number behind each.
TRIMS = {}


# ---- running the Director ------------------------------------------------------------------------------
def _absolutize(node, base):
    """Relative asset paths made absolute, so a copy of the project runs from anywhere."""
    if isinstance(node, dict):
        return {k: _absolutize(v, base) for k, v in node.items()}
    if isinstance(node, list):
        return [_absolutize(v, base) for v in node]
    if isinstance(node, str) and node.startswith("../"):
        return os.path.normpath(str(base / node))
    return node


def director_block(existing):
    """The project's `autoDirector` block: the Song mode settings the cut was made with."""
    block = dict(existing or {})
    block.update(SETTINGS)
    return block


def _scratch(project, scene, plan_events):
    """A copy of the project as the Director should see it: the Auto-director's own camera available
    (the authored cameras are GV3's, not its), no cut installed, the events in the song plan."""
    SCRATCH.mkdir(parents=True, exist_ok=True)
    p = _absolutize(copy.deepcopy(project), WORLD)
    s = _absolutize(copy.deepcopy(scene), WORLD)
    s["cameraDirection"] = {"cameras": [{"id": 1, "name": "Main", "autoDirector": True}], "shots": [],
                            "default": 1, "nextId": 2}
    p["autoDirector"] = director_block(p.get("autoDirector"))
    p["songPlan"] = {"sections": [], "events": plan_events}
    p["timeline"]["tracks"] = [t for t in p["timeline"]["tracks"]
                               if not t["target"].startswith(("camera/", "cameras/"))]
    scene_path = SCRATCH / "gv3-director.scene.json"
    scene_path.write_text(json.dumps(s))
    p["assets"]["scene"]["path"].update(path=scene_path.name, sha256=hashlib.sha256(scene_path.read_bytes()).hexdigest(),
                                        size=scene_path.stat().st_size)
    project_path = SCRATCH / "gv3-director.json"
    project_path.write_text(json.dumps(p))
    return project_path


def run_director(project, scene, plan_events):
    """The Director's cut report (`avgen-song-cut` v1) for this project, or None without the tool."""
    if not TOOL.exists():
        return None
    path = _scratch(project, scene, plan_events)
    out = SCRATCH / "song-cut.json"
    r = subprocess.run([str(TOOL), "--project", str(path), "--out", str(out)], capture_output=True, text=True)
    (SCRATCH / "song-cut.log").write_text(r.stdout + r.stderr)
    if r.returncode != 0:
        raise RuntimeError(f"avgen_song_cut failed ({r.returncode}); see {SCRATCH / 'song-cut.log'}:\n"
                           + r.stderr[-2000:])
    return json.loads(out.read_text())


def _essential(report):
    """What the compositions depend on: every shot's place on the grid, and each section's arc."""
    return [(s["startBar"], s["startBeat"], round(s["beats"], 3), s["section"], s["arc"]) for s in report["shots"]]


def _slim(report, source):
    """The record: the report without its per-cut list (derivable), with where the events came from."""
    keep = {k: report[k] for k in ("format", "version", "settings", "grid", "sections", "shots", "stats", "warnings")}
    keep["events"] = source
    return keep


def direct(project, scene, recut=False, log=print):
    """The cut's spans, from the Director: `(spans, report, note)`."""
    plan_events, source = events(project, scene)
    project["songPlan"] = {"sections": [], "events": plan_events}
    live = run_director(project, scene, plan_events)
    record = json.loads(RECORD.read_text()) if RECORD.exists() else None
    if live is None:
        if record is None:
            raise RuntimeError(f"{TOOL} is not built and there is no recorded cut ({RECORD})")
        note = f"the Director's recorded cut ({RECORD.name}); {TOOL.name} is not built, so it was not re-run"
        report = record
    elif recut or record is None:
        RECORD.write_text(json.dumps(_slim(live, source), indent=1) + "\n")
        note = f"the Director's cut, re-run and recorded in {RECORD.name}"
        report = live
    elif _essential(live) != _essential(record):
        moved = [f"{a[0]}.{a[1]} ({a[2]:g} beats)" for a, b in zip(_essential(live), _essential(record)) if a != b][:8]
        log(f"warning: the Director now cuts this project differently from the cut the compositions were "
            f"written against ({len(live['shots'])} spans against {len(record['shots'])}; first differences at "
            f"{', '.join(moved) or 'the end'}). The recorded cut is used; run with --recut to adopt the new one "
            f"and re-author the spans that moved.", file=sys.stderr)
        note = f"the Director's recorded cut ({RECORD.name}); the live Director disagrees, see the warning"
        report = record
    else:
        note = f"the Director's cut, re-run and identical to {RECORD.name}"
        report = live
    return spans(report), report, note


# ---- the spans ---------------------------------------------------------------------------------------
SEGMENT_OF_SECTION = [name for name, _first, _after in music.SEGMENTS]


class Span:
    """One of the Director's shots: where it sits on the fitted grid, and why it is as long as it is."""

    __slots__ = ("index", "label", "start", "end", "bar", "beat", "segment", "arc", "kind", "subject",
                 "subject_reason", "ends_on", "why", "director_start", "director_end", "trimmed")

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, kw.get(k))

    @property
    def beats(self):
        return (self.end - self.start) / music.BEAT

    @property
    def duration(self):
        return self.end - self.start


def label(bar, beat):
    return "open" if bar <= 0 else f"{bar}.{beat}"


def spans(report, film_end=225.5):
    """The report's shots on the fitted grid, trims applied. Refuses a cut that is not on the beat."""
    shots = report["shots"]
    out = []
    for i, s in enumerate(shots):
        bar, beat = int(s["startBar"]), int(s["startBeat"])
        start = 0.0 if bar <= 0 else music.beat(bar, beat)
        if bar > 0 and abs(start - s["start"]) > 0.5 * music.BEAT:
            raise RuntimeError(f"the Director's shot {i} at {s['start']:.3f} s is not bar {bar} beat {beat} "
                               f"({start:.3f} s) of the fitted grid")
        out.append(Span(index=i, label=label(bar, beat), start=start, end=None, bar=bar, beat=beat,
                        segment=SEGMENT_OF_SECTION[s["section"]], arc=s["arc"], kind=s["kind"],
                        subject=s["subject"], subject_reason=s["subjectReason"], ends_on=s["endsOn"],
                        why=s["why"], director_start=s["start"], director_end=s["end"], trimmed=0))
    for a, b in zip(out, out[1:]):
        a.end = b.start
    out[-1].end = film_end
    for a, b in zip(out, out[1:]):
        trim = TRIMS.get(a.label, 0)
        if trim:
            if trim >= round(a.beats) - 0.5:
                raise RuntimeError(f"a trim of {trim} beats would empty span {a.label}")
            a.end -= trim * music.BEAT
            b.start = a.end
            a.trimmed = trim
    unknown = set(TRIMS) - {s.label for s in out}
    if unknown:
        raise RuntimeError(f"trims name spans the Director did not lay: {sorted(unknown)}")
    return out
