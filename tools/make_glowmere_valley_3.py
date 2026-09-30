#!/usr/bin/env python3
"""Generate Glowmere Valley 3 -- a music video to "Rebuild" -- from Glowmere Valley 2 (multicam).

    python3 tools/make_glowmere_valley_3.py            # the film
    python3 tools/make_glowmere_valley_3.py --scout    # a location scout instead of the cut
    python3 tools/make_glowmere_valley_3.py --final    # the film, configured for the 4K final render

It reads examples/world/glowmere-valley-2-multicam.{json,scene.json} and never writes to them (the
brief: the original stays unmodified), and writes examples/world/glowmere-valley-3.{json,scene.json},
build/gv3/shots.json (the cut, for tools/contact_sheet.py) and, for the film,
docs/glowmere-valley-3/04-shot-plan.md.

What is authored about Glowmere Valley 3 lives in tools/gv3/: the musical grid (music.py), the
Director's cut and what GV3 tells the Director (songcut.py), the compositions on its spans
(shots.py), the look and the modulation (look.py), the cast and the abduction (cast.py). This file
only assembles. Every creative decision is data there, written out as the engine's own
content -- a camera collection with locked shots, timeline keys, modulation routes, effect
instances, staging -- so the project is the whole truth and renders without this script.

What it strips from the source, and why (docs/glowmere-valley-3/README.md has the record):

  * the Song-mode camera bake (six `camera/*` timeline tracks, `cameraAimFollow`, `cameraShotSpans`)
    and the multicam's own three cameras: the cut is authored here, and every piece of the old one
    either fights it (a replace track holds its value for the whole film) or nudges it (aim-follow);
  * the detected and hand-edited song sections, which were wrong (docs/glowmere-valley-3/01-music.md
    §1.6), replaced by the production's own segmentation so the sequencer shows the real structure;
  * the routes that made everything pulse on every beat, replaced by a modulation with a hierarchy.
"""
import argparse
import copy
import hashlib
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from gv3 import cast, heroes, look, music, musicians, songcut, world  # noqa: E402
from gv3.ground import Ground  # noqa: E402

ROOT = HERE.parent
WORLD = ROOT / "examples" / "world"
SRC_PROJECT = WORLD / "glowmere-valley-2-multicam.json"
SRC_SCENE = WORLD / "glowmere-valley-2-multicam.scene.json"
OUT_PROJECT = WORLD / "glowmere-valley-3.json"
OUT_SCENE = WORLD / "glowmere-valley-3.scene.json"
BUILD = ROOT / "build" / "gv3"
DOCS = ROOT / "docs" / "glowmere-valley-3"

# The source this production was derived from. If the multicam changes, the generator says so
# rather than silently producing a different film: the owner may have edited it in the app.
SOURCE_SHA256 = {
    "glowmere-valley-2-multicam.json": "",
    "glowmere-valley-2-multicam.scene.json": "",
}

# How the production's segments read in the engine's section vocabulary (src/song/section_type.cpp),
# and each one's treatment -- the shot intent the Director cuts it by (songcut.TREATMENT_OF: a
# built-in, or one of GV3's own, which the project's shot language defines). The names are the
# production's own.
SECTION_TYPES = {
    "cold-open": "establishing",
    "riff-groove": "groove",
    "first-pullback": "pause",
    "groove-2": "groove",
    "lift": "build",
    "arrival": "peak",
    "melodic-plateau": "exploration",
    "lead-forward": "tension",
    "suspension": "suspense",
    "submerged-break": "breakdown",
    "riser": "riser",
    "drop": "drop",
    "tail": "outro",
}
# ...and in the analyser's smaller vocabulary of section *functions* (src/analysis/structure.hpp),
# where `other` is the honest answer for a groove that has no verses and choruses.
SECTION_FUNCTIONS = {
    "cold-open": "intro", "riff-groove": "other", "first-pullback": "break", "groove-2": "other",
    "lift": "build", "arrival": "other", "melodic-plateau": "other", "lead-forward": "other",
    "suspension": "break", "submerged-break": "breakdown", "riser": "build", "drop": "drop", "tail": "outro",
}
# Segment mean energy from the analysis (01-music.md §1.3), 0..1 with the riser crest at 1.
# The film's energy arc, which the Director reads twice: Song Mode's pacing (ADR-921) and the
# reactivity proposal's depth (ADR-927, `section.energy` as the depth source). The revision authors it.
# The first pass carried 01-music.md's measured composite (0.29-0.87). That composite is right about
# the music but flat as a shape: the cold open sat above the break, and the riser above the drop. The
# authored arc keeps the music's ranking (drop > arrival > lift > groove; low points the pull-back,
# the break and the tail) and the light's story (drained through the suspension and break, taken in
# the riser, rebuilt at the drop). The cold open stays alive, at half.
SEGMENT_ENERGY = {
    "cold-open": 0.50, "riff-groove": 0.55, "first-pullback": 0.25, "groove-2": 0.60, "lift": 0.70,
    "arrival": 0.85, "melodic-plateau": 0.65, "lead-forward": 0.70, "suspension": 0.40,
    "submerged-break": 0.20, "riser": 0.60, "drop": 1.00, "tail": 0.30,
}

FILM_END = 225.5  # the reverb dies at 225.38; the picture is black from the last hit
CUT_LEAD = 1.0 / 60.0  # seconds every cut lands ahead of its beat


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(obj):
    return json.dumps(obj, indent=2) + "\n"


def world_block(scene):
    for node in scene["nodes"]:
        if "world" in node:
            return node["world"]
    raise RuntimeError("the scene has no terrain world")


# ---- what the source carried that the new cut must not inherit -------------------------------------
def strip_director_residue(project, scene):
    for key in ("cameraAimFollow", "cameraShotSpans", "cameraContinuousTake", "parkedDirector", "songPlan"):
        project.pop(key, None)
    tl = project.setdefault("timeline", {"enabled": True, "cues": [], "tracks": []})
    tl["tracks"] = [t for t in tl.get("tracks", [])
                    if not (t["target"].startswith("camera/") or t["target"].startswith("cameras/"))]
    params = project["parameters"]
    for key in [k for k in params if k.startswith("cameras/")]:
        del params[key]
    seq = project.setdefault("sequence", {})
    seq["shots"] = []
    seq["sectionPerformance"] = []
    # One musical time (ADR-896): the meter's settings are parameters. The source's control.phraseBars
    # and control.sectionPhrases are no longer read; the hand-measured grid is pinned (bar 1 on the
    # first tracked beat, 8-bar phrases), which the analysis also detects.
    control = project.setdefault("control", {})
    control.pop("phraseBars", None)
    control.pop("sectionPhrases", None)
    project["parameters"]["music/meter/bar1Beat"] = 0
    project["parameters"]["music/meter/phraseBars"] = 8


def write_sections(project):
    """The production's segmentation, as markers and a section timeline the sequencer shows."""
    seq = project["sequence"]
    markers, sections, detected = [], [], []
    for name, first, after in music.SEGMENTS:
        start, end = music.segment(name)
        kind, intent = SECTION_TYPES[name], songcut.TREATMENT_OF[name]
        markers.append({"kind": "section", "name": name.replace("-", " "), "time": round(start, 6)})
        sections.append({"authored": True, "density": 0.0, "edited": ["type", "shot-intent", "start", "end"],
                         "end": round(end, 6), "energy": SEGMENT_ENERGY[name], "group": 0,
                         "shotIntent": intent, "start": round(start, 6), "type": kind})
        detected.append({"density": 0.0, "end": round(end, 6), "endConfidence": 1.0,
                         "energy": SEGMENT_ENERGY[name], "function": SECTION_FUNCTIONS[name], "group": 0,
                         "labelConfidence": 1.0, "occurrence": 0, "origin": "authored",
                         "start": round(start, 6), "startConfidence": 1.0})
    # The silent pre-roll before the first downbeat belongs to the first segment.
    sections[0]["start"] = 0.0
    detected[0]["start"] = 0.0
    markers[0]["time"] = 0.0
    seq["markers"] = markers
    seq["sectionTimeline"] = {"duration": FILM_END, "sections": sections}
    # GV3's own treatments beside the built-ins, so the Sequence panel's treatment picker offers them.
    seq["shotLanguage"] = songcut.shot_language()
    seq["structure"] = {"duration": FILM_END, "sections": detected, "tempoBpm": music.BPM, "tempoConfidence": 1.0}


# ---- the camera ------------------------------------------------------------------------------------
def install_cut(project, scene, shots):
    """Every shot's rig into the scene's camera collection, one locked shot each, and the rigs'
    keys onto the project timeline. The cut covers the film end to end, so nothing else -- no event
    camera, no default -- ever takes the frame.

    The spans are the Director's (songcut.py); the project says so where an artist looks: the
    Auto-director panel shows the Song mode and shot timing the cut was made with (`autoDirector`),
    its own camera stays available to it (so Direct re-cuts the same spans on the Main camera), and
    the set pieces' moments are the song plan's events."""
    cameras = [{"id": 1, "name": "Main", "autoDirector": True}]
    cut = []
    tracks = []
    shots = sorted(shots, key=lambda s: s.start)
    for i, shot in enumerate(shots):
        ident = i + 2
        slug = shot.sid
        cameras.append(shot.rig.camera_json(ident, slug))
        # Every cut leads its beat by one frame: a cut that lands on the frame after the beat reads
        # as late, one that lands on or just before it reads as on it (research principle 17).
        start = shot.start if shot.start <= 0.0 else shot.start - CUT_LEAD
        end = shot.end if shot is shots[-1] else shot.end - CUT_LEAD
        entry = {"camera": ident, "start": round(start, 6), "end": round(end, 6),
                 "transition": shot.transition, "locked": True, "label": f"{shot.sid} {shot.purpose}"[:80]}
        if shot.transition == "blend":
            entry["blend"] = shot.blend
        cut.append(entry)
        tracks.extend(shot.rig.tracks_json(slug))
    scene["cameraDirection"] = {"cameras": cameras, "shots": cut, "default": 2 if len(cameras) > 1 else 1,
                                "nextId": len(cameras) + 1}
    project["timeline"]["tracks"].extend(tracks)
    project["autoDirector"] = songcut.director_block(project.get("autoDirector"))


ALIEN_BUDGET = 0.20  # the most of the film an alien may lead (brief section 9; the plan: 38% -> 20% or less)


def screen_time(shots):
    """Seconds of the cut by what leads each shot (rig.LEADS)."""
    out = {}
    for s in shots:
        out[s.lead] = out.get(s.lead, 0.0) + s.duration
    return out


def check_cut(shots, end):
    """Refuse a cut with a hole, an overlap, an empty shot, a cut off the beat, or aliens leading more
    of the film than the budget allows."""
    problems = []
    shots = sorted(shots, key=lambda s: s.start)
    if shots and shots[0].start > 1e-6:
        problems.append(f"the cut starts at {shots[0].start:.3f}, not 0")
    for a, b in zip(shots, shots[1:]):
        if abs(a.end - b.start) > 1e-6:
            problems.append(f"{a.sid} ends {a.end:.4f} but {b.sid} starts {b.start:.4f}")
    if shots and end is not None and shots[-1].end < end - 1e-6:
        problems.append(f"the cut ends at {shots[-1].end:.3f}, before {end}")
    for s in shots:
        if s.end <= s.start:
            problems.append(f"{s.sid} is empty")
        if s.start > 0.0:
            beats = (s.start - music.FIRST_DOWNBEAT) / music.BEAT
            if abs(beats - round(beats)) > 1e-6:
                problems.append(f"{s.sid} starts at {s.start:.4f}, off the beat grid")
    if end is not None and shots:
        alien = screen_time(shots).get("alien", 0.0) / (shots[-1].end - shots[0].start)
        if alien > ALIEN_BUDGET + 1e-9:
            problems.append(f"aliens lead {100 * alien:.1f}% of the film, over the {100 * ALIEN_BUDGET:.0f}% budget")
    return problems


def write_shots_json(shots, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dump({"shots": [
        {"id": s.sid, "start": round(s.start, 4), "end": round(s.end, 4),
         "label": s.purpose[:60], "segment": s.segment, "subject": s.subject, "lead": s.lead,
         **({"span": s.span.label, "arc": s.span.arc} if s.span else {})} for s in shots]}))


def shot_plan(shots):
    """The production record's shot table (04-shot-plan.md), written from the cut itself."""
    lines = ["# 4. Shot plan", "",
             "Generated by `tools/make_glowmere_valley_3.py`: the spans are the engine Director's Song Mode cut "
             "(`tools/gv3/songcut.py`, `avgen_song_cut`) and each span's composition is `tools/gv3/shots.py` -- "
             "the table and the project are written from the same data, so they cannot disagree. Times are "
             "seconds on the 130 BPM grid (bar.beat in brackets); Arc and Why are the Director's. Status and "
             "quality assessment are kept in [revision/phase3/cut.md](revision/phase3/cut.md) per iteration.", "",
             "| Shot | Time | Bars | Segment | Purpose | Subject | Camera | Movement | Music | Effects | Modulation | Arc | Lead |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    # The first eleven columns keep the first pass's order: the Critic's adapter reads them by position.
    for s in shots:
        arc = s.span.arc if s.span else ""
        lines.append(f"| {s.sid} | {s.start:.2f}-{s.end:.2f} ({music.label(max(s.start, music.FIRST_DOWNBEAT))}) | "
                     f"{s.duration / music.BAR:.2f} | {s.segment} | {s.purpose} | {s.subject} | "
                     f"{s.camera} | {s.movement} | {s.music} | {s.effects} | {s.modulation} | {arc} | {s.lead} |")
    lines.append("")
    total = shots[-1].end - shots[0].start
    lead = ", ".join(f"{k} {v:.1f} s ({100 * v / total:.0f}%)" for k, v in sorted(screen_time(shots).items()))
    lines.append(f"{len(shots)} shots; median length {sorted(s.duration for s in shots)[len(shots) // 2]:.2f} s. "
                 f"Led by: {lead}.")
    lines.append("")
    return "\n".join(lines)


# ---- assembly --------------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--scout", action="store_true", help="a location scout instead of the cut")
    parser.add_argument("--recut", action="store_true",
                        help="adopt the Director's cut as it is now and rewrite tools/gv3/song_cut.json")
    parser.add_argument("--final", action="store_true",
                        help="the final render's configuration: 4K at full offline quality (tools/gv3/offline.py)")
    parser.add_argument("--final-trace", metavar="CAST_JSON",
                        help="with --final: an avgen_cast_trace --camera JSON, so followed shots are measured too")
    args = parser.parse_args()

    for path in (SRC_PROJECT, SRC_SCENE):
        pinned = SOURCE_SHA256.get(path.name)
        if pinned and sha256(path) != pinned:
            print(f"warning: {path.name} changed since Glowmere Valley 3 was derived from it", file=sys.stderr)

    project = json.loads(SRC_PROJECT.read_text())
    scene = json.loads(SRC_SCENE.read_text())
    project = copy.deepcopy(project)
    scene = copy.deepcopy(scene)
    scene["name"] = "glowmere-valley-3"

    strip_director_residue(project, scene)
    write_sections(project)
    report = []
    world.close_ends(scene, report)  # before any height is asked of the world
    ground = Ground(world_block(scene))
    heroes.apply(project, scene, ground, report)  # the art pass's two new heroes, seated by world.apply
    world.apply(project, scene, ground, report)
    heroes.join_parts(project, scene, report)  # after seating: every hero's parts at its cap's place (ADR-986)
    cast.apply(project, scene)
    musicians.apply(project, scene, ground, report)  # the art pass's performers (after the cast: an entity each)
    look.apply_base(project, scene)

    if args.scout:
        from gv3 import scout
        shots = scout.build(ground)
        end = None
    else:
        from gv3 import shots as film
        end = FILM_END
        arc = look.apply_arc(project)
        motifs = look.apply_motifs(project, scene)
        motifs += musicians.routes(project, look._route)
        report.append(f"look: {arc} arc track(s), {motifs} motif route(s)")
        # The spans are the Director's, cut from this project as it now stands (songcut.py).
        spans, director, note = songcut.direct(project, scene, recut=args.recut)
        shots = film.build(ground, spans, lead=CUT_LEAD)
        st = director["stats"]
        report.append(f"cut: {note}: {st['shots']} spans, {st['cutsOnDownbeat']} of {st['cuts']} cuts on a "
                      f"downbeat, lengths {st['min']:.2f}-{st['max']:.2f} s (cv {st['cv']:.2f})")
    problems = check_cut(shots, end)
    if problems:
        for p in problems:
            print("cut:", p, file=sys.stderr)
        return 1
    install_cut(project, scene, shots)
    if not args.scout:
        cast.warp_rim(project, scene)  # the saucer's faint rim, in the shots that want it (revision round 1)
        look.shot_emphasis(project, scene)  # a shot's own emphasis: s49's gold gills (revision round 1)
        film.apply_clearings(scene)  # plants a shot's subject must not pass through: s46's fan plant (round 2)
        # The Camera Travel Beam on the camera changes that open a phrase (look.apply_travel_beam).
        beam_cuts = look.apply_travel_beam(project, shots)
        pulses = look.apply_hero_pulses(project, scene, shots)
        by = {}
        for _, n in pulses:
            by[n] = by.get(n, 0) + 1
        report.append(f"hero pulse (GV2 multicam's, on markers): {len(pulses)} rings, "
                      + ", ".join(f"{n} {c}" for n, c in sorted(by.items(), key=lambda kv: -kv[1])))
        report.append(f"travel beam: fires on {len(beam_cuts)} cuts, "
                      + ", ".join(f"{sid} (bar {b}, {t:.2f} s)" for t, sid, b in beam_cuts))

    # Last of the look: every effect's block agrees with the flat values that render (look.sync_effect_blocks).
    synced = look.sync_effect_blocks(project)
    report.append(f"effect blocks agree with what renders: {len(synced)} value(s) rewritten"
                  + (": " + ", ".join(f"{i} {p} {w} -> {n}" for i, p, w, n in synced) if synced else ""))

    render = project.setdefault("render", {})
    # The render path resolves against the project's folder. Into build/, which git ignores: the film
    # carries the song, which must never be committed, and examples/world/ is not ignored.
    render.update({"path": "../../build/gv3/glowmere-valley-3.mov", "start": 0.0, "end": FILM_END if end else shots[-1].end,
                   "fps": 60.0, "width": 1920, "height": 1080, "tier": "offline", "limits": "unlimited",
                   "muxAudio": True, "output": "video", "codec": "h264", "quality": 90})
    if args.final:
        from gv3 import offline
        offline.apply(project, scene, report, trace=args.final_trace)

    OUT_SCENE.write_text(dump(scene))
    project["assets"]["scene"]["path"]["path"] = OUT_SCENE.name
    project["assets"]["scene"]["path"]["sha256"] = sha256(OUT_SCENE)
    project["assets"]["scene"]["path"]["size"] = OUT_SCENE.stat().st_size
    OUT_PROJECT.write_text(dump(project))
    write_shots_json(shots, BUILD / ("scout.json" if args.scout else "shots.json"))
    if not args.scout:
        from gv3 import directives
        DOCS.mkdir(parents=True, exist_ok=True)
        (DOCS / "03-directives.md").write_text(directives.markdown())
        (DOCS / "04-shot-plan.md").write_text(shot_plan(shots))
    for line in report:
        print("  fixed:", line)
    print(f"wrote {OUT_PROJECT.relative_to(ROOT)} and {OUT_SCENE.relative_to(ROOT)}: "
          f"{len(shots)} shot(s), {shots[-1].end:.2f} s of cut")
    return 0


if __name__ == "__main__":
    sys.exit(main())
