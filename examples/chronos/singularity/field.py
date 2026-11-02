"""CHRONOS, Temporal Singularity: the field, in Python. A line-for-line mirror of field.wgsl's field functions (not its
noise), so the figures are integrated through exactly the masses the layers draw and lens with. Change both together.
"""
import math

TEAR_TIMES = [13.0, 31.5, 52.0, 71.0, 92.5, 110.0, 131.0, 149.5, 170.0, 188.0, 206.0, 226.0]
BURST_TIMES = [5.5, 21.0, 24.5, 41.5, 46.0, 61.0, 82.0, 86.5, 101.0, 121.5, 139.0, 158.5, 162.0, 178.5, 197.0, 216.5]


def smoothstep(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def rot(v, a):
    c, s = math.cos(a), math.sin(a)
    return (c * v[0] - s * v[1], s * v[0] + c * v[1])


def tear(t):
    return max(math.exp(-((t - tk) / 2.4) ** 2) for tk in TEAR_TIMES)


def tear_integral(t):
    return sum(2.13 * (1.0 + math.tanh(0.6 * (t - tk))) for tk in TEAR_TIMES)


def main_center(t):
    return (0.04 + 0.27 * math.sin(0.047 * t + 1.0) + 0.06 * math.sin(0.131 * t + 2.3),
            0.01 + 0.12 * math.sin(0.061 * t + 0.4) + 0.04 * math.sin(0.157 * t + 4.1))


def core_radius_at(t, base=0.085, expansion=0.0):
    breathe = 1.0 + 0.10 * math.sin(0.21 * t + 0.3) + 0.06 * math.sin(0.57 * t + 1.7) + 0.04 * math.sin(1.13 * t + 0.2)
    slow = 1.0 + 0.28 * math.sin(0.037 * t + 2.0) + 0.1 * math.sin(0.089 * t + 0.5)
    return base * breathe * slow * (1.0 + 0.22 * tear(t) + 0.35 * expansion)


def eddy(i, t, c0, R0):
    if i == 0:
        ph = 0.9 + 0.17 * t
        a = 0.42 * (1.0 + 0.25 * math.cos(0.31 * t))
        c = (c0[0] + a * math.cos(ph), c0[1] + a * 0.72 * math.sin(ph))
        return c, R0 * 0.32, -1.0, smoothstep(0.1, 0.8, 0.5 + 0.5 * math.sin(0.083 * t + 0.6))
    if i == 1:
        ph = 4.0 - 0.11 * t
        a = 0.66 * (1.0 + 0.2 * math.sin(0.23 * t))
        c = (c0[0] + a * math.cos(ph), c0[1] + a * 0.62 * math.sin(ph))
        return c, R0 * 0.24, 1.0, smoothstep(0.2, 0.9, 0.5 + 0.5 * math.sin(0.061 * t + 2.9))
    ph = 2.2 + 0.05 * t
    a = 0.95 + 0.2 * math.sin(0.07 * t)
    c = (c0[0] + a * math.cos(ph), c0[1] + a * 0.5 * math.sin(ph))
    return c, R0 * 0.38, -1.0, smoothstep(0.55, 0.95, 0.5 + 0.5 * math.sin(0.044 * t + 4.4))


def frame_zoom(t, scale=1.0):
    push = (0.14 * math.exp(-((t - 40.0) / 9.0) ** 2) + 0.18 * math.exp(-((t - 120.0) / 11.0) ** 2) +
            0.12 * math.exp(-((t - 185.0) / 9.0) ** 2))
    return (1.0 + 0.05 * math.sin(0.043 * t) + push) * scale


def frame_turn(t):
    return 0.011 * t + 0.07 * math.sin(0.031 * t)


def field_to_screen(q, t):
    """Field coordinates -> screen coordinates (frame heights, origin at the centre, y up)."""
    z = frame_zoom(t)
    return rot((q[0] * z, q[1] * z), -frame_turn(t))


def screen_to_field(p, t):
    z = frame_zoom(t)
    r = rot(p, frame_turn(t))
    return (r[0] / z, r[1] / z)
