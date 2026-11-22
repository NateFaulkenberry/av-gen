#!/usr/bin/env python3
"""Neon Dreams it14 (owner msgs 195, 200): two flyers land IN the band's crowd and stay to watch; the vortex's inside
for the film's last shot ("finish the scene off by diving into this vortex and fading out").

    /usr/bin/python3 examples/neon-dreams/blender/nd_it14.py [--only flights|vortex]
    /usr/bin/python3 examples/neon-dreams/blender/nd_variety.py        # after it, as always: run nd_variety LAST

Plain Python, deterministic, idempotent: it rewrites its own records in art/population.json (`flyers` with "it": 14)
and art/cloud_design.json (everything tagged it14 / `vortex_dive`), and nothing else. ND_ART names a scratch art folder.

FLIGHTS (msg 200: "an alien or two flying in and landing in the crowd to watch along"). The band already had five
flights inside twelve seconds, and two of them were wasted on the lens: fly_band_in_south came down below the frame's
left edge and fly_quay_band at the crowd's far west edge. Both now come down in the crowd's front-left block, which
the lens holds from 43.6 to 49.5 s (work/art/it14: projected through the baked camera):
  fly_quay_band      leads the lens down the Quay as before, down at (143.0, 28.5) on the 44.22 s kick, 17 m ahead
  fly_band_in_south  crosses in front of the lens from the right, down at (146.6, 28.6) ON the 47.03 s crash
Each record carries `landing.gap_m` (nd_variety.py makes the audience step back that far; their old places are kept
in pos0) and `after`: clip cues in nd_cues' shape for what it does once it is down (sways, cheers with the crash,
claps), which needs the flyer built from its `-nd` cast file (the engine's side; reference diff in the studio
project, work/art/it14/nd_cast_it14.diff).

VORTEX: see vortex() below.
"""
from __future__ import annotations

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_argv, sys.argv = sys.argv, sys.argv[:1]
import nd_band as NB  # noqa: E402  (the it12 flight record's shape, the stage's frame, the kicks)
sys.argv = _argv
ART = os.environ.get("ND_ART", os.path.join(HERE, "..", "art"))
KICK = 2 * 60.0 / 171.4      # the art clips' kick (nd_poses.py)
STAGE = NB.C


def R3(v):
    return [round(float(x), 3) for x in v]


# ------------------------------------------------------------------------------------------------ the flights
def after_cues(td, plan):
    """[(clip, kicks it lasts or None, blend)] -> cues from the end of Landing on; the clip second is counted from the
    touchdown's kick, so a clip that carries the beat keeps it."""
    out = []
    t = td + 0.80                      # Landing (1.07 s, feet down 0.27 s in) has played out
    for clip, kicks, blend in plan:
        out.append({"t": round(t, 3), "clip": clip, "rate": 1.0, "offset": round((t - td) % (4 * KICK), 4), "blend": blend,
                    "stock": "Idle"})
        if kicks is None:
            break
        t += kicks * KICK
    return out


def flights(pop):
    crash = min((t for t, _ in json.load(open(os.path.join(HERE, "..", "audio", "sabertooth_events.json")))["stems"]["crash"]["onsets"]),
                key=lambda c: abs(c - 47.03))
    by = {f["id"]: f for f in pop["flyers"]}
    out = {}
    # 1. the Quay's flyer: the lens follows it from the close pass; down in the crowd 17 m ahead of the lens
    A = [30.4, 134.51, 14.76]
    B = [143.0, STAGE[1], 28.5]
    rt = [[40.0, 146.0, 14.0], [62.0, 146.0, 15.0], [90.0, 146.0, 20.0], [B[0] - 14.0, 146.0, B[2] - 3.0]]
    td = NB.snap(44.22)
    f = NB.flight("fly_quay_band", "alien-ranger", A, B, 34.6, td, 147.0, rt,
                  "it14 (msg 200): off a Quay roof garden ahead of the lens down the Quay and over the Steps, and down IN "
                  "the band's crowd, front-left block, 17 m ahead of the lens on the 44.22 s kick; it stays and watches",
                  da="roof", db="band")
    # sways with the crowd, throws its arms up when the crash's cheer reaches it, then claps to the end
    hush = crash + 0.08 + 0.45
    f["after"] = after_cues(td, [("ND_Sway", None, 0.35)]) + [
        {"t": round(hush, 3), "clip": "ND_Arms_up", "rate": 1.0, "offset": 0.0, "blend": 0.28, "stock": "Idle"},
        {"t": round(hush + 8 * KICK, 3), "clip": "ND_Clap_b", "rate": 1.0, "offset": round((hush + 8 * KICK - td) % (4 * KICK), 4),
         "blend": 0.45, "stock": "Idle"}]
    f["landing"]["gap_m"] = 1.25
    out[f["id"]] = f
    # 2. the south isle's flyer: across in front of the lens from the right, round the stage's front (never nearer
    #    than 9.4 m to its centre: the halo and its masts are 6.75 m out), down in the crowd ON the crash
    A = [96.5, 122.623, 98.0]
    B = [146.6, STAGE[1], 28.6]
    top = 130.5      # 4 m over the lens: at 133 it crossed the frame's top edge, half out of the picture
    rt = [[105.5, top, 84.0], [118.0, top, 62.0], [130.0, top, 42.5], [139.0, top, 31.8]]
    td = NB.snap(crash)
    f = NB.flight("fly_band_in_south", "alien-pilot", A, B, 37.2, td, top, rt,
                  "it14 (msg 200): from the hanging-garden isle across in front of the lens (right to left) and down IN the "
                  "band's crowd, front-left block, touching down on the 47.03 s crash; it cheers with the crowd and stays",
                  bob_a="isle_south2", da="isle:isle_south2", db="band")
    f["after"] = after_cues(td, [("ND_Cheer", 8, 0.28), ("ND_Clap", None, 0.45)])
    f["landing"]["gap_m"] = 1.25
    out[f["id"]] = f
    for f in out.values():
        yaw = round(NB.yaw_to(f["landing"]["pos"], [STAGE[0], 0.0, STAGE[2]]), 1)   # down facing the band
        for k in f["keys"]:
            if k["clip"] in ("Floating", "Landing", "Idle") and k["t"] > f["t_takeoff"] + 3.0:
                k["yaw_deg"] = yaw
        f["it"], f["msg"] = 14, 200
        f["was"] = {"landing": by[f["id"]]["landing"]["pos"] if by[f["id"]].get("it") != 14 else by[f["id"]]["was"]["landing"],
                    "t_land": by[f["id"]]["t_land"] if by[f["id"]].get("it") != 14 else by[f["id"]]["was"]["t_land"]}
    pop["flyers"] = [out.get(f["id"], f) for f in pop["flyers"]]
    edits = [e for e in pop.get("edits", []) if e.get("script") != "blender/nd_it14.py"]
    edits.append({"it": 14, "msg": 200, "by": "art", "script": "blender/nd_it14.py",
                  "what": "fly_quay_band and fly_band_in_south land in the band's crowd in the lens's view (landing.gap_m, "
                          "`after` cues); nd_variety.py opens the gaps",
                  "landings": {k: {"pos": v["landing"]["pos"], "t": v["on_beat"]["touchdown_kick_s"], "was": v["was"]}
                               for k, v in out.items()}})
    pop["edits"] = edits
    for f in out.values():
        print("flight %-18s lift %.3f touchdown %.3f at %s (was %s at %.2f); after: %s" % (
            f["id"], f["on_beat"]["liftoff_kick_s"], f["on_beat"]["touchdown_kick_s"], f["landing"]["pos"], f["was"]["landing"],
            f["was"]["t_land"], [(c["t"], c["clip"]) for c in f["after"]]))


# ------------------------------------------------------------------------------------------------ the vortex
VX = {
    # ADR-2120's column: a sheath of mist that spirals up out of the eye, banded by the arms (it was never switched on)
    "column": 50.0, "columnDensity": 0.45, "columnPitch": 60.0,
    "depth": 84.0, "ocean_y": -110.0,          # the funnel's depth and the cosmic ocean's disc (it12's values)
    # the bank that lies on the ocean's disc: [metres under the disc, metres over it, density, scale, coverage]
    "floor": [4.0, 26.0, 0.55, 28.0, 0.5],
    # the tatters over the bowl (the engine's `vortex_shelf` sheet): [radius, base, top, density, scale, coverage]
    "shelf": [760.0, -30.0, 40.0, 0.7, 55.0, 0.45],
}
VX.update(json.loads(os.environ.get("ND_VX", "{}")))   # ND_VX: a JSON of knobs, for A/B stills


def vortex(des):
    """The vortex's inside for the dive (msg 195: "finish the scene off by diving into this vortex and fading out").

    The engine's dive (layout.camera, it14) comes over the funnel's west rim at 225 m/s and looks down into it at -22
    to -44 degrees from 68.5 to 69.6 s, fading from 68.4 s; the lens stays above the sea's top while the picture is
    bright. So what the dive shows is the funnel from above, close: ADR-2120's terraces and arms already carry the
    walls. What it lacked, and what this sets (all in the vortex's own frame; the centre and the axes are not touched):
      mist_layers  `vortex_floor`: a torn bank lying on the ocean's disc. The disc is a flat plate with 250 m features
                   that cuts the funnel's lowest terrace along a line; now the light comes up through tattered cloud
                   and the plate's edge is never seen.
                   `vortex_shelf`: the engine's bank round the vortex, re-centred on it (it stayed 400 m east when
                   the vortex moved in it12b, so the west half of the bowl, where the dive comes in, had none) and
                   thinned: tatters over the bowl that the funnel's swirl winds inward.
      vortex       column / columnDensity / columnPitch: the sheath of mist that spirals up out of the eye. It softens
                   the throat's lip and gives the eye a second, turning layer over the floor.
    Tried and dropped (stills in the studio project, work/art/it14): wisp zones over the bowl (the wisp layer's 10 m
    grain is a milky film at this distance and hides the terraces); a deeper funnel.
    """
    brk = des["sea_design"]["break"]
    v = brk["vortex"]
    cx, cz = v["centre"]
    throat = v["throat_semi_axes"]
    v["depth_m"] = VX["depth"]
    v["floor_y"] = round(v["top_y"] - VX["depth"], 1)
    brk["ocean_plane"]["y"] = VX["ocean_y"]
    des["vortex"] = {"column": VX["column"], "columnDensity": VX["columnDensity"], "columnPitch": VX["columnPitch"],
                     "_note": "it14 (msg 195): ADR-2120 keys the art block (sea_design.break.vortex) does not set; "
                              "build.py's vortex_design() takes them over its defaults"}
    layers = [dict(l) for l in des.get("mist_layers", []) if l["name"] != "vortex_floor"]
    for l in layers:
        if l["name"] == "vortex_shelf" and VX["shelf"]:
            r, base, top, dens, scale, cov = VX["shelf"]
            l.update({"centre": [cx, cz], "radius": r, "base": base, "top": top, "density": dens, "scale": scale, "coverage": cov})
    if VX["floor"]:
        under, over, dens, scale, cov = VX["floor"]
        oy = VX["ocean_y"]
        layers.append({"name": "vortex_floor", "centre": [cx, cz], "radius": round(1.25 * throat[0], 1), "base": oy - under,
                       "top": oy + over, "density": dens, "scale": scale, "coverage": cov, "stretch": 2.4,
                       "drift": [0.6, 0.0, 0.3]})
    assert len(layers) <= 6, "the wisp layer holds 6 sheets (kMaxCloudWispLayers)"
    des["mist_layers"] = layers
    des["wisp_regions"] = [z for z in des["wisp_regions"] if not z["name"].startswith("dive_")]
    des["vortex_dive"] = {"msg": 195, "script": "blender/nd_it14.py",
                          "note": "shaped for the engine's it14 dive (layout.camera: over the west rim at ~69.0 s, down toward "
                                  "the throat's west side, the picture fading 68.4 -> 70.18 s); nothing here depends on the "
                                  "path's exact line, only on the vortex's own frame",
                          "knobs": {k: VX[k] for k in sorted(VX)}}
    print("vortex: column %.0f m x %.2f, pitch %.0f; depth %.0f, ocean y %.0f; floor %s; shelf %s; %d mist layers" % (
        VX["column"], VX["columnDensity"], VX["columnPitch"], VX["depth"], VX["ocean_y"], VX["floor"], VX["shelf"], len(layers)))


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    if only in (None, "flights"):
        p = os.path.join(ART, "population.json")
        pop = json.load(open(p))
        flights(pop)
        json.dump(pop, open(p, "w"), indent=None, separators=(",", ":"))
    if only in (None, "vortex"):
        p = os.path.join(ART, "cloud_design.json")
        des = json.load(open(p))
        vortex(des)
        json.dump(des, open(p, "w"), indent=None, separators=(",", ":"))


if __name__ == "__main__":
    main()
