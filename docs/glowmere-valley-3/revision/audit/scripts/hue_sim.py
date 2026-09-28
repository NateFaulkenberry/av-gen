"""Quantify on-screen fungi emission hue vs the hue hueField intends.

Model (inferred from code):
  instance.emissive m = hueRotationMultiplierOklab(layer.emissiveColor, turns) * emissiveMul   (procedural.cpp:2489-2503)
  program emission    = m * C * mask * ramp * emissionIntensity                               (glowmere-tissue.material.json)
  fs_proc emissiveMul = m                                                                      (procedural.wgsl:519)
  shadeSurface        = program.emission * emissiveMul  -> m^2 * C * ...                       (pbr_shade.wgsl:773)
"""
import math


def lin_to_oklab(c):
    r, g, b = c
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l_, m_, s_ = (max(x, 0.0) ** (1 / 3) for x in (l, m, s))
    return (0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
            1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
            0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_)


def oklab_to_lin(lab):
    L, a, b = lab
    l_ = L + 0.3963377774 * a + 0.2158037573 * b
    m_ = L - 0.1055613458 * a - 0.0638541728 * b
    s_ = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    return (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)


def hue_shift(c, turns):
    L, a, b = lin_to_oklab(c)
    C = math.hypot(a, b)
    h = math.atan2(b, a) + turns * 2 * math.pi
    return oklab_to_lin((L, C * math.cos(h), C * math.sin(h)))


def hue_deg(c):
    L, a, b = lin_to_oklab(c)
    return (math.degrees(math.atan2(b, a)) + 360) % 360, math.hypot(a, b)


def mult(base, turns):
    safe = [max(x, 1e-3) for x in base]
    rot = [max(x, 0.0) for x in hue_shift(safe, turns)]
    return [min(max(r / s, 0.0), 96.0) for r, s in zip(rot, safe)]


P = (0.341, 0.0821, 1.0)      # fungi layer emissiveColor
C = (0.08, 0.95, 0.53)        # glowmereTissue emission constant (op 10)

print(f"layer emissiveColor hue {hue_deg(P)[0]:.0f} deg; program constant hue {hue_deg(C)[0]:.0f} deg")
print("turns | intended hue (rot of P) | on-screen hue m^2*C (chroma) | m")
for t in (-0.16, -0.08, -0.035, 0.0, 0.035, 0.08, 0.16):
    m = mult(P, t)
    intended = hue_shift(P, t)
    shown = [mm * mm * cc for mm, cc in zip(m, C)]
    once = [mm * cc for mm, cc in zip(m, C)]
    hi, _ = hue_deg(intended)
    hs, cs = hue_deg(shown)
    ho, _ = hue_deg(once)
    print(f"{t:+.3f} | {hi:6.0f} | {hs:6.0f} ({cs:.3f})  [m^1 would be {ho:4.0f}] | {[round(x, 2) for x in m]}")
