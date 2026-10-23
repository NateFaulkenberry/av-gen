"""The Living Prism: the six palette looks (spec 6.1), as palette roles in OKLCH, converted to linear RGB.

A look is not "everything one hue". Each has a hierarchy (05-stage1-plan.md section 4):
  - one DOMINANT crystal family (the primary formation and most of the crystal area),
  - one SECONDARY family (the other heroes),
  - one small ACCENT (the ember block, a few seams; usually warm against cool, or cool against warm),
  - near-black SHADOW rock that carries a trace of the look's hue, and the haze, water and light colours.
Lightness and chroma are set per role, so the hierarchy survives any look: shadows L < 0.2, bodies L 0.45-0.65,
tips and seams L > 0.8 at lower chroma (bright things desaturate, as light does).

Roles (each binds to material-program constants, light colours and water tints in Stage 3):
  shadow   rock body          rockLit  rock catching light   seam     luminous mineral seams in the rock
  primary  the spire, the arch's crust                        secondary the fan, chandelier, druses
  tertiary the geode's lining                                 accent   the ember block, sparse seam accents
  core     internal glow of the crystals                      tip      terminations and facet highlights
  water    the pool's reflection tint                         haze     fog and volumetric colour
  key      the shaft light                                    fill     the low glow lights
  deep     the back passage's light: the depth cue, a hue apart from the dominant
"""
import math

ROLES = ["shadow", "rockLit", "seam", "primary", "secondary", "tertiary", "accent", "core", "tip", "water", "haze",
         "key", "fill", "deep"]

# (L, C, h degrees) per role. h is OKLCH hue: ~0 pink, 30 coral, 70 amber, 90 gold, 140 green, 200 cyan,
# 250 blue, 265 cobalt, 290 violet, 320 purple/fuchsia, 350 magenta.
LOOKS = [
    ("Ultraviolet", {
        "shadow": (0.13, 0.03, 290), "rockLit": (0.30, 0.05, 295), "seam": (0.78, 0.17, 330),
        "primary": (0.52, 0.22, 300), "secondary": (0.55, 0.24, 335), "tertiary": (0.62, 0.25, 355),
        "accent": (0.75, 0.15, 55), "core": (0.70, 0.22, 320), "tip": (0.92, 0.07, 340),
        "water": (0.30, 0.10, 295), "haze": (0.22, 0.07, 300), "key": (0.82, 0.08, 300), "fill": (0.65, 0.20, 330), "deep": (0.62, 0.16, 268)}),
    ("Glacier", {
        "shadow": (0.13, 0.03, 260), "rockLit": (0.32, 0.04, 255), "seam": (0.85, 0.10, 230),
        "primary": (0.50, 0.18, 262), "secondary": (0.68, 0.16, 240), "tertiary": (0.80, 0.07, 290),
        "accent": (0.85, 0.06, 285), "core": (0.75, 0.14, 245), "tip": (0.95, 0.03, 240),
        "water": (0.32, 0.08, 250), "haze": (0.25, 0.06, 255), "key": (0.90, 0.04, 240), "fill": (0.70, 0.13, 250), "deep": (0.72, 0.12, 222)}),
    ("Solar Mineral", {
        "shadow": (0.12, 0.03, 340), "rockLit": (0.30, 0.05, 20), "seam": (0.82, 0.16, 75),
        "primary": (0.55, 0.22, 350), "secondary": (0.66, 0.17, 35), "tertiary": (0.78, 0.16, 70),
        "accent": (0.86, 0.15, 90), "core": (0.75, 0.18, 55), "tip": (0.95, 0.05, 80),
        "water": (0.28, 0.08, 20), "haze": (0.24, 0.07, 30), "key": (0.88, 0.07, 70), "fill": (0.68, 0.18, 30), "deep": (0.58, 0.17, 335)}),
    ("Opaline", {
        "shadow": (0.15, 0.025, 300), "rockLit": (0.36, 0.03, 300), "seam": (0.90, 0.06, 0),
        "primary": (0.76, 0.09, 5), "secondary": (0.72, 0.10, 275), "tertiary": (0.88, 0.03, 300),
        "accent": (0.65, 0.17, 305), "core": (0.85, 0.08, 330), "tip": (0.97, 0.02, 300),
        "water": (0.36, 0.06, 290), "haze": (0.30, 0.05, 310), "key": (0.92, 0.03, 330), "fill": (0.78, 0.09, 290), "deep": (0.76, 0.08, 240)}),
    ("Abyssal", {
        "shadow": (0.10, 0.03, 255), "rockLit": (0.24, 0.05, 260), "seam": (0.75, 0.12, 205),
        "primary": (0.38, 0.17, 262), "secondary": (0.42, 0.18, 295), "tertiary": (0.62, 0.11, 205),
        "accent": (0.78, 0.13, 210), "core": (0.62, 0.17, 255), "tip": (0.88, 0.08, 230),
        "water": (0.22, 0.07, 250), "haze": (0.16, 0.06, 258), "key": (0.72, 0.10, 245), "fill": (0.55, 0.16, 260), "deep": (0.58, 0.14, 212)}),
    ("Wild Spectrum", {  # one vivid family per region: the spire violet, the fan cyan, the geode magenta, ember gold
        "shadow": (0.12, 0.03, 285), "rockLit": (0.30, 0.04, 285), "seam": (0.80, 0.16, 150),
        "primary": (0.55, 0.24, 300), "secondary": (0.72, 0.15, 205), "tertiary": (0.60, 0.25, 350),
        "accent": (0.84, 0.16, 85), "core": (0.74, 0.20, 320), "tip": (0.95, 0.05, 200),
        "water": (0.28, 0.08, 270), "haze": (0.22, 0.06, 290), "key": (0.86, 0.07, 280), "fill": (0.66, 0.20, 340), "deep": (0.62, 0.17, 250)}),
]


def oklch_to_linear(L, C, h):
    a, b = C * math.cos(math.radians(h)), C * math.sin(math.radians(h))
    l_ = L + 0.3963377774 * a + 0.2158037573 * b
    m_ = L - 0.1055613458 * a - 0.0638541728 * b
    s_ = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
    return [round(max(0.0, c), 4) for c in rgb]  # out-of-gamut channels clip at 0 (a vivid colour's honest limit)


def linear_to_srgb8(c):
    return tuple(int(round(255 * min(1.0, (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))))
                 for v in c)


def looks_linear():
    return [(name, {r: oklch_to_linear(*lch) for r, lch in roles.items()}) for name, roles in LOOKS]
