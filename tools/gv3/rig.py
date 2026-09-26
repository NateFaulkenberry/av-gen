"""Camera rigs and shots for Glowmere Valley 3, as data the generator turns into the engine's own
camera collection (`cameraDirection`) and timeline keys.

A shot here is one authored rig that is live, locked, for one span of the cut. Everything a rig can
do is the engine's (docs/development notes in tools/make_glowmere_valley_3.py): a fixed eye and
target, keyed moves on the ordinary timeline, a live aim at a node, a follow with an offset that
can itself be keyed (an orbit around something that moves), or a ride along a scene spline such as
the river's own centreline. Nothing here is evaluated at render time; it is all written out as
`cameras/<slug>/*` keys and a locked shot, so the render reads exactly what this file says.
"""

import math


def vec(v):
    # Millimetres: finer than any framing decision, and the file stays readable.
    return [round(float(v[0]), 4), round(float(v[1]), 4), round(float(v[2]), 4)]


def focal_to_vfov(focal_mm, sensor_height_mm=24.0):
    """Vertical field of view in degrees for a focal length on the project's 36x24 sensor."""
    return math.degrees(2.0 * math.atan(sensor_height_mm / (2.0 * focal_mm)))


class Key:
    __slots__ = ("time", "value", "interp")

    def __init__(self, time, value, interp="linear"):
        self.time = float(time)
        self.value = value
        self.interp = interp


class Rig:
    """One camera. Channels are keyed in absolute film seconds.

    kind:
      'free'   -- position/target (each keyable); optional aim at a node instead of a target;
                  optional follow of a node with a (keyable) offset
      'spline' -- rides a scene spline: splineT keyed, lookAhead, splineOffset
    """

    def __init__(self, name, focal=35.0, position=None, target=None, aim=None, aim_offset=(0.0, 0.0, 0.0),
                 follow=None, follow_offset=(0.0, 0.0, 0.0), follow_local=False, lag=0.0, clearance=0.0,
                 spline=None, spline_t=0.0, look_ahead=2.0, spline_offset=(0.0, 0.0, 0.0)):
        self.name = name
        self.focal = float(focal)
        self.position = vec(position) if position is not None else [0.0, 10.0, 0.0]
        self.target = vec(target) if target is not None else [0.0, 0.0, 0.0]
        self.aim = aim
        self.aim_offset = vec(aim_offset)
        self.follow = follow
        self.follow_offset = vec(follow_offset)
        self.follow_local = follow_local
        self.lag = float(lag)
        self.clearance = float(clearance)
        self.spline = spline
        self.spline_t = float(spline_t)
        self.look_ahead = float(look_ahead)
        self.spline_offset = vec(spline_offset)
        self.keys = {}  # channel -> [Key]

    def key(self, channel, time, value, interp="linear"):
        if isinstance(value, (int, float)):
            value = [float(value)]
        else:
            value = vec(value) if len(value) == 3 else [float(x) for x in value]
        self.keys.setdefault(channel, []).append(Key(time, value, interp))
        return self

    # ---- movement vocabulary --------------------------------------------------------------------
    def move(self, t0, t1, p0, p1, target0=None, target1=None, interp="easeInOut"):
        """A straight dolly/crane from p0 to p1 over [t0, t1], the target optionally moving too."""
        self.position = vec(p0)
        self.key("position", t0, p0, interp).key("position", t1, p1, "linear")
        if target0 is not None:
            self.target = vec(target0)
            self.key("target", t0, target0, interp).key("target", t1, target1 if target1 is not None else target0,
                                                        "linear")
        return self

    def path(self, keys, interp="smooth", channel="position"):
        """Several keys along a curve: [(t, point), ...]. `smooth` is clamped Catmull-Rom."""
        for i, (t, p) in enumerate(keys):
            self.key(channel, t, p, interp if i + 1 < len(keys) else "linear")
        if channel == "position":
            self.position = vec(keys[0][1])
        elif channel == "target":
            self.target = vec(keys[0][1])
        return self

    def zoom(self, t0, t1, f0, f1, interp="easeInOut"):
        self.focal = float(f0)
        self.key("focalLength", t0, f0, interp).key("focalLength", t1, f1, "linear")
        return self

    def orbit(self, t0, t1, radius, height, a0_deg, a1_deg, steps=8, interp="smooth"):
        """Keys `followOffset` around the followed node from angle a0 to a1 (degrees, about +Y,
        0 = +X, 90 = +Z). Needs `follow`."""
        assert self.follow, "orbit needs a follow node"
        for i in range(steps + 1):
            u = i / steps
            a = math.radians(a0_deg + (a1_deg - a0_deg) * u)
            off = [radius * math.cos(a), height, radius * math.sin(a)]
            self.key("followOffset", t0 + (t1 - t0) * u, off, interp if i < steps else "linear")
            if i == 0:
                self.follow_offset = off
        return self

    def ride(self, t0, t1, s0, s1, interp="easeInOut"):
        """Along the spline from splineT s0 to s1."""
        assert self.spline, "ride needs a spline"
        self.spline_t = float(s0)
        self.key("splineT", t0, s0, interp).key("splineT", t1, s1, "linear")
        return self

    # ---- output ---------------------------------------------------------------------------------
    def camera_json(self, ident, slug):
        cam = {"id": ident, "name": self.name, "slug": slug,
               "placement": "spline" if self.spline else "free",
               "position": self.position, "target": self.target,
               "fov": round(focal_to_vfov(self.focal), 4), "focalLength": self.focal,
               "autoDirector": False}
        if self.aim:
            cam["aimNode"] = self.aim
            cam["aimOffset"] = self.aim_offset
        if self.follow:
            cam["followNode"] = self.follow
            cam["followOffset"] = self.follow_offset
            if self.follow_local:
                cam["followLocal"] = True
            if self.lag > 0.0:
                cam["followLagSeconds"] = self.lag
            if self.clearance > 0.0:
                cam["followClearance"] = self.clearance
        if self.spline:
            cam["spline"] = self.spline
            cam["splineT"] = self.spline_t
            cam["lookAhead"] = self.look_ahead
            cam["splineOffset"] = self.spline_offset
        return cam

    def tracks_json(self, slug):
        tracks = []
        for channel, keys in self.keys.items():
            keys = sorted(keys, key=lambda k: k.time)
            tracks.append({
                "target": f"cameras/{slug}/{channel}", "component": -1, "timeBase": "seconds",
                "mode": "replace", "loopLength": 0.0, "enabled": True,
                "keys": [{"time": round(k.time, 6), "value": [round(x, 5) for x in k.value], "interp": k.interp}
                         for k in keys],
            })
        return tracks


class Shot:
    """One locked span of the cut and the rig that is live for it, with the production notes the
    shot plan documents (purpose, subject, musical relationship, effects, modulation)."""

    def __init__(self, sid, start, end, rig, segment="", purpose="", subject="", camera="", movement="",
                 music="", effects="", modulation="", transition="cut", blend=0.0, status="planned",
                 notes=""):
        self.sid = sid
        self.start = float(start)
        self.end = float(end)
        self.rig = rig
        self.segment = segment
        self.purpose = purpose
        self.subject = subject
        self.camera = camera
        self.movement = movement
        self.music = music
        self.effects = effects
        self.modulation = modulation
        self.transition = transition
        self.blend = float(blend)
        self.status = status
        self.notes = notes

    @property
    def duration(self):
        return self.end - self.start
