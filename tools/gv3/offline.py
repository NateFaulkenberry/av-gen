"""The final render: Glowmere Valley 3 at the offline renderer's full quality, in 4K.

    python3 tools/make_glowmere_valley_3.py --final

applies this, and nothing else does: without `--final` the project keeps its preview settings, so a
preview renders as fast as it always did. What follows is docs/glowmere-valley-3/revision/audit/
reports/render-post.md's recommended configuration, each value with the reason it is the one.

The engine's offline tier already lifts everything that is a *cost* (ADR-186/191: the procedural
distance cull, the rig pose rate and the entity behaviour bands). What it leaves alone is what the
engine treats as *composition*, and those the scene has to author (ADR-112, docs/rendering.md):
how far shadows reach, how many cascades, how far terrain is drawn at full detail, whether the fog is
marched. Raising a distance the tier already lifted changes nothing; these are the ones that do.

Post (tone map, bloom, grain and the rest) is not here: gv3-look and the render stream own it.
"""

import math

from . import world

OUTPUT = "../../build/gv3/final/glowmere-valley-3-2160p.mov"
RENDER = {
    # 3840x2160 through --size's own path, supersampled x2: the ceiling (render_settings.cpp), and the
    # only factor at which the one-tap resolve is an exact 2x2 box. `limits: tier` is the offline
    # default of ADR-191 -- everything lifted except the LOD ladder, which is the renderer's only
    # prefilter for sub-pixel geometry (lifting it raised flicker 57%). ProRes 422 rather than h264
    # at quality 90 (32.7 Mbit/s), which smeared the grain and the dark gradients.
    "width": 3840, "height": 2160, "fps": 60.0, "tier": "offline", "limits": "tier", "supersample": 2.0,
    "codec": "prores422", "quality": 95, "aovs": "", "output": "video", "muxAudio": True, "path": OUTPUT,
}

# Shadows. ADR-112 sizes the automatic range so the coarsest cascade's texel is 8 cm, which is ~77 m
# at every tier, and beyond it the moon -- the light rig's description of how the valley gets its
# form -- does nothing. The texel is 2.12 x range / resolution, so at the offline tier's 4096 a
# 160 m range keeps 8.3 cm, and 300 m gives 15.5 cm: fine for a valley seen from a hundred metres,
# coarse for a character's contact shadow. So 160 is the base and 300 is keyed on the shots whose
# frame is mostly ground beyond 160 m, measured below, not listed by hand (the cut is gv3-cut's).
SHADOW_RANGE = 160.0
SHADOW_RANGE_WIDE = 300.0
WIDE_FRACTION = 0.2             # of the frame's pixels on ground beyond SHADOW_RANGE
TERRAIN_SHADOW_DISTANCE = 320.0  # the terrain casts only within this; it was 150
# The scene's 3 beat the offline tier's 4 (scene_renderer.cpp: a scene's count wins); 0 = the tier's.
SHADOW_CASCADES = 0

# Terrain. The grid spans [-320, 352]^2, so a 1000 m view distance reaches every chunk from anywhere
# in it, and with the LOD off every chunk is drawn at LOD 0 (1.2 m quads). With it on, the skyline was
# drawn at LOD 1-2, 15-26 px per quad at both 1080p and 4K: faceted ridgelines.
TERRAIN_VIEW_DISTANCE = 1000.0

# The fog march. The project's `scene/volumeMaxDistance` 0 switched the march off (ADR-705: 0 means
# the closed form carries the whole ray), so the scene's noise, local lights and step count were all
# inert. 220 m is ADR-705's flagship reach for Glowmere, 32 steps its offline count, jitter 0.5 the
# ADR-461 value, and horizon density 1 the knob ADR-705 measured matching the far band's veil.
MARCH = {"scene/volumeMaxDistance": 220.0, "scene/volumeSteps": 32, "scene/volumeJitter": 0.5,
         "scene/horizonDensity": 1.0}
# The march lights the air it covers, which the closed form never did. Per step its extinction is
# density x volumeAbsorption and its in-scattering density x volumeScattering (shaders/volume.wgsl), so
# the arc's density -- gv3-look's, keyed on the timeline, and every route onto it -- is left exactly as
# the previews have it, which keeps the veil (the extinction) the same, and the light the march adds
# is set here. Calibrated on 4K ranges against the preview (phase3/world.md).
MARCH_SCATTERING = 0.5


def _set_track(project, target, keys):
    tracks = project["timeline"]["tracks"]
    tracks[:] = [t for t in tracks if t["target"] != target]
    tracks.append({"target": target, "component": -1, "timeBase": "seconds", "mode": "replace",
                   "loopLength": 0.0, "enabled": True, "keys": keys})


def far_fraction(field, eye, target, vfov_deg, reach, aspect=16 / 9, cols=24, rows=14, step=2.0):
    """The share of a view's pixels that land on ground more than `reach` metres away (horizontally),
    by marching a grid of rays over the world's heightfield."""
    np = field.np
    f = np.array([target[k] - eye[k] for k in range(3)], dtype=float)
    f /= np.linalg.norm(f)
    right = np.cross(f, [0.0, 1.0, 0.0])
    right /= max(np.linalg.norm(right), 1e-9)
    up = np.cross(right, f)
    th = math.tan(math.radians(vfov_deg) / 2)
    ts = np.arange(step, 1400.0, step)
    far = 0
    for j in range(rows):
        v = (1 - 2 * (j + 0.5) / rows) * th
        for i in range(cols):
            u = (2 * (i + 0.5) / cols - 1) * th * aspect
            d = f + u * right + v * up
            d /= np.linalg.norm(d)
            x, y, z = eye[0] + d[0] * ts, eye[1] + d[1] * ts, eye[2] + d[2] * ts
            inside = (x >= world.GRID_LO) & (x <= world.GRID_HI) & (z >= world.GRID_LO) & (z <= world.GRID_HI)
            under = inside & (y <= field.at(x, z))
            if under.any():
                k = int(np.argmax(under))
                if math.hypot(x[k] - eye[0], z[k] - eye[2]) > reach:
                    far += 1
    return far / (cols * rows)


def wide_shots(project, scene, field, trace=None):
    """{shot index: far fraction} for every shot of the cut; fixed and keyed rigs from their keys,
    followed and aimed ones only from a cast trace (`avgen_cast_trace --camera`), else 0."""
    shots = scene["cameraDirection"]["shots"]
    cameras = {c["id"]: c.get("slug", "") for c in scene["cameraDirection"]["cameras"]}
    fractions = {i: 0.0 for i in range(len(shots))}
    by_slug = {}
    for label, t, eye, target, vfov in world.camera_poses(project, scene, per_shot=3):
        by_slug.setdefault(label, []).append(far_fraction(field, eye, target, vfov, SHADOW_RANGE))
    cam = None
    if trace:
        import json
        cam = json.loads(open(trace).read())["camera"]
    for i, s in enumerate(shots):
        slug = cameras[s["camera"]]
        fs = list(by_slug.get(slug, []))
        if cam is not None and not fs:
            # A traced frame counts for this shot only when it lies in the shot's span AND the trace's
            # active camera is this shot's camera: a trace of another cut says nothing about this one.
            for k in range(0, len(cam["t"]), 60):
                if s["start"] <= cam["t"][k] < s["end"] and cam["camera"][k] == slug:
                    fs.append(far_fraction(field, cam["eye"][k], cam["target"][k], cam["vfov"][k], SHADOW_RANGE))
        if fs:
            fractions[i] = sum(fs) / len(fs)
    return fractions


def apply(project, scene, report, trace=None):
    """Turn the generated project into the final render's. Run after the cut is installed."""
    params = project["parameters"]
    render = project.setdefault("render", {})
    render.update(RENDER)

    scene["environment"]["shadowCascades"] = SHADOW_CASCADES
    terrain = next(n for n in scene["nodes"] if n.get("kind") == "terrain" and "world" in n)
    terrain["terrain"]["shadowDistance"] = TERRAIN_SHADOW_DISTANCE
    name = terrain["name"]
    params[f"nodes/{name}/terrainViewDistance"] = TERRAIN_VIEW_DISTANCE
    params[f"nodes/{name}/terrainLod"] = False

    field = world.Field(terrain["world"])
    fractions = wide_shots(project, scene, field, trace)
    cut = scene["cameraDirection"]["shots"]
    keys, wides = [], []
    for i, shot in enumerate(cut):
        value = SHADOW_RANGE_WIDE if fractions[i] >= WIDE_FRACTION else SHADOW_RANGE
        if value == SHADOW_RANGE_WIDE:
            wides.append(shot.get("label", str(i)).split(" ")[0])
        if not keys or keys[-1]["value"][0] != value:
            keys.append({"time": round(shot["start"], 6), "value": [value], "interp": "step"})
    params["scene/shadowRange"] = SHADOW_RANGE
    _set_track(project, "scene/shadowRange", keys)

    params.update(MARCH)
    params["scene/volumeScattering"] = MARCH_SCATTERING

    rigs = [n for n in scene["nodes"] if "animation" in n]
    for n in rigs:
        n["animation"]["updateHz"] = 0  # every frame: at 30 Hz in a 60 fps film distant bodies slid
    report.append(f"final: {RENDER['width']}x{RENDER['height']} x{RENDER['supersample']:g}, tier {RENDER['tier']}, "
                  f"limits {RENDER['limits']}, {RENDER['codec']}; shadows {SHADOW_RANGE:g} m "
                  f"({SHADOW_RANGE_WIDE:g} m on {len(wides)} wides: {', '.join(wides)}), terrain LOD 0 to "
                  f"{TERRAIN_VIEW_DISTANCE:g} m, the fog marched to {MARCH['scene/volumeMaxDistance']:g} m at "
                  f"scattering {MARCH_SCATTERING:g}, {len(rigs)} rigs posed every frame")
