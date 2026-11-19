"""Neon Dreams: load the kit's GLBs into Blender and instance them with per-instance light-role tints.

This is the reference reading of art/layout.json (the engine's build.py does the same thing in AV Gen):
  * a kit asset is imported once; instances share its mesh;
  * every emissive role (win_warm, win_cool, strip, neon_a, garden, ...) of an instance takes its colour from a palette key
    (the instance's "tint" map, else the role's default key) times the grey level texture baked in the GLB;
  * positions in layout.json are engine coordinates (Y-up); Blender is Z-up: blender = (x, -z, y), yaw about Y = yaw about Z.
"""
import bpy, os, math, json
from mathutils import Vector, Matrix, Euler

DEFAULT_KEY = dict(win_warm="win_d", win_cool="win_a", strip="eave", beacon="beacon", landmark="landmark", garden="garden_a", lantern="amber",
                   keel="bounce_a", neon_a="sign_a", beam_lamp="moon_disc", beam="moon_disc", cond_glow="cond_a", cond_lamp="cond_a", cond_work="cond_c")
BINDS = []            # (setter(rgb), key): filled as tinted materials are made; apply_palette() runs them
_TEMPLATES, _BASE, _VARIANTS = {}, {}, {}
GAIN = {}             # role -> multiplier over the GLB's emissive strength (scene-level tuning)


def hex2lin(h):
    h = h.lstrip('#'); c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple((x / 12.92) if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def role_of(mat):
    return mat.name.split(".")[0]


def _principled(mat):
    return next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)


def variant(role, key, gain=1.0):
    """The material of `role` tinted by palette `key`. Non-emissive roles are shared untinted (key None)."""
    k = (role, key, round(gain, 3))
    if k in _VARIANTS:
        return _VARIANTS[k]
    base = _BASE[role]
    if key is None:
        _VARIANTS[k] = base
        return base
    m = base.copy(); m.name = "%s@%s" % (role, key)
    nt = m.node_tree; b = _principled(m)
    es = b.inputs['Emission Strength']; ec = b.inputs['Emission Color']
    es.default_value = es.default_value * gain * GAIN.get(role, 1.0)
    rgb = nt.nodes.new('ShaderNodeRGB')
    if ec.links:                                              # level texture (or sign atlas) x palette colour
        src = ec.links[0].from_socket
        mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.inputs[0].default_value = 1.0
        nt.links.new(src, mix.inputs[6]); nt.links.new(rgb.outputs[0], mix.inputs[7]); nt.links.new(mix.outputs[2], ec)
    else:
        nt.links.new(rgb.outputs[0], ec)
    BINDS.append((lambda c, s=rgb.outputs[0]: setattr(s, "default_value", (c[0], c[1], c[2], 1.0)), key))
    _VARIANTS[k] = m
    return m


def load(kit_dir, asset):
    """Import one kit asset (path relative to the kit folder) and return its template object (hidden, at the origin)."""
    if asset in _TEMPLATES:
        return _TEMPLATES[asset]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(kit_dir, asset))
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == 'MESH']
    ob = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.join()
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    ob.parent = None
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in new:
        if o != ob and o.name in bpy.data.objects and o.type != 'MESH':
            bpy.data.objects.remove(o)
    roles = []
    for i, slot in enumerate(ob.material_slots):
        r = role_of(slot.material)
        roles.append(r)
        if r not in _BASE:
            _BASE[r] = slot.material; slot.material.name = r
            b = _principled(slot.material)
            if b and r in ("facade", "trim", "metal", "deck", "glass", "foliage", "cloth", "pool", "figure"):
                pass
        else:
            ob.data.materials[i] = _BASE[r]
    ob["roles"] = roles
    ob.hide_render = True; ob.hide_viewport = True
    _TEMPLATES[asset] = ob
    return ob


def place(kit_dir, asset, pos, yaw_deg=0.0, scale=(1, 1, 1), tint=None, gain=None, coll=None, pitch_deg=0.0, roll_deg=0.0, name=None):
    """Instance `asset` at an ENGINE position (x, y up, z). yaw about the engine's +y; pitch about its local x; roll about z."""
    t = load(kit_dir, asset)
    o = bpy.data.objects.new(name or os.path.basename(asset).split(".")[0], t.data)
    (coll or bpy.context.scene.collection).objects.link(o)
    o.location = (pos[0], -pos[2], pos[1])
    o.rotation_euler = Euler((math.radians(pitch_deg), math.radians(roll_deg), math.radians(yaw_deg)), 'ZYX')
    o.scale = (scale[0], scale[2], scale[1]) if not isinstance(scale, (int, float)) else (scale, scale, scale)
    tint = tint or {}; gain = gain or {}
    for i, r in enumerate(t["roles"]):
        key = tint.get(r, DEFAULT_KEY.get(r))
        if key is None:
            continue
        o.material_slots[i].link = 'OBJECT'
        o.material_slots[i].material = variant(r, key, gain.get(r, 1.0))
    return o


def apply_palette(pal):
    """pal: key -> linear rgb."""
    for setter, key in BINDS:
        setter(pal[key])


def load_palette(path, state):
    d = json.load(open(path))
    names = {"uv": "Ultraviolet Dream", "blue": "Electric Blue", "rose": "Neon Rose", "amber": "Amber Eclipse"}
    st = d["states"][names.get(state, state)]
    pal = {k: tuple(v["linear"]) for k, v in st.items()}
    for k, v in d["fixed"].items():
        pal[k] = tuple(v["linear"])
    return pal
