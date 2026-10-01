#!/usr/bin/env python3
"""Generates examples/liminal/all-you-got-pass2{,.scene,.rig}.json: the art pass 2 music video for the owner's
song *All You Got* (docs/prototypes/liminal-space/02-art-pass-2.md governs; the plan is PASS2-PLAN.md).

A primitive simulation boots up out of black and builds a home out of light; we wander its small furnished
rooms; it breaks; it grows into a tree of rooms; we sit with its objects; it dances; it opens onto a line-drawn
landscape, crashes in colour, and dissolves at dawn into the point it came from.

    python3 tools/liminal/make_all_you_got_pass2.py            # the film
    python3 tools/liminal/make_all_you_got_pass2.py --kit      # a stills project of every world (kit preview)

Times are the owner's bar numbering through tools/liminal/pass2_grid.py (`t(bar, beat)`).
"""

from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import kit as K  # noqa: E402
import pass2_grid as G  # noqa: E402
import rooms2 as RM  # noqa: E402
from film2 import Film, mix, mul, srgb  # noqa: E402

OUT = os.path.join(ROOT, "examples", "liminal")
END = 258.0
t = G.t

GRID = {"origin": G.BAR1, "beatsPerBar": 4, "tempo": [{"bar": 1, "bpm": 109}, {"bar": 75, "bpm": 111}],
        "sections": {"intro": 1, "release": 17, "verse1": 25, "pause": 41, "letgo": 42, "verse2": 50,
                     "bridgein": 66, "bridge1": 67, "bridge2": 75, "bridge3": 83, "chorus": 91, "ending": 115}}

# ---- palettes (PASS2-PLAN.md §3): sRGB hex -> linear; roles bound below -----------------------------------
ROLES = ["wall", "furn", "furn2", "figure", "fog", "fill", "glow", "screen", "canvas", "canvas2", "glass", "word",
         "accent"]


def P(name, wall, furn, furn2, figure, fog, glow, screen, canvas, canvas2, glass, word, accent, fill="#050507"):
    return name, {"wall": srgb(wall), "furn": srgb(furn), "furn2": srgb(furn2), "figure": srgb(figure),
                  "fog": srgb(fog), "fill": srgb(fill), "glow": srgb(glow), "screen": srgb(screen),
                  "canvas": srgb(canvas), "canvas2": srgb(canvas2), "glass": srgb(glass), "word": srgb(word),
                  "accent": srgb(accent)}


PALETTES = [
    P("P0boot", "#FFFFFF", "#FF2BD6", "#22E6FF", "#FFFFFF", "#000000", "#FFFFFF", "#22E6FF", "#FF2BD6", "#3B5BFF",
      "#101030", "#FFFFFF", "#3B5BFF", fill="#000000"),
    P("P1compile", "#22E6FF", "#8A5CFF", "#FFC23D", "#FFFFFF", "#05030C", "#FFC23D", "#22E6FF", "#FF2BD6", "#8A5CFF",
      "#141040", "#FFFFFF", "#FF2BD6"),
    P("P2allyougot", "#7FF3FF", "#FF3FA4", "#FFB347", "#E8E8FF", "#04061A", "#FFB347", "#9FE8FF", "#FF3FA4", "#7F5BFF",
      "#1A2A6A", "#FFFFFF", "#FF3FA4", fill="#04050C"),
    P("P3night", "#9CC8FF", "#C8E2FF", "#7FB0FF", "#E8EEFF", "#020512", "#FFB15C", "#8FD0FF", "#3A5BC0", "#FFB15C",
      "#12264E", "#DDEBFF", "#FFB15C", fill="#03040A"),
    P("P4ember", "#FF3B3B", "#FF8A3D", "#FFE08A", "#FFE6D0", "#0E0204", "#FFE08A", "#FF6A3D", "#FF3B3B", "#FFE08A",
      "#3A0A0A", "#FFE08A", "#FFE08A", fill="#090203"),
    P("P5void", "#FFFFFF", "#FFFFFF", "#FF4FD8", "#FFFFFF", "#000000", "#FFFFFF", "#FFFFFF", "#FF4FD8", "#FF4FD8",
      "#000000", "#FFFFFF", "#FF4FD8", fill="#000000"),
    P("P6violet", "#B07CFF", "#D9C2FF", "#8A5CFF", "#F0E6FF", "#07020F", "#FF8FE6", "#C9A8FF", "#FF4FD8", "#8A5CFF",
      "#1E0E3A", "#FF4FD8", "#FF4FD8", fill="#06030B"),
    P("P7tension", "#22F0D0", "#FF2E9A", "#22F0D0", "#FFFFFF", "#010406", "#E8FF3A", "#22F0D0", "#FF2E9A", "#E8FF3A",
      "#05302A", "#FFFFFF", "#E8FF3A", fill="#020405"),
    P("P8growth", "#FFD27A", "#FFFFFF", "#FFB08A", "#FFF0D8", "#0A0830", "#FFD27A", "#FFE6B0", "#FFB08A", "#FFD27A",
      "#2A1C50", "#FFF2D0", "#FFB08A", fill="#060418"),
    P("P9jewels", "#BFB8D8", "#FF3355", "#3D7BFF", "#FFFFFF", "#05020A", "#FFB33D", "#5CF0FF", "#FF3355", "#3D7BFF",
      "#14082A", "#FFFFFF", "#FF7AB8", fill="#040209"),
    P("P10dance", "#FF7A2E", "#FF4F8B", "#FFC94A", "#FFE8D0", "#140406", "#FFC94A", "#2EE6D6", "#FF4F8B", "#2EE6D6",
      "#3A0E10", "#FFF0D8", "#2EE6D6", fill="#0C0304"),
    P("P11open", "#5CC8FF", "#FF5FA2", "#FFC86B", "#FFFFFF", "#030820", "#FFC86B", "#7FE7FF", "#FF5FA2", "#FFC86B",
      "#0A1A4A", "#FFFFFF", "#FFC86B", fill="#02040E"),
    P("P12summit", "#FFFFFF", "#FF5FD0", "#5CFFE0", "#FFFFFF", "#08041A", "#FFFFFF", "#FFFFFF", "#FF5FD0", "#5CFFE0",
      "#1A0A3A", "#FFFFFF", "#FFE35C", fill="#05030E"),
    P("P13dawn", "#FFE3C0", "#FFD0B0", "#FFB89A", "#FFF4E8", "#FF9E7A", "#FFE3B0", "#FFE3B0", "#FFB89A", "#FFE3B0",
      "#FFD0B0", "#FFFFFF", "#FFE3B0", fill="#2A1A20"),
]
PALETTE_INDEX = {name: i for i, (name, _) in enumerate(PALETTES)}

LINE_ROLE = {"wall": "wall", "furn": "furn", "furn2": "furn2", "figure": "figure"}
SURFACE_ROLE = {K.GLOW: "glow", K.SCREEN: "screen", K.CANVAS: "canvas", K.CANVAS2: "canvas2", K.GLASS: "glass"}


def add_world(film: Film, room: dict, edge_width=0.011, edge_intensity=4.0, max_distance=30.0):
    names = []
    for name, tree, role, bmin, bmax in room["objects"]:
        film.sdf(name, tree, edge_width=edge_width, edge_intensity=edge_intensity, bmin=bmin, bmax=bmax,
                 max_distance=max_distance)
        film.bind(LINE_ROLE[role], f"sdf/{name}/look/edge/color")
        film.bind("fill", f"sdf/{name}/surface/{K.FILL}/color")
        film.bind("fill", f"sdf/{name}/surface/{K.FLOOR}/color")
        film.bind("fill", f"sdf/{name}/surface/{K.ACCENT}/color")
        for k, r in SURFACE_ROLE.items():
            film.bind(r, f"sdf/{name}/surface/{k}/emission", "multiply")
        names.append(name)
    lights = []
    for lname, pos, kind in room.get("lights", []):
        color = [1.0, 0.72, 0.42] if kind == "lamp" else [0.5, 0.8, 1.0]
        film.point_light(lname, pos, color, 2.5 if kind == "lamp" else 1.2, 3.5 if kind == "lamp" else 2.2)
        film.bind("glow" if kind == "lamp" else "screen", f"lights/{lname}/color")
        lights.append(lname)
    return names, lights


def environment(fog_hex="#04061A", stem="all-you-got-pass2"):
    fog = srgb(fog_hex)
    return {"intensity": 0.0, "background": fog, "fogColor": fog, "lightRig": f"{stem}.rig.json",
            "volumeDensity": 0.0, "volumeAbsorption": 1.0, "volumeEmission": 0.0, "volumeScattering": 0.5,
            "volumeMaxDistance": 0.0, "volumeLocalLights": 0.0,
            "sky": {"enabled": False, "background": False, "useKeyLight": False, "zenithColor": fog,
                    "horizonColor": fog, "groundColor": fog, "sunColor": [1.0, 0.9, 0.8],
                    "sunDirection": [0.0, 0.2, -1.0], "sunIntensity": 0.0, "sunSize": 0.02, "sunGlow": 0.05,
                    "intensity": 0.0}}


PARAMS = {"camera/exposure/mode": 0, "camera/exposure/compensation": 0.0, "post/tonemap/operator": 1,
          "post/bloom/enabled": True, "post/bloom/intensity": 0.35, "post/bloom/threshold": 0.9,
          "post/grade/contrast": 1.08, "post/grade/saturation": 1.1, "post/output/vignette": 0.25,
          "post/output/grain": 0.0, "post/motionBlur/amount": 0.35}


def palettes(film: Film):
    for name, roles in PALETTES:
        film.palette(name, **roles)
    film.bind("fog", "scene/fogColor")


# =============================================================================================================
# KIT PREVIEW: every world, a few framings each, one second a shot (render as stills at fps 1).
# =============================================================================================================

def build_kit(palette_name="P2allyougot"):
    film = Film(end=12.0)
    palettes(film)
    liv = RM.living_room()
    names, lights = add_world(film, liv)
    a = liv["anchors"]
    views = [("impact", [[-0.6, 1.5, 1.95], [-0.6, 1.5, 1.9]], (-0.6, 0.95, -1.6)),
             ("couch", [[1.9, 1.55, 1.75], [1.85, 1.55, 1.7]], (-1.9, 0.75, -0.35)),
             ("tv", [[-1.3, 1.35, 1.8], [-1.25, 1.35, 1.75]], (2.2, 0.85, -0.3)),
             ("door", [[-0.3, 1.6, 1.9], [-0.25, 1.6, 1.85]], (1.5, 1.2, -2.2)),
             ("man", [[0.4, 1.25, 0.9], [0.35, 1.25, 0.85]], a["man"]),
             ("wide", [[2.3, 2.25, 1.95], [2.25, 2.25, 1.9]], (-1.0, 0.6, -1.0))]
    for i, (nm, eye, look) in enumerate(views):
        film.shot(nm, i * 1.0, (i + 1) * 1.0, eye, look, nodes=names, lights=lights, fov=62)
    film.track("palette/position", [(0.0, float(PALETTE_INDEX[palette_name])), (12.0, float(PALETTE_INDEX[palette_name]))])
    path = film.write(OUT, "all-you-got-pass2-kit", GRID, environment(stem="all-you-got-pass2-kit"),
                      dict(PARAMS, **{"post/motionBlur/amount": 0.0}))
    print("\n".join(film.report))
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kit", action="store_true")
    ap.add_argument("--palette", default="P2allyougot")
    a = ap.parse_args()
    if a.kit:
        build_kit(a.palette)
    else:
        raise SystemExit("the film is not assembled yet; use --kit")


if __name__ == "__main__":
    main()
