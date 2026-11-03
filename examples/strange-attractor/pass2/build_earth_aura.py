"""The Earth's aura (Art Pass 2, M3e; owner §6: replace the ring of white pixel blocks with a glowing aura -- soft,
luminous and atmospheric, hugging the limb and falling off smoothly, stylised and vibrant -- with glitch and datamosh).

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_earth_aura.py -- assets/strange-attractor/build

Writes, each a flat emissive card in the plane facing the viewer, radius 10 = the Earth's (the scene scales it by the
Earth's radius / 10, as the M3c halos), set just behind the planet:
- earth-aura.glb: the aura. An image of a radial falloff: it rises over the limb (0.95-1.0 R), is brightest just
  outside it, and falls away smoothly (a tight exponential and a broad one) to nothing by 1.9 R; cyan-white at the
  limb, through azure, to violet at its outer reach. Alpha-blended, so it is a glow, not a disc.
- earth-aura-tear.glb: the same aura, torn: horizontal bands slid sideways (scanline tears) and square blocks lifted
  and displaced (block displacement), the torn parts split into magenta and cyan. The scene flashes it on the highs.
Everything is generated here (no downloads).
"""
import math
import os
import sys

import bpy
import numpy as np

out_dir = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
SIZE = 1024
EXTENT = 2.0            # the card spans -2R..2R


def aura_image(seed=None):
    y, x = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    u = (x + 0.5) / SIZE * 2.0 - 1.0
    v = (y + 0.5) / SIZE * 2.0 - 1.0
    r = np.sqrt(u * u + v * v) * EXTENT                      # in Earth radii
    rise = np.clip((r - 0.95) / 0.05, 0.0, 1.0)
    rise = rise * rise * (3.0 - 2.0 * rise)
    d = np.maximum(r - 1.0, 0.0)
    fall = 0.75 * np.exp(-d / 0.07) + 0.35 * np.exp(-d / 0.32)
    edge = np.clip((1.9 - r) / 0.4, 0.0, 1.0)
    i = rise * fall * edge * edge
    t = np.clip(d / 0.6, 0.0, 1.0)[..., None]
    limb = np.array([0.75, 0.95, 1.0], np.float32)
    mid = np.array([0.25, 0.6, 1.0], np.float32)
    outer = np.array([0.55, 0.3, 1.0], np.float32)
    col = np.where(t < 0.4, limb + (mid - limb) * (t / 0.4), mid + (outer - mid) * ((t - 0.4) / 0.6))
    rgba = np.concatenate([col * np.clip(i, 0, 1)[..., None] * 1.0, np.clip(i * 1.15, 0.0, 1.0)[..., None]], axis=-1)
    if seed is not None:
        rng = np.random.default_rng(seed)
        torn = rgba.copy()
        # scanline tears: bands slid sideways, split magenta / cyan
        for _ in range(16):
            y0 = int(rng.integers(0, SIZE - 8))
            h = int(rng.integers(3, 26))
            sh = int(rng.integers(-90, 90))
            band = np.roll(rgba[y0:y0 + h], sh, axis=1)
            tint = np.array([1.0, 0.25, 0.85, 1.0] if rng.random() < 0.5 else [0.25, 0.95, 1.0, 1.0], np.float32)
            torn[y0:y0 + h] = band * tint
        # block displacement: square blocks lifted from the ring and dropped elsewhere along it
        for _ in range(26):
            b = int(rng.integers(16, 56))
            a = rng.uniform(0, 2 * math.pi)
            rr = rng.uniform(1.0, 1.35) / EXTENT * SIZE / 2.0
            cx, cy = SIZE / 2 + rr * math.cos(a), SIZE / 2 + rr * math.sin(a)
            dx, dy = rng.integers(-70, 70), rng.integers(-24, 24)
            x0, y0 = int(np.clip(cx - b / 2, 0, SIZE - b)), int(np.clip(cy - b / 2, 0, SIZE - b))
            x1, y1 = int(np.clip(x0 + dx, 0, SIZE - b)), int(np.clip(y0 + dy, 0, SIZE - b))
            torn[y1:y1 + b, x1:x1 + b] = np.maximum(torn[y1:y1 + b, x1:x1 + b], rgba[y0:y0 + b, x0:x0 + b] * 1.3)
        rgba = np.clip(torn, 0.0, 1.0)
    return rgba


def card(name, rgba):
    img = bpy.data.images.new(name, SIZE, SIZE, alpha=True)
    img.pixels.foreach_set(np.flipud(rgba).astype(np.float32).ravel())
    img.file_format = "PNG"
    img.pack()
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.blend_method = "BLEND"
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    b.inputs["Base Color"].default_value = (0, 0, 0, 1)
    nt.links.new(tex.outputs["Color"], b.inputs["Emission Color"])
    nt.links.new(tex.outputs["Alpha"], b.inputs["Alpha"])
    b.inputs["Emission Strength"].default_value = 4.0
    bpy.ops.mesh.primitive_plane_add(size=2.0 * EXTENT * 10.0)
    o = bpy.context.object
    o.name = name
    o.data.materials.append(m)
    return o


bpy.ops.wm.read_factory_settings(use_empty=True)
for name, seed in (("earth-aura", None), ("earth-aura-tear", 1906)):
    o = card(name, aura_image(seed))
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    path = f"{out_dir}/{name}.glb"
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    print("AURA", path)
    o.hide_set(True)
