"""Replicates src/core/wind.cpp motionResponse + vegetationBend magnitudes for GV3's layers.

Tip travel (metres) = bend * profile(1 at tip) * height, bend 'along' = s*(steady + gust*g), flutter = s*flutter*sin(...)
s = speed * region (region in [1-regionAmount, 1+regionAmount]); g = gustAmount * envelope(0..1).
"""
import json
import math

TAU = 2 * math.pi


def osc_gain(w, w0, z):
    w0 = max(w0, 1e-4)
    r = w / w0
    z = min(max(z, 0.02), 4.0)
    re = 1 - r * r
    im = 2 * z * r
    return 1 / max(math.hypot(re, im), 1e-4)


def response(wind, m):
    k = max(m.get("stiffness", 1.0), 1e-3)
    mass = max(m.get("mass", 0.02), 1e-6)
    z = min(max(m.get("damping", 0.3), 0.02), 4.0)
    w0 = math.sqrt(k / mass)
    statics = m.get("tipAmplitude", 0.25) * max(m.get("windSensitivity", 0.0), 0) / k
    wg = TAU * wind["gustSpeed"] / max(wind["gustScale"], 0.01)
    wr = TAU * wind["regionDrift"]
    steady = statics * osc_gain(wr, w0, z)
    gust = statics * osc_gain(wg, w0, z) * max(m.get("gustResponse", 1.0), 0)
    flutter = statics * min(0.5 / z, 4.0) * wind["turbulence"]
    return steady, gust, flutter, w0, wg


def report(wind, layers, label):
    s = wind["speed"]
    print(f"\n== {label}: speed {s}, gust {wind['gustAmount']} x ({wind['gustScale']} m @ {wind['gustSpeed']} m/s)"
          f" -> gust period {wind['gustScale'] / max(wind['gustSpeed'], 1e-6):.1f} s, turbulence {wind['turbulence']}")
    print(f"{'layer':12s} {'h(m)':>5s} {'lean cm':>8s} {'gust swing cm':>13s} {'flutter cm':>10s} {'flutter Hz':>10s}")
    for name, h, m in layers:
        st, gu, fl, w0, wg = response(wind, m)
        lean = s * st * h * 100
        swing = s * gu * wind["gustAmount"] * h * 100      # full gust envelope 0 -> 1 at mean region
        flut = s * fl * h * 100                             # amplitude of the sine
        print(f"{name:12s} {h:5.2f} {lean:8.1f} {swing:13.1f} {flut:10.2f} {w0 / TAU:10.2f}")


scene = json.load(open("/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/examples/world/glowmere-valley-3.scene.json"))
wind = scene["wind"]
valley = [n for n in scene["nodes"] if n["name"] == "valley"][0]
layers = [(l["name"], l["height"], l["motion"]) for l in valley["scatter"] if l.get("motion")]
report(wind, layers, "GV3 as authored")

retune = dict(wind, gustSpeed=8.0, gustScale=26.0, gustAmount=0.9, turbulence=0.45)
report(retune, layers, "candidate retune (field only; species unchanged)")
