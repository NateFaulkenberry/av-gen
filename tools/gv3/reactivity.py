"""Audio reactivity for Glowmere Valley 3, as a generator step: the Director proposes, the production
edits, the engine audits.

The first pass hand-wrote eleven routes and left the world, in the owner's words, "a static 3D scene
playing alongside music": one route in the pixels (the elder's heartbeat), the rest either shared
materials that moved all ten heroes in lockstep, or nothing. The revision asks the engine's own
reactivity planner (ADR-924..927) for the routes, the way a Director would, and treats its answer as a
starting position:

  1. PREPARE  what the planner reads is put in place first (look.py and this module): the waves of
              light it can send through the small mushrooms (a field timed from each bar, one from the
              drop), the scored pulses as events, the shared-material routes taken out.
  2. PROPOSE  `avgen --project <scratch copy> --propose-reactivity <dump>` on the project as generated
              so far. The planner is deterministic, so the same generator gives the same routes.
  3. EDIT     every change to its proposal is a row in EDITS or DROPS below, with the reason. What it
              proposes and nobody edits is installed as proposed.
  4. INSTALL  the dump's `install` block, the way ADR-927 says a generator installs it: routes and
              sources appended, parameters merged, the plan kept in `directingPlans`, so each route is
              a row marked `[plan: <key>]` with its reason in the Modulation panel, and the plan is
              listed in the Director panel.
  5. AUDIT    `avgen --audit-routes` on the installed project (ADR-902): every route and track live,
              or the generator says which is not.

`critic_modulation()` writes the evaluator's view of the same routes: which entity or screen region
each should visibly move and at what instants, so the Critic's route-locked check can tell
"configured" from "configured and observed" (brief section 16).
"""

import copy
import fnmatch
import hashlib
import json
import pathlib
import subprocess

from . import music

ROOT = pathlib.Path(__file__).resolve().parents[2]
ENGINE = ROOT / "build" / "release" / "src" / "avgen"
# A mirror of the repository's layout for scratch copies: the scene names its light rig, materials and
# profiles as `../<dir>/...` and its assets as `../../assets/...`, so a copy must sit two folders below a
# root that has them. build/ is ignored by git; examples/ must never hold scratch.
SCRATCH = ROOT / "build" / "gv3" / "reactivity"


# ---- PREPARE ---------------------------------------------------------------------------------------
# Waves of light through the small mushrooms (brief section 4: "local waves of illumination"; the
# plan's map: a ring from the elder through the fungi on the bar, and on the drop the light reaching
# the whole valley). A wave field whose clock starts at the latest event (ADR-906), named by a
# mushroom layer's `emissiveField` (ADR-905): the layer's emission x (1 + amount x field), after its
# program, and the planner scales each layer's depth with the section.
#
# Only the latest event's front exists, so a ring on every bar is restarted every 1.85 s: at 20 m/s it
# never gets further than 37 m. The bar's rings are therefore the elder's own, local -- they fade out
# by 45 m, before the next leaves -- and the valley-wide ring is a second field that fires once, on the
# drop, and crosses the valley in ten seconds. Two fields, two layers: the planner reads a field that
# is itself triggered, and does not look inside a compound of triggered fields (it reports one on the
# transport clock), so each layer names its own.
ELDER = [-12.0, 0.0, 52.0]  # the rings leave from under the elder's cap
WAVE_FIELDS = [
    # A pulse exp(-(2 pi s / wavelength)^2): at 40 m a band about 13 m wide, lighting each mushroom
    # it crosses for about half a second at 20 m/s. It fades out by 45 m, before the next leaves.
    {"name": "bar-wave", "kind": "field", "position": ELDER, "field": {
        "name": "bar-wave", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
        "wavelength": 40.0, "waveSpeed": 20.0, "waveWidth": 0.0,
        "falloff": {"kind": "smoothstep", "inner": 10.0, "outer": 45.0},
        "trigger": {"source": "beat", "everyN": 4, "offset": 0}}},
    # The drop's ring: out from the elder on the crash at 40 m/s, 19 m across; it crosses what the
    # drop's first wide sees (150 m) in under four seconds and fades out 220-340 m away.
    {"name": "drop-wave", "kind": "field", "position": ELDER, "field": {
        "name": "drop-wave", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
        "wavelength": 60.0, "waveSpeed": 40.0, "waveWidth": 0.0,
        "falloff": {"kind": "smoothstep", "inner": 220.0, "outer": 340.0},
        "trigger": {"source": "marker", "name": "drop"}}},
]
# Which layer shows which wave, and how much of it (the emission stream's measured range: 3 subtle,
# 8 clear; fungi 6-10, shelf fungi 4-6, beacons 3-4). The fungi and shelf fungi are the elder's
# neighbours; the beacons, 1.5 m lamps, are the small lights that still read across a wide, so they
# carry the drop's ring over the valley.
WAVE_LAYERS = {"fungi": ("bar-wave", 8.0), "shelf-fungi": ("bar-wave", 5.0), "beacons": ("drop-wave", 3.5)}
# The heroes take the drop's ring too: each lights as it passes, the elder first, on the crash, and
# the valley relights outward from it (the plan: "the light rebuilt"). A hero part samples the field
# at its origin, so the whole part answers at once. Its own glow, on its own layer, carries on.
HERO_PARTS_ON_DROP_RING = ("-cap", "-under", "-gills")
HERO_DROP_RING = 1.5
# The regional hue spread, lowered because ADR-904 made it a real rotation of the colour shown.
HUE_FIELD = {"fungi": 0.07}
# Where the bar's rings run: from groove 2, when the track's body arrives, to the suspension; again in
# the drop. Silent while the valley holds its breath (the pull-back), while the saucer drains the light
# (suspension, break), through the riser (the roll drives the fungi instead) and in the tail.
BAR_WAVES = {"groove-2", "lift", "arrival", "melodic-plateau", "lead-forward", "drop"}


# Which heroes the film features, by how much of the cut they fill (the Critic's visibility over the
# first pass's final, 1080p: the elder in most shots at 400-800 px; the lantern 240-340 px in s04,
# s07, s12 and s28; the bloom filling s15; the umbra 480 px in s35; the cairn 80-110 px in s03 and
# s36; the rest 20-40 px specks in wides). The planner hands the music's layers out most important
# first -- the kick, the clap, the downbeat, the breath, the phrase, the lead, the bass, then the
# section change -- and the scene's importances were a plain ramp in the order the heroes were made,
# which gave the bar's swell to the spire, never more than 40 px, and the lead's slow level to the
# umbra's one close-up. Ranked by the cut, the heroes with close-ups take the rhythmic layers.
FEATURED = ["elder-2-cap", "lantern-cap", "bloom-cap", "umbra-cap", "cairn-cap", "spire-cap", "veil-cap",
            "ridge-cap", "scree-cap", "ember-cap"]


def _terrain(scene):
    return next(n for n in scene["nodes"] if n.get("kind") == "terrain")


def prepare(project, scene):
    """What the planner reads, into the scene and the project: the wave fields, the mushroom layers'
    and the heroes' names for them, and the heroes ranked by the cut."""
    names = {f["name"] for f in WAVE_FIELDS}
    scene["nodes"] = [n for n in scene["nodes"] if n.get("name") not in names] + copy.deepcopy(WAVE_FIELDS)
    for layer in _terrain(scene)["scatter"]:
        name = layer.get("name")
        if name in WAVE_LAYERS:
            layer["emissiveField"], layer["emissiveFieldAmount"] = WAVE_LAYERS[name]
        if name in HUE_FIELD:
            layer["hueField"] = HUE_FIELD[name]
    bases = [h[: -len("-cap")] for h in FEATURED]
    for node in scene["nodes"]:
        if node.get("kind") == "procedural" and any(node["name"] == b + p for b in bases for p in HERO_PARTS_ON_DROP_RING):
            node["procedural"]["emissiveField"] = "drop-wave"
            node["procedural"]["emissiveFieldAmount"] = HERO_DROP_RING
    rank = {name: round(0.5 - 0.0215 * i, 4) for i, name in enumerate(FEATURED)}
    for heroes in (scene.get("heroes", []), project.get("heroes", [])):
        for h in heroes:
            if h["name"] in rank:
                h["importance"] = rank[h["name"]]


def wave_tracks():
    """The bar rings on and off by section, stepped on each section's first downbeat: the ring that
    leaves on that downbeat is the first of the new section's (or the first that does not)."""
    from .look import track
    steps = [(0.0 if n == music.SEGMENTS[0][0] else music.segment(n)[0], 1.0 if n in BAR_WAVES else 0.0)
             for n, _, _ in music.SEGMENTS]
    keys = [(steps[0][0], steps[0][1], "linear")]
    for (start, v), (_, prev) in zip(steps[1:], steps):
        if v != prev:
            keys += [(start - 0.001, prev, "linear"), (start, v, "linear")]
    keys.append((music.TAIL_END, steps[-1][1], "linear"))
    return [track("field/bar-wave/strength", keys)]


# ---- EDIT --------------------------------------------------------------------------------------------
# Changes to the planner's items, matched by item key (fnmatch patterns). Each names what it changes
# and why. `chain` entries merge into the route's chain; everything else replaces a route field.
EDITS = [
    # The heartbeat, kept from the first pass (plan section 1): the elder's gold on the kicks the track
    # actually plays -- none in the pull-back or on the four gap beats, half strength in the muffled
    # break, 20 ms ahead of each hit -- rather than the detector's low onsets, which fire 0-18 ms late
    # and add four false hits on the gap beats, the valley's held breaths. A short fall, as the first
    # pass measured it working: gone within the beat, the next kick lands on a dark gill.
    ("kick.elder-2-gills", {"source": "timeline.kick", "chain": {"decayMs": 140.0}},
     "the scored kicks (20 ms lead, silent gaps), falling within the beat"),
    ("kick.elder-2-under", {"source": "timeline.kick", "chain": {"decayMs": 170.0}},
     "the scored kicks; the underside a little slower than the gills, so the pulse spreads"),
    ("kick.elder-2-cap", {"source": "timeline.kick", "chain": {"decayMs": 200.0}},
     "the scored kicks; the cap last and slowest"),
    ("kick.elder-2-spores", {"source": "timeline.kick"}, "the scored kicks"),
    ("kick.elder-practical", {"source": "timeline.kick"}, "the scored kicks: the light the elder throws echoes its heartbeat"),
    # The seams thin in the quiet sections but never vanish: the proposal's x0.70 floor took the tears
    # below their safe range (the validator's OVER_SATURATED warning); x0.80 keeps 0.28.
    ("section.water-tears", {"chain": {"remapOutMin": 0.8}}, "the quiet sections' floor held inside the tears' safe range"),
    # (The bloom's two-bar breath is phased onto the bass glide in SOURCE_PARAMETERS.)
]
# Items taken out, and why. Each is something the film already does another way.
DROPS = {
    # The arc (look.py, from the directives) keys these section by section with the story's shapes --
    # the drain across the suspension, the ramp of the riser -- so a section-energy route on top would
    # count the sections twice, and on the fog it would pull the other way: the proposal thickens the
    # air with energy, the arc clears it for the drop and fills the submerged break.
    "section.fog": "the arc keys the fog per directive; this would thicken the drop the arc clears",
    "section.ecology-light": "the arc keys the ecology light with the valley's own light",
    "section.spores-density": "the arc keys the spores with the sparkle",
    "section.tree-fireflies-density": "the arc keys the fireflies with the sparkle",
    "section.river-motes-density": "the arc keys the river motes with the sparkle",
    "section.water-glow": "the arc keys the water's glow with the valley's light",
    # The drop's ring fires once, in the drop, where the section's depth is its largest and constant:
    # a depth route on it would only rescale it (its amount says how strong it is).
    "section.beacons": "the beacons show the drop's ring, which runs only in the drop",
}
# Items dropped by what they move, for the same reason, where the keys are the planner's to number:
# the heroes' parts show the drop's ring, and the planner gives each part a depth route (30 of them).
DROP_TARGETS = {
    "procedural/*/emissiveFieldAmount": "a hero part shows only the drop's ring; its depth is the drop's",
}
# Parameters of the planner's sources, merged over its own.
SOURCE_PARAMETERS = {
    # beat-synced: position = beats / 8 + phase, a sine 0.5 - 0.5 cos(2 pi position) peaking at 0.5;
    # the glide is beat 4 of bar 2 (beat 7 counting bar 1 beat 1 as 0), led by 20 ms.
    "sources/two-bar-breath/phase": round((0.5 - (7.0 - 0.02 / music.BEAT) / 8.0) % 1.0, 6),
}


def _edit(route, change):
    route = copy.deepcopy(route)
    for k, v in change.items():
        if k == "chain":
            route["chain"].update(v)
        else:
            route[k] = v
    return route


def tune(proposal):
    """(installed routes, plan, notes) after EDITS and DROPS. Refuses to guess: an edit or a drop
    whose item the planner no longer proposes is reported, not silently skipped."""
    install = copy.deepcopy(proposal["install"])
    plan = install["directingPlan"]
    keys = [it["key"] for it in plan["routes"]]
    notes = []
    for pattern, _, why in EDITS:
        if not fnmatch.filter(keys, pattern):
            notes.append(f"edit '{pattern}' matched no proposed item ({why})")
    for key in DROPS:
        if key not in keys:
            notes.append(f"drop '{key}' matched no proposed item")
    for pattern in DROP_TARGETS:
        if not any(fnmatch.fnmatch(it["route"]["target"], pattern) for it in plan["routes"]):
            notes.append(f"drop of '{pattern}' matched no proposed route")
    routes, kept, dropped = [], [], []
    by_key = {r["planItem"].split("/", 1)[1]: r for r in install["routes"]}
    for item in plan["routes"]:
        key = item["key"]
        if key in DROPS or any(fnmatch.fnmatch(item["route"]["target"], p) for p in DROP_TARGETS):
            dropped.append(key)
            continue
        route = by_key[key]
        for pattern, change, why in EDITS:
            if fnmatch.fnmatch(key, pattern):
                route = _edit(route, change)
                item["route"] = _edit(item["route"], change)
                item["reason"] = f"{item['reason']} GV3 edit: {why}."
        routes.append(route)
        kept.append(item)
    plan["routes"] = kept
    # The plan's record of what it produced: a dropped item produced nothing. An edited item keeps
    # its record, so the Director panel knows the production changed it and a revision keeps it.
    plan["produced"] = [p for p in plan["produced"] if p.get("item") not in dropped]
    parameters = dict(install["parameters"])
    parameters.update(SOURCE_PARAMETERS)
    return {"routes": routes, "sources": install["sources"], "parameters": parameters, "plan": plan,
            "dropped": dropped, "notes": notes}


def install(project, tuned):
    """ADR-927's install, as plain JSON edits: a re-install replaces the previous one."""
    project["routes"] = [r for r in project.get("routes", [])
                         if not r.get("planItem", "").startswith("reactivity/")] + tuned["routes"]
    names = {(s["kind"], s["name"]) for s in tuned["sources"]}
    project["sources"] = [s for s in project.get("sources", []) if (s["kind"], s["name"]) not in names] + \
        tuned["sources"]
    project.setdefault("parameters", {}).update(tuned["parameters"])
    plans = [p for p in project.get("directingPlans", []) if p.get("id") != tuned["plan"]["id"]]
    project["directingPlans"] = plans + [tuned["plan"]]


# ---- the engine --------------------------------------------------------------------------------------
def _link(link, target):
    if link.is_symlink() or link.exists():
        return
    link.symlink_to(target)


def scratch_copy(project, scene, name):
    """Write a project and its scene where the engine can load them as it would the real files.
    Returns the project's path."""
    world = SCRATCH / "examples" / "world"
    world.mkdir(parents=True, exist_ok=True)
    _link(SCRATCH / "assets", ROOT / "assets")
    for entry in (ROOT / "examples").iterdir():
        if entry.is_dir() and entry.name != "world":
            _link(SCRATCH / "examples" / entry.name, entry)
    scene_path = world / f"{name}.scene.json"
    scene_path.write_text(json.dumps(scene, indent=1) + "\n")
    p = copy.deepcopy(project)
    audio = p["assets"]["audio"]
    if not pathlib.Path(audio["path"]).is_absolute():
        audio["path"] = str((ROOT / "examples" / "world" / audio["path"]).resolve())
    p["assets"]["scene"]["path"] = {"path": scene_path.name,
                                    "sha256": hashlib.sha256(scene_path.read_bytes()).hexdigest(),
                                    "size": scene_path.stat().st_size}
    project_path = world / f"{name}.json"
    project_path.write_text(json.dumps(p, indent=1) + "\n")
    return project_path


def _run(args, log):
    if not ENGINE.exists():
        raise RuntimeError(f"{ENGINE} is not built (it proposes and audits the film's reactivity)")
    with open(log, "w") as fh:
        done = subprocess.run([str(a) for a in args], stdout=fh, stderr=subprocess.STDOUT, timeout=600)
    if done.returncode != 0:
        raise RuntimeError(f"{' '.join(str(a) for a in args[:3])} ... exited {done.returncode}; see {log}")


def propose(project, scene):
    path = scratch_copy(project, scene, "proposal-input")
    out = SCRATCH / "proposal.json"
    _run([ENGINE, "--project", path, "--propose-reactivity", out, "--fps", "60"], SCRATCH / "proposal.log")
    return json.loads(out.read_text())


def audit(project, scene):
    path = scratch_copy(project, scene, "installed")
    out = SCRATCH / "audit.json"
    _run([ENGINE, "--project", path, "--audit-routes", out, "--fps", "60"], SCRATCH / "audit.log")
    return json.loads(out.read_text())


def apply(project, scene):
    """Propose, edit, install and audit the film's reactivity. Returns a line for the generator's report."""
    proposal = propose(project, scene)
    tuned = tune(proposal)
    install(project, tuned)
    project["timeline"]["tracks"] = [t for t in project["timeline"]["tracks"]
                                     if not t["target"].startswith("field/bar-wave/")] + wave_tracks()
    report = audit(project, scene)
    (SCRATCH / "tuned.json").write_text(json.dumps({k: v for k, v in tuned.items() if k != "plan"}, indent=1))
    bad = [r for r in report["routes"] + report["tracks"] if r["verdict"] != "live"]
    lines = [f"{r['verdict']}: {r.get('source', '')} -> {r['target']}: {r['reason']}" for r in bad]
    for n in tuned["notes"]:
        print("reactivity:", n)
    for line in lines:
        print("reactivity audit:", line)
    s = proposal["summary"]
    levels = {}
    for r in tuned["routes"]:
        item = next(i for i in proposal["items"] if i["key"] == r["planItem"].split("/", 1)[1])
        levels[item["level"]] = levels.get(item["level"], 0) + 1
    return (f"reactivity: {len(tuned['routes'])} of {s['routes']} proposed routes installed "
            f"(micro {levels.get('micro', 0)}, meso {levels.get('meso', 0)}, macro {levels.get('macro', 0)}; "
            f"{len(tuned['dropped'])} dropped), {len(tuned['sources'])} sources; audit: "
            f"{report['summary']['routes']['live']}/{report['summary']['routes']['total']} routes and "
            f"{report['summary']['tracks']['live']}/{report['summary']['tracks']['total']} tracks live")


# ---- the evaluator's view ------------------------------------------------------------------------------
PARTS = ("-cap", "-under", "-gills", "-stem")


def _hero_of(node, heroes):
    for h in heroes:
        base = h[: -len("-cap")]
        if node in {base + p for p in PARTS} or node == f"{base}-spores":
            return h
    return None


def _entities(target, heroes):
    parts = target.split("/")
    if parts[0] in ("nodes", "particles") and len(parts) > 1:
        h = _hero_of(parts[1], heroes)
        if h:
            return [h]
    if "elder-practical" in target:
        return ["elder-2-cap"]
    if target.startswith("material/glowmere2Cap/"):
        return list(heroes)
    if target.startswith("material/glowmere2TissueWarm/"):
        return ["elder-2-cap"]
    if target.startswith("material/glowmere2TissueCool/"):
        return [h for h in heroes if h != "elder-2-cap"]
    return []


def _source_events(project, source):
    """(critic source name, event instants) for a route's source: the instants it answers."""
    kind, _, name = source.partition(".")
    if kind == "timeline":
        src = next((s for s in project["sources"] if s["kind"] == "timeline" and s["name"] == name), None)
        keys = (src or {}).get("settings", {}).get("keys", [])
        if (src or {}).get("settings", {}).get("mode") == "event" or name in ("kick", "gap", "crash"):
            return source, [k["time"] for k in keys if k["value"] > 0]
        return source, None  # a value curve, not events
    bars = [music.bar(n) for n in range(1, 123)]
    if source == "music.downbeat":
        return "beat.downbeat", bars
    if source == "music.phrase":
        return "beat.phrase", bars[::8]
    if source == "section.change":
        return "beat.section", [music.bar(first) for _, first, _ in music.SEGMENTS[1:]]
    if kind == "lfo":
        phase = project["parameters"].get(f"sources/{name}/phase", 0.0)
        per = project["parameters"].get(f"sources/{name}/beatsPerCycle", 1.0)
        crests, k = [], 0
        while True:
            beats = per * (k + 0.5 - phase)
            t = music.FIRST_DOWNBEAT + beats * music.BEAT
            if t > music.TAIL_END:
                break
            if t >= 0.0:
                crests.append(t)
            k += 1
        return f"beat.{name}", crests
    return source, None  # an audio band: the Critic locks to its own onsets of that band


def critic_modulation(project, regions=None):
    """The routes as `critic.scene/1` modulation: source, target, entities (or `region:<id>`), and the
    instants each should answer (its source's events, moved by the route's own delay). Sources the
    Critic's route-locked check does not know by name (music.*, section.*, an LFO) are given as
    `beat.*` with their instants, which is what they are: events on the bar grid."""
    heroes = [h["name"] for h in project.get("heroes", []) if h["name"].endswith("-cap")]
    regions = regions or {}
    out = []
    for r in project.get("routes", []):
        target = r["target"]
        ents = _entities(target, heroes)
        for layer, rid in regions.items():
            if f"/scatter/{layer}/" in target:
                ents = ents + [f"region:{rid}"]
        source, events = _source_events(project, r["source"])
        m = {"source": source, "engine_source": r["source"], "target": target, "amount": r.get("amount"),
             "op": r.get("op"), "enabled": r.get("enabled", True), "entities": ents, "chain": r.get("chain"),
             "plan_item": r.get("planItem", "")}
        if events is not None:
            delay = (r.get("chain") or {}).get("delayMs", 0.0) / 1000.0
            m["events"] = [round(t + delay, 4) for t in events][:2000]
        out.append(m)
    return out


# ---- the command line: the film as written, audited and exported ---------------------------------------
def _written():
    world = ROOT / "examples" / "world"
    project = json.loads((world / "glowmere-valley-3.json").read_text())
    scene = json.loads((world / "glowmere-valley-3.scene.json").read_text())
    return project, scene


def main(argv):
    """python3 -m gv3.reactivity audit                     the written project's route audit
       python3 -m gv3.reactivity snapshot NAME             a scratch copy of the written project
       python3 -m gv3.reactivity critic OUT [REGIONS]      the evaluator's modulation input"""
    project, scene = _written()
    if argv[:1] == ["audit"]:
        report = audit(project, scene)
        print(json.dumps(report["summary"], indent=1))
        bad = [r for r in report["routes"] + report["tracks"] if r["verdict"] != "live"]
        for r in bad:
            print(f"{r['verdict']}: {r.get('source', '')} -> {r['target']}: {r['reason']}")
        return 1 if bad else 0
    if argv[:1] == ["snapshot"]:
        print(scratch_copy(project, scene, argv[1]))
        return 0
    if argv[:1] == ["critic"]:
        regions = json.loads(pathlib.Path(argv[2]).read_text()) if len(argv) > 2 else None
        layers = {}
        if regions:
            for rg in regions:
                layers.setdefault(rg.get("layer"), rg["id"])
        mod = critic_modulation(project, {k: v for k, v in layers.items() if k})
        pathlib.Path(argv[1]).write_text(json.dumps({"modulation": mod}, indent=1))
        print(f"{len(mod)} routes -> {argv[1]}")
        return 0
    print(main.__doc__)
    return 2


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
