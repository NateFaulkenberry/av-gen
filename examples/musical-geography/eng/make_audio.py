"""Phase 7 (engineering): one Musical Geography project per track, with the music wired through the existing modulation
only -- project routes on the response bus, the beat clock and the levels, entity reactions on the aliens, and the
stems as feeds where a song has them. No drum classifier. Every target is a registered parameter art can retune.

    python3 examples/musical-geography/build.py --state night --flight --out DIR
    python3 examples/musical-geography/eng/make_audio.py DIR TRACK [--silent]

TRACK is a file under assets/audio (feline-footwear, night-shift, glowmere-valley, ...). --silent swaps the audio for
digital silence of the same length (the calm test). Writes DIR/<track>.json beside the scene.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
CONTOUR_INTERVAL = 4.0

# ---- the terrain, the contours, the path (project routes) ----------------------------------------------------------
ROUTES = [
    # the swell: low attacks (kick and bass alike) lift every wave train
    {"source": "response.low", "target": "env/tiles/amplitude", "amount": 0.6, "op": "add", "polarity": "unipolar"},
    # a ring dropped ahead of the camera on each onset
    {"source": "response.onset", "target": "env/tiles/rings/trigger", "amount": 1.0, "op": "add",
     "polarity": "unipolar"},
    # how busy the music is: the land breathes deeper in a dense passage (audio.energy is smoothed over 1 s)
    {"source": "audio.energy", "target": "env/tiles/breathing/amplitude", "amount": 0.25, "op": "add",
     "polarity": "unipolar"},
    # the contour lines march one interval up the hills per bar (offset is periodic in the interval: the wrap is seamless)
    {"source": "beat.bar", "target": "env/tiles/contour/offset", "amount": CONTOUR_INTERVAL, "op": "add",
     "polarity": "unipolar"},
    # ...and flash on each beat
    {"source": "beat.pulse", "target": "env/tiles/glow/contour", "amount": 1.5, "op": "add", "polarity": "unipolar",
     "envelope": "peakhold", "envelopeFallPerSecond": 4.0},
    # the path glows with the mids (the melodic register)
    {"source": "response.mid", "target": "env/tiles/glow/path", "amount": 1.6, "op": "add", "polarity": "unipolar",
     "attackMs": 30.0, "decayMs": 250.0},
    # the pads (crown lights) with the highs
    {"source": "response.high", "target": "env/tiles/glow/pads", "amount": 1.0, "op": "add", "polarity": "unipolar",
     "attackMs": 20.0, "decayMs": 300.0},
]

# ---- the aliens (entity reactions: a seek replays them) ------------------------------------------------------------
REACTIONS = [
    # a hop on the beat (the cue's rising edge sets a resting alien off, or turns a walking one's next step into a hop)
    {"signal": "beat.pulse", "target": "hop/trigger", "depth": 1.0},
    # louder music, higher hops and harder landings
    {"signal": "response.level", "target": "hop/apex", "depth": 1.6, "chain": {"attackMs": 80.0, "decayMs": 400.0}},
    {"signal": "response.level", "target": "hop/strength", "depth": 0.8, "chain": {"attackMs": 80.0, "decayMs": 400.0}},
]

# The glow at rest: what silence shows (calm), lifted by the routes above. 1 = the palette as authored.
GLOW_REST = {"path": 0.4, "contour": 0.5, "crest": 1.0, "pads": 0.5}

STEMS = {
    "feline-footwear": {
        "stems": [{"key": "drums", "label": "Drums", "role": "drums", "file": "stems/feline-footwear (Drums).wav"},
                  {"key": "bass", "label": "Bass", "role": "bass", "file": "stems/feline-footwear (Bass).wav"},
                  {"key": "synth", "label": "Synth", "role": "synth", "file": "stems/feline-footwear (Synth).wav"}],
        # the beat and the hits from the drums, the levels from the bass (docs/help/audio-input.md, Feeds)
        "feeds": {"beat": "stem.drums", "hits": "stem.drums", "levels": "stem.bass"},
    },
}


def route(r):
    out = {k: r[k] for k in ("source", "target", "amount", "op", "polarity")}
    chain = {k: r[k] for k in ("attackMs", "decayMs", "envelope", "envelopeFallPerSecond") if k in r}
    if chain:
        out["chain"] = chain  # nested: the route reader refuses a chain key written on the route itself (CP-D r2)
    return out


def main():
    out, track = sys.argv[1], sys.argv[2]
    silent = "--silent" in sys.argv
    audio_dir = os.path.join(REPO, "assets", "audio")
    audio = os.path.join(audio_dir, track + ".wav")
    if silent:
        seconds = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                        audio], capture_output=True, text=True).stdout)
        quiet = os.path.join(out, "silence-%d.wav" % int(seconds))
        if not os.path.exists(quiet):
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                            "anullsrc=r=48000:cl=stereo", "-t", str(seconds), quiet], check=True)
        audio = quiet
    scene_path = os.path.join(out, "musical-geography.scene.json")
    scene = json.load(open(scene_path))
    for e in scene.get("entities", []):
        e["reactions"] = [dict(r) for r in REACTIONS]
    scene["environment"]["tileDynamics"]["glow"] = dict(GLOW_REST)
    json.dump(scene, open(scene_path, "w"), indent=1)
    proj = json.load(open(os.path.join(out, "musical-geography.json")))
    proj["assets"]["audio"] = audio
    proj["routes"] = [route(r) for r in ROUTES]
    extra = STEMS.get(track) if not silent else None
    if extra:
        proj["assets"]["stems"] = [dict(s, file=os.path.join(audio_dir, s["file"])) for s in extra["stems"]]
        proj["feeds"] = extra["feeds"]
    name = track + ("-silent" if silent else "")
    json.dump(proj, open(os.path.join(out, name + ".json"), "w"), indent=1)
    print("wrote", os.path.join(out, name + ".json"))


if __name__ == "__main__":
    main()
