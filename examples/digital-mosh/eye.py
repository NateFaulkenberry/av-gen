"""DIGITAL MOSH, pass 5b: the floating eye (kept from pass 6 at the owner's request).

A colossal eye hovers over the riverbed. Its gaze is the node's rotation: the iris looks along the node's local +Z,
so a stage turns the eye by turning the node (presets blend the rotation, so it turns slowly). Its identity is the
material program's to change, along the pass-6 chain eye -> moon -> hole, by two constants a stage sets:

  moon  0..1  the iris closes and the white turns to a cratered moon (the Corruption)
  hole  0..1  the moon turns to a hole: ink, with the strain's ring burning round it (the Nightmare)

and then the Collapse takes it apart into its own blocks (the rot field eats it, as it eats the tree).
"""
from __future__ import annotations

from forms import sdf


def lin(hexs):
    def c(v):
        v = v / 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return [round(c(int(hexs[i:i + 2], 16)), 4) for i in (1, 3, 5)]


R = 13.0                     # the ball's radius (m)
CREAM = lin("#ece3d2")
ROSE = lin("#c99a92")
IRIS = [lin("#2f4f53"), lin("#5f9aa0"), lin("#c1ae5e")]   # the Bee's turquoise warming to ochre at the pupil
LIMBUS = lin("#1f2a2c")
INK = lin("#2d0c1f")
PUPIL = lin("#0c0a0e")
MOON = lin("#c9cdd0")


def tree():
    """The ball and the cornea's bulge (a smaller sphere set into it along +Z), eaten by the rot like everything."""
    ball = sdf("sphere", radius=R)
    cornea = sdf("translate", [sdf("sphere", radius=R * 0.5)], translation=[0.0, 0.0, R * 0.62])
    eye = sdf("smoothUnion", [ball, cornea], smooth=R * 0.06)
    return {"root": sdf("displaceField", [eye], amount=0.0, reference="rot", name="eaten")}


def bounds():
    return [-R - 1.0, -R - 1.0, -R - 1.0], [R + 1.0, R + 1.0, R * 1.2 + 1.0]


def op(kind, dst, **kw):
    o = {"kind": kind, "dst": dst}
    o.update(kw)
    return o


def program(strain):
    """Wax-cream sclera with faint dusty-rose veins, a fibrous iris ringed by a dark limbus, an ink pupil, wet; then
    moon and hole as the stage says (constants 'moon' and 'hole', the 2nd and 3rd `constant` ops)."""
    return {"name": "eye", "ops": [
        op("input", 0, input="localPosition"),
        op("gradient", 1, srcA=0, value=1.0 / R, constant=[0.0, 0.0, 1.0, 0.0]),   # 1 at the cornea's centre
        op("smoothstep", 2, srcA=1, constant=[0.80, 0.815, 0, 0]),                  # the iris disc
        op("smoothstep", 3, srcA=1, constant=[0.985, 1.0, 0, 0]),                   # the pupil
        op("remap", 4, srcA=1, value=1, constant=[0.81, 1.0, 0.0, 1.0]),
        op("ramp", 4, srcA=4, constant=IRIS[0] + [1], constant2=IRIS[1] + [1], constant3=IRIS[2] + [1]),
        op("noise", 5, srcA=0, value=1.4, seed=21),                                 # the iris's fibres
        op("remap", 5, srcA=5, value=1, constant=[0.3, 0.7, 0.6, 1.3]),
        op("multiply", 4, srcA=4, srcB=5),
        op("voronoiEdge", 5, srcA=0, value=0.32, seed=5),                           # veins, faint
        op("smoothstep", 5, srcA=5, constant=[0.0, 0.02, 0, 0]),
        op("remap", 5, srcA=5, value=1, constant=[0.0, 1.0, 0.6, 1.0]),
        op("constant", 6, constant=ROSE + [1]),
        op("constant", 7, constant=CREAM + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        op("mixBy", 6, srcA=6, srcB=4, srcC=2),                                     # sclera -> iris
        op("smoothstep", 5, srcA=1, constant=[0.79, 0.81, 0, 0]),                   # the limbal ring
        op("remap", 7, srcA=2, value=1, constant=[0.0, 1.0, 1.0, 0.0]),
        op("multiply", 5, srcA=5, srcB=7),
        op("constant", 7, constant=LIMBUS + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        op("constant", 7, constant=PUPIL + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=3),                                     # the pupil
        # the moon: craters on a cold white, the iris gone
        op("constant", 5, constant=[0.0, 0.0, 0.0, 0.0]),                           # MOON amount (stage)
        op("voronoi", 7, srcA=0, value=0.22, seed=13),
        op("remap", 7, srcA=7, value=1, constant=[0.1, 0.9, 0.5, 0.95]),
        op("multiply", 7, srcA=7, srcB=7),
        op("constant", 4, constant=MOON + [1]),
        op("multiply", 7, srcA=7, srcB=4),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        # the hole: ink, and the strain burning in a ring round the pupil's place
        op("constant", 5, constant=[0.0, 0.0, 0.0, 0.0]),                           # HOLE amount (stage)
        op("constant", 7, constant=INK + [1]),
        op("mixBy", 6, srcA=6, srcB=7, srcC=5),
        op("smoothstep", 7, srcA=1, constant=[0.55, 0.8, 0, 0]),
        op("smoothstep", 4, srcA=1, constant=[1.0, 0.85, 0, 0]),
        op("multiply", 7, srcA=7, srcB=4),
        op("multiply", 7, srcA=7, srcB=5),
        op("constant", 4, constant=strain + [1]),
        op("multiply", 7, srcA=7, srcB=4),
        # wet: the iris glassier than the white
        op("remap", 1, srcA=2, value=1, constant=[0.0, 1.0, 0.16, 0.05]),
    ], "baseColor": 6, "metallic": -1, "roughness": 1, "emission": 7, "emissionIntensity": 0.0, "opacity": -1}


def program_constant_index(prog, nth):
    """1-based op index of the nth `constant` op (the stage's MOON amount is nth=5, HOLE nth=7)."""
    seen = 0
    for k, o in enumerate(prog["ops"], start=1):
        if o["kind"] == "constant":
            seen += 1
            if seen == nth:
                return k
    raise KeyError(nth)
