#!/usr/bin/env python3
"""SONIC ABSTRACT: the abstract direction's nine prototypes (04-brief-abstract-direction.md as amended by briefs 05-08),
their own project (examples/sonic-abstract/, the index category "Sonic Abstract"). Builds them, and makes the review
media: each a still, reactive clips with real music, a MIDI clip, and its modulation map.

    python3 tools/sonic_vfx/abstract.py build [module ...]             # write examples/sonic-abstract/ (no module: all
                                                                       #   nine, and the index's Sonic Abstract entries)
    python3 tools/sonic_vfx/abstract.py build <module> --projects DIR  # look development elsewhere
    python3 tools/sonic_vfx/abstract.py music                          # cut the real-music excerpts (gitignored)
    python3 tools/sonic_vfx/abstract.py blockout <id> [--at 6] [--projects DIR] [--tag v2]  # silent, 960x540
    python3 tools/sonic_vfx/abstract.py silent <id> [--at 6] [--size 1920x1080]             # the silent still
    python3 tools/sonic_vfx/abstract.py stills [id ...] [--size 1920x1080] [--tag v3]       # many silent stills,
                                                                       #   one render queue (one short GPU job)
    python3 tools/sonic_vfx/abstract.py clip <id> [--class allyougot] [--seconds 30] [--size 1920x1080]
    python3 tools/sonic_vfx/abstract.py clips [id ...] [--class allyougot] [--seconds 30]   # many clips, one queue
    python3 tools/sonic_vfx/abstract.py frame <id> --class allyougot --at 12   # one frame of a reactive clip
    python3 tools/sonic_vfx/abstract.py sheet [--blockouts]                     # contact sheet of the nine
    python3 tools/sonic_vfx/abstract.py maps [id ...]                           # each one's as-built modulation map
    python3 tools/sonic_vfx/abstract.py tour [--class allyougot] [--seconds 3.6] # one piece of music, nine worlds
    python3 tools/sonic_vfx/abstract.py notes [id ...]                          # each one's one-page note

Files go to ~/Desktop/av-gen-review/28-sonic-abstract/, named by set-list position (`01-sacred-geometry-still.png`,
`01-sacred-geometry-allyougot.mp4`). Every render goes through tools/gpu-lock.sh with the pinned engine (review.py's
PIN), one job per hold.

Classes: `allyougot` and `rebuild` are 30 s excerpts of the owner's tracks (audio only); `full`, `chords`, `lead`, ...
are tools/sonic_vfx/make_test_material.py's synthesized MIDI material (audio plus the MIDI file).
"""
import argparse
import glob
import importlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review, kit  # noqa: E402
from tools.sonic_vfx.variant import make_silent  # noqa: E402

OUT = os.path.expanduser("~/Desktop/av-gen-review/28-sonic-abstract")
# The excerpts: (source on the owner's Desktop, start second, length). All You Got from 34 s builds into the bass
# entry at about 42 s; Rebuild from 156 s holds a breakdown (162-178 s) and the return at 180 s.
MUSIC = {
    "allyougot": (os.path.expanduser("~/Desktop/All You Got.wav"), 34.0, 30.0),
    "rebuild": (os.path.expanduser("~/Desktop/Rebuild.mp3"), 156.0, 30.0),
}


def scene_ids():
    from tools.sonic_vfx.scenes import ABSTRACT
    return [importlib.import_module("tools.sonic_vfx.scenes." + n).ID for n in ABSTRACT]


def hero_at(sid, default):
    """A module's HERO_AT (the second its hero still is taken at), else `default`."""
    from tools.sonic_vfx.scenes import ABSTRACT
    for n in ABSTRACT:
        m = importlib.import_module("tools.sonic_vfx.scenes." + n)
        if m.ID == sid:
            return float(getattr(m, "HERO_AT", default))
    return default


def build(modules, out_dir=None):
    from tools.sonic_vfx.scenes import ABSTRACT
    names = modules or ABSTRACT
    built = []
    for name in names:
        sc = importlib.import_module("tools.sonic_vfx.scenes." + name).build()
        pp, _ = sc.write(out_dir or kit.ABSTRACT_DIR)
        built.append(sc)
        print("%-24s %3d nodes %2d effects %4d routes %3d mappings -> %s" % (
            sc.id, len(sc.nodes), len(sc.effects), len(sc.routes),
            len(sc.mappings) + len(sc.mappings2) + len(sc.mappings3), os.path.relpath(pp, REPO)))
    if not modules and not out_dir:
        kit.write_index(built, category=kit.ABSTRACT_CATEGORY, out_dir=kit.ABSTRACT_DIR, doc="ABSTRACT-PLAN.md")
        print("examples/index.json: the %s category lists %d scenes" % (kit.ABSTRACT_CATEGORY, len(built)))


def numbered(sid):
    ids = scene_ids()
    n = ids.index(sid) + 1 if sid in ids else 99
    return "%02d-%s" % (n, sid)


def project_path(sid, projects=None):
    return os.path.join(projects or kit.ABSTRACT_DIR, sid + ".json")


def cut_music():
    for cls, (src, start, length) in MUSIC.items():
        dst = os.path.join(REPO, "assets", "audio", review.REAL_MUSIC[cls])
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(start), "-t", str(length), "-i", src, "-ar",
                        "48000", "-ac", "2", "-c:a", "pcm_s16le", dst], check=True)
        print(dst)


def silent_still(sid, at, size, dst, projects=None, tier="realtime", warmup=240):
    work = os.path.join(OUT, "work", "silent")
    os.makedirs(work, exist_ok=True)
    proj = make_silent(project_path(sid, projects), os.path.join(work, sid + "--silent.json"))
    d = os.path.join(work, "%s--%s" % (sid, size))
    shutil.rmtree(d, ignore_errors=True)
    review.run(["--headless", "--project", proj, "--render", d, "--format", "png", "--range",
                "%.3f:%.3f" % (at, at + 0.04), "--size", size, "--fps", "25", "--particle-warmup", str(warmup),
                "--tier", tier])
    got = sorted(glob.glob(os.path.join(d, "*.png")))
    if not got:
        print("no frame rendered for", sid)
        return None
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    shutil.copy(got[0], dst)
    print(dst)
    return dst


def reactive_clip(sid, cls, size, seconds, dst, projects=None, tier="realtime"):
    work = os.path.join(OUT, "work", "reactive")
    os.makedirs(work, exist_ok=True)
    proj, dur = review.variant(sid, cls, work, projects or kit.ABSTRACT_DIR)
    end = min(dur, seconds) if seconds else dur
    review.run(["--headless", "--project", proj, "--render", dst, "--range", "0:%.2f" % end, "--size", size,
                "--fps", "30", "--codec", "h264", "--quality", "90", "--particle-warmup", "120", "--tier", tier])
    print(dst)
    return dst


def clip_frame(clip, at, dst):
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "%.3f" % at, "-i", clip, "-frames:v", "1", "-update", "1",
                    dst], check=False)
    print(dst)


def sheet(dst, blockouts=False, cols=3):
    from PIL import Image, ImageDraw
    if blockouts:
        paths = [os.path.join(OUT, "work", "blockout", s + ".png") for s in scene_ids()]
    else:
        paths = [os.path.join(OUT, numbered(s) + "-still.png") for s in scene_ids()]
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        sys.exit("no stills yet")
    w, h = 640, 360  # three by three: a 1920x1080 sheet
    rows = (len(paths) + cols - 1) // cols
    canvas = Image.new("RGB", (w * cols, h * rows), (0, 0, 0))
    d = ImageDraw.Draw(canvas)
    for i, p in enumerate(paths):
        im = Image.open(p).convert("RGB").resize((w, h), Image.LANCZOS)
        x, y = (i % cols) * w, (i // cols) * h
        canvas.paste(im, (x, y))
        d.text((x + 8, y + 6), os.path.basename(p).replace("-still.png", "").replace(".png", ""),
               fill=(255, 255, 255))
    canvas.save(dst, quality=92)
    print(dst)


# What each signal is, in the owner's words (the brief's audio dimensions), for the modulation maps.
DIMENSIONS = [
    ("Level (loudness)", ("response.level",)),
    ("Bass", ("response.bass",)),
    ("Kick (onset, low)", ("response.kick", "response.kickEnv", "response.low")),
    ("Snare (onset, mid)", ("response.snare", "response.snareEnv")),
    ("Hats and highs", ("response.hat", "response.hatEnv", "response.hatRate", "audio.treble")),
    ("Any onset", ("response.onset", "response.onsetEnv")),
    ("Mids", ("audio.mid", "response.melodic")),
    ("Spectral bands", ("audio.bass", "audio.lowMid", "audio.highMid")),
    ("Centroid (brightness)", ("sonic.brightness", "sonic.brightness.slow", "audio.spectralCentroid")),
    ("Flux", ("response.flux",)),
    ("Sustain", ("response.sustain",)),
    ("Tempo and beat", ("beat.pulse", "beat.bar", "beat.phase", "beat.bpm")),
    ("Intensity (12 s dynamics)", ("response.intensity",)),
    ("MIDI notes", ("notes.lastPitch", "response.note", "response.noteEnv", "notes.noteOn", "visual.")),
    ("MIDI velocity", ("notes.lastVelocity",)),
    ("MIDI chords", ("notes.polyphony", "notes.class.", "notes.chord", "notes.tension")),
    ("MIDI voices", ("notes.voice.",)),
    ("Held notes and releases", ("notes.held", "notes.release")),
    ("Mod wheel (CC 1)", ("control.modwheel",)),
]
STRUCTURAL = ("distribution/count", "majorSegments", "radialSegments", "node/polygon/count", "node/rooms/count",
              "accordion/size", "lattice/size", "distribution/radius", "/radiusGrowth", "spline/", "spawnRate")


def dimension_of(src):
    for name, prefixes in DIMENSIONS:
        if any(src == p or (p.endswith(".") and src.startswith(p)) for p in prefixes):
            return name
    return "Other"


def describe_chain(r):
    c = r.get("chain", {})
    bits = []
    if c.get("integrate"):
        bits.append("integrated (a rate into a position)")
    if c.get("delayMs"):
        bits.append("delay %d ms" % c["delayMs"])
    if c.get("threshold") == "binary":
        bits.append("a switch above %.2f" % c.get("thresholdLevel", 0.5))
    if c.get("springHz"):
        bits.append("spring %.1f Hz" % c["springHz"])
    if "attackMs" in c or "decayMs" in c:
        bits.append("attack %d / decay %d ms" % (c.get("attackMs", 0), c.get("decayMs", 0)))
    if r.get("op") == "multiply":
        g, o = c.get("gain", 1.0), c.get("offset", 0.0)
        if g != 1.0 or o:
            bits.append("scales it to %g at full input (x%g at rest)" % (o + g, o))
        else:
            bits.append("scales it")
    elif c.get("gain", 1.0) != 1.0 or c.get("offset"):
        bits.append("x %g %+g" % (c.get("gain", 1.0), c.get("offset", 0.0)))
    if r.get("depthSource"):
        bits.append("scaled by %s" % r["depthSource"])
    return "; ".join(bits)


def write_map(sid):
    import json
    proj = json.load(open(project_path(sid)))
    d = proj.get("sonicScene", {})
    routes = proj.get("routes", [])
    groups = {}
    for r in routes:
        groups.setdefault(dimension_of(r["source"]), []).append(r)
    lines = ["# %s: %s" % (numbered(sid), d.get("title", sid)), "", d.get("thesis", ""), "",
             "Project: `examples/sonic-abstract/%s.json`. %d routes; `[S]` marks a STRUCTURAL target (a count, an "
             "order, a spacing, a recursion depth, a spline's shape)." % (sid, len(routes)), "",
             "## In plain words", ""]
    for row in d.get("vocabulary", []):
        lines.append("- **%s** (`%s`): %s" % (row[0], row[1], row[2]))
    lines += ["", "## As built: every route, by audio dimension", ""]
    order = [n for n, _ in DIMENSIONS] + ["Other"]
    for dim in order:
        rs = groups.get(dim)
        if not rs:
            continue
        lines += ["### %s" % dim, "", "| signal | drives | amount | shaping |", "|---|---|---|---|"]
        for r in rs:
            tgt = r["target"] + ("[%d]" % r["component"] if "component" in r else "")
            s_mark = " `[S]`" if any(k in r["target"] for k in STRUCTURAL) else ""
            lines.append("| `%s` | `%s`%s | %g | %s |" % (r["source"], tgt, s_mark, r["amount"], describe_chain(r)))
        lines.append("")
    maps_ = [m for src in proj.get("sources", []) if src.get("kind") == "interpret"
             for m in src.get("settings", {}).get("mappings", [])]
    if maps_:
        lines += ["## Derived signals (interpret mappings)", "",
                  "%d mappings build the `visual.*` signals above (for example a note's place: `visual.<name>Hit<k>` "
                  "is a note-on that lands at place k)." % len(maps_), ""]
    dst = os.path.join(OUT, numbered(sid) + "-modulation.md")
    open(dst, "w").write("\n".join(lines) + "\n")
    print(dst)


def make_caption(text, dst, size=(1920, 1080)):
    """A transparent 1080p overlay with the caption at the bottom left (this ffmpeg has no drawtext)."""
    from PIL import Image, ImageDraw, ImageFont
    im = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 30)
    except OSError:
        font = ImageFont.load_default()
    d.text((42, size[1] - 72), text, fill=(255, 255, 255, 215), font=font)
    im.save(dst)


def tour(cls, seg):
    """One continuous piece of music through the nine worlds: every clip of `cls` starts the same excerpt at 0, so
    prototype k's clip cut at [k seg, (k + 1) seg] continues the music where the previous one stopped. Hard cuts, the
    excerpt's own audio underneath, each segment captioned with its prototype."""
    work = os.path.join(OUT, "work", "tour-" + cls)
    os.makedirs(work, exist_ok=True)
    parts = []
    for k, sid in enumerate(scene_ids()):
        src = os.path.join(OUT, "%s-%s.mp4" % (numbered(sid), cls))
        if not os.path.exists(src):
            print("missing", src)
            continue
        part = os.path.join(work, "%02d.mp4" % k)
        caption = os.path.join(work, "%02d-caption.png" % k)
        make_caption(numbered(sid).split("-", 1)[0] + "   " + sid.replace("-", " ").upper(), caption)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "%.3f" % (k * seg), "-t", "%.3f" % seg, "-i", src,
                        "-i", caption, "-filter_complex", "[0:v]scale=1920:1080[b];[b][1:v]overlay=0:0", "-an",
                        "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p", "-r", "30", part], check=True)
        parts.append(part)
    listing = os.path.join(work, "parts.txt")
    open(listing, "w").write("".join("file '%s'\n" % p for p in parts))
    silent = os.path.join(work, "video.mp4")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0", "-i", listing, "-c", "copy", silent],
                   check=True)
    wav = review.material(cls)[0]
    dst = os.path.join(OUT, "00-tour-%s.mp4" % cls)
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", silent, "-t", "%.3f" % (seg * len(parts)), "-i", wav,
                    "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-shortest", dst],
                   check=True)
    print(dst)


def write_note(sid):
    from tools.sonic_vfx.abstract_notes import NOTES
    import json
    n = NOTES[sid]
    proj = json.load(open(project_path(sid)))
    title = proj.get("sonicScene", {}).get("title", sid)
    num = numbered(sid)
    lines = ["# %s: %s (%s)" % (num.split("-", 1)[0], title, n["working_title"]), "",
             "**Open it:** File > Examples > Sonic Abstract > %s (`examples/sonic-abstract/%s.json`). Live input turns "
             "on when it opens." % (title, sid), "",
             "## What it is", "", n["language"], "",
             "## What drives what", ""]
    lines += ["- " + d for d in n["drives"]]
    lines += ["", "The full as-built map, every route by audio dimension: `%s-modulation.md`." % num, "",
              "## What would expand it into more scenes", ""]
    lines += ["- " + e for e in n["expand"]]
    if n.get("verdict"):
        lines += ["", "## Verdict", "", n["verdict"]]
    lines += ["", "## Media", "", "- `%s-still.png`: the frame with no modulation at all (the silent test)." % num]
    for cls, what in (("allyougot", "with All You Got (34-64 s)"), ("rebuild", "with Rebuild (156-186 s)"),
                      ("full", "with the synthesized MIDI test material (pads, bass, lead and drums with their notes)")):
        if os.path.exists(os.path.join(OUT, "%s-%s.mp4" % (num, cls))):
            lines.append("- `%s-%s.mp4`: the reactive clip %s." % (num, cls, what))
    dst = os.path.join(OUT, num + "-note.md")
    open(dst, "w").write("\n".join(lines) + "\n")
    print(dst)


def stills_queue(ids, at, size, tag, projects=None, tier="realtime"):
    """Silent stills of several prototypes in ONE engine process (a render queue): one short GPU job instead of one
    lock hold per still. 1080p stills go to NN-<id>-still.png, other sizes (or a tag) to work/blockout/<id>[-tag].png."""
    import json
    work = os.path.join(OUT, "work", "silent")
    os.makedirs(work, exist_ok=True)
    w, h = (int(v) for v in size.split("x"))
    jobs, outs = [], []
    for sid in ids:
        proj = make_silent(project_path(sid, projects), os.path.join(work, sid + "--silent.json"))
        d = os.path.join(work, "%s--%s-q" % (sid, size))
        shutil.rmtree(d, ignore_errors=True)
        # a queue job takes no --particle-warmup (the queue copies only codec, quality and a few others), so each still
        # is the LAST frame of a 3-second pre-roll: the particles have had time to fill
        t_at = hero_at(sid, at) if not tag else at
        jobs.append({"project": proj, "render": {"width": w, "height": h, "fps": 25, "start": max(0.0, t_at - 3.0),
                                                 "end": t_at + 0.02, "output": "png", "path": d, "tier": tier}})
        if size == "1920x1080" and not tag:
            dst = os.path.join(OUT, numbered(sid) + "-still.png")
        else:
            dst = os.path.join(OUT, "work", "blockout", sid + ("-" + tag if tag else "") + ".png")
        outs.append((d, dst))
    qf = os.path.join(work, "queue.json")
    json.dump({"format": "avgen-render-queue", "jobs": jobs}, open(qf, "w"), indent=1)
    review.run(["--headless", "--queue", qf, "--particle-warmup", "240"])
    for d, dst in outs:
        got = sorted(glob.glob(os.path.join(d, "*.png")))
        if got:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(got[-1], dst)
            print(dst)
        else:
            print("no frame for", dst)


def clips_queue(ids, cls, size, seconds, projects=None, tier="realtime"):
    """Reactive clips of several prototypes in ONE engine process (a render queue, one GPU lock hold). A queue job
    takes no particle warm-up, so each clip opens with its particles assembling (the music excerpt starts mid-song)."""
    import json
    work = os.path.join(OUT, "work", "reactive")
    os.makedirs(work, exist_ok=True)
    w, h = (int(v) for v in size.split("x"))
    jobs, outs = [], []
    for sid in ids:
        proj, dur = review.variant(sid, cls, work, projects or kit.ABSTRACT_DIR)
        end = min(dur, seconds) if seconds else dur
        dst = os.path.join(OUT, "%s-%s.mp4" % (numbered(sid), cls))
        jobs.append({"project": proj, "render": {"width": w, "height": h, "fps": 30, "start": 0.0, "end": end,
                                                 "output": "video", "path": dst, "codec": "h264", "quality": 90,
                                                 "muxAudio": True, "tier": tier}})
        outs.append(dst)
    qf = os.path.join(work, "queue-%s.json" % cls)
    json.dump({"format": "avgen-render-queue", "jobs": jobs}, open(qf, "w"), indent=1)
    review.run(["--headless", "--queue", qf])
    for dst in outs:
        print(dst, "ok" if os.path.exists(dst) else "MISSING")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["build", "music", "silent", "blockout", "clip", "frame", "sheet", "maps",
                                         "tour", "notes", "stills", "clips"])
    ap.add_argument("scene", nargs="*")
    ap.add_argument("--at", type=float, default=6.0)
    ap.add_argument("--size", default="")
    ap.add_argument("--class", dest="cls", default="allyougot")
    ap.add_argument("--seconds", type=float, default=0.0)
    ap.add_argument("--projects", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--tag", default="", help="a suffix for a blockout iteration (work/blockout/<id>-<tag>.png)")
    ap.add_argument("--tier", default="realtime")
    ap.add_argument("--blockouts", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    proj = a.projects or None
    if a.mode == "build":
        build(a.scene, proj)
        return
    if a.mode == "tour":
        tour(a.cls, a.seconds or 3.6)
        return
    if a.mode == "clips":
        clips_queue(a.scene or scene_ids(), a.cls, a.size or "1920x1080", a.seconds, a.projects or None, a.tier)
        return
    if a.mode == "stills":
        stills_queue(a.scene or scene_ids(), a.at, a.size or "1920x1080", a.tag, a.projects or None, a.tier)
        return
    if a.mode == "notes":
        for sid in (a.scene or scene_ids()):
            write_note(sid)
        return
    if a.mode == "maps":
        for sid in (a.scene or scene_ids()):
            write_map(sid)
        return
    a.scene = a.scene[0] if a.scene else None
    if a.mode == "music":
        cut_music()
    elif a.mode == "silent":
        silent_still(a.scene, a.at, a.size or "1920x1080",
                     a.out or os.path.join(OUT, numbered(a.scene) + "-still.png"), proj, a.tier)
    elif a.mode == "blockout":
        name = a.scene + ("-" + a.tag if a.tag else "") + ".png"
        silent_still(a.scene, a.at, a.size or "960x540", a.out or os.path.join(OUT, "work", "blockout", name), proj,
                     a.tier)
    elif a.mode == "clip":
        reactive_clip(a.scene, a.cls, a.size or "1920x1080", a.seconds,
                      a.out or os.path.join(OUT, "%s-%s.mp4" % (numbered(a.scene), a.cls)), proj, a.tier)
    elif a.mode == "frame":
        clip = os.path.join(OUT, "%s-%s.mp4" % (numbered(a.scene), a.cls))
        clip_frame(clip, a.at, a.out or os.path.join(OUT, "%s-%s-%04.1f.png" % (numbered(a.scene), a.cls, a.at)))
    else:
        sheet(a.out or os.path.join(OUT, "00-contact-sheet%s.jpg" % ("-blockouts" if a.blockouts else "")),
              a.blockouts)


if __name__ == "__main__":
    main()
