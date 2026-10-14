"""Colourways for the hero library: recoloured copies of the exported GLBs (system python3 + Pillow, not Blender).

    python3 colorways.py <raw dir (build.py output)> <out dir> [species ...]

For every species it writes the default colourway (hero_<species>_lod{0,1}.glb) and the variants in COLORWAYS
(hero_<species>_<colour>_lod{0,1}.glb, plus hero_<species>_<colour>.emit.json). The meshes are untouched: only the
emissive images, and for the cool variants the base-colour images, are rewritten inside each GLB.

The recolour keeps the localised anatomy and its internal hue variation. Every emissive texel's hue is assigned to
one of the source bands of the family palette (green, cyan, blue-violet), and each band maps to its own target hue,
so a green body with cyan tips becomes, say, a violet body with blue tips. Saturation is boosted toward the
reference. A body glow is then added under the anatomy: a translucent, lit-from-within body, modulated by the
painted tissue thickness (the base texture's lightness, which is lighter where the tissue is thin), in the variant's
body colour. Veins, tips and rims stay on top.

Reference palette (sampled from 00-reference.png; see 03-hero-assets.md):
  chartreuse (0.75,1.0,0.15) / lime (0.58,0.92,0.44) / emerald (0.1,1.0,0.4) / teal-green (0.2,0.91,0.75) /
  cyan (0.19,0.81,0.93) / electric blue (0.15,0.4,1.0) / blue (0.33,0.39,0.82) / violet (0.55,0.25,1.0) /
  magenta-pink (0.79,0.39,0.87) / gold (1.0,0.75,0.25) / amber (0.92,0.67,0.34).
"""
from __future__ import annotations

import io
import json
import os
import shutil
import struct
import sys

import numpy as np
from PIL import Image

# ---- target colours (sRGB-ish display values, as specified by the lead) ---------------------------------------

C = {
    "emerald": (0.10, 1.00, 0.40), "chartreuse": (0.75, 1.00, 0.15), "lime": (0.58, 0.92, 0.44),
    "teal": (0.20, 0.91, 0.75), "cyan": (0.19, 0.81, 0.93), "blue": (0.15, 0.40, 1.00),
    "deepblue": (0.33, 0.39, 0.82), "violet": (0.55, 0.25, 1.00), "magenta": (0.79, 0.39, 0.87),
    "gold": (1.00, 0.75, 0.25), "amber": (0.92, 0.60, 0.25), "mint": (0.55, 1.0, 0.75),
    # warm and magenta accents (the owner, 2026-10-09: "more colour")
    "magentaw": (0.88, 0.15, 0.78), "rose": (1.0, 0.33, 0.58), "pink": (1.0, 0.55, 0.78),
    "amberw": (1.0, 0.6, 0.12), "orange": (1.0, 0.42, 0.08), "coral": (1.0, 0.32, 0.26),
    "deepviolet": (0.42, 0.08, 1.0), "indigo": (0.26, 0.16, 1.0),
}

# a colourway: what the three source bands (green, cyan, blue/violet) become, the body glow's colour and level,
# a saturation boost, and whether the tissue (base colour) is tinted with the same hue map
VARIANTS = {
    "default":    dict(green="emerald", cyan="cyan", blue="blue", body="emerald", tint=False),
    "emerald":    dict(green="emerald", cyan="teal", blue="emerald", body="emerald", tint=False),
    "emeraldcyan": dict(green="emerald", cyan="cyan", blue="cyan", body="emerald", tint=False),  # cyan rims
    "chartreuse": dict(green="chartreuse", cyan="lime", blue="emerald", body="chartreuse", tint=False),
    "gold":       dict(green="gold", cyan="amber", blue="gold", body="amber", tint=False),
    "goldtip":    dict(green="cyan", cyan="gold", blue="cyan", body="cyan", tint=False),  # gold tips, cyan body
    "blue":       dict(green="blue", cyan="cyan", blue="deepblue", body="blue", tint=True),
    "violet":     dict(green="violet", cyan="blue", blue="magenta", body="violet", tint=True),
    "cyan":       dict(green="cyan", cyan="teal", blue="blue", body="cyan", tint=True),
    # warm and magenta accents: each source band goes to a neighbouring hue, so a body and its tips still differ
    "magenta":    dict(green="magentaw", cyan="rose", blue="deepviolet", body="magentaw", tint=True),
    "rose":       dict(green="rose", cyan="pink", blue="magentaw", body="rose", tint=True),
    "amber":      dict(green="amberw", cyan="gold", blue="orange", body="amberw", tint=False),
    "orange":     dict(green="orange", cyan="amberw", blue="coral", body="orange", tint=False),
    "coral":      dict(green="coral", cyan="rose", blue="orange", body="coral", tint=False),
    "deepviolet": dict(green="deepviolet", cyan="indigo", blue="magentaw", body="deepviolet", tint=True),
    "ambertip":   dict(green="teal", cyan="amberw", blue="teal", body="teal", tint=False),  # amber tips, teal body
    "ambersparks": dict(green="amberw", cyan="cyan", blue="blue", body="emerald", tint=False),  # amber specks only
}

def _hex(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


C.update({"lead_magenta": _hex("#D34BDB"), "lead_magenta_deep": (0.6, 0.16, 0.74), "lead_coral": _hex("#FF6F59"),
          "lead_warmpink": (1.0, 0.5, 0.62), "lead_amber": _hex("#FFB347"), "lead_orange": _hex("#FF8C42"),
          "lead_rose": _hex("#E07A9A"), "lead_rose_light": (0.96, 0.6, 0.76), "lead_violet": (0.55, 0.25, 1.0),
          "lead_core_orange": (1.0, 0.5, 0.14), "lead_blue": (0.25, 0.3, 0.9)})

# The lead's exact requests (2026-10-09): per-species band maps, so the colour lands on the named anatomy.
LEAD_VARIANTS = {
    # sacs #D34BDB, deeper at the base (the blue-band sacs and stems go deep magenta)
    ("beadcluster", "magenta"): dict(green="lead_magenta", cyan="lead_magenta_deep", blue="lead_magenta_deep",
                                     body="lead_magenta", tint=True),
    # #FF6F59 sacs, warm pink rims
    ("beadcluster", "coral"): dict(green="lead_coral", cyan="lead_warmpink", blue="lead_warmpink", body="lead_coral",
                                   tint=False),
    # #FFB347 berries, orange cores
    ("lanternberry", "amber"): dict(green="lead_amber", cyan="lead_core_orange", blue="lead_amber", body="lead_amber",
                                    tint=False),
    # #FF8C42 bulbs, amber stalk glow (kept low: the stalk's emission is already low)
    ("bulbstalk", "orange"): dict(green="lead_orange", cyan="lead_amber", blue="lead_orange", body="lead_orange",
                                  tint=False),
    # #E07A9A fronds (the margins and outer veins), violet midrib (the emerald band at the centre)
    ("veilfern", "rose"): dict(green="lead_violet", cyan="lead_rose", blue="lead_rose_light", body="lead_rose",
                               tint=True),
    # magenta tips (the cyan band toward the tips) on a violet body (the emerald band low down)
    ("filigree", "magenta"): dict(green="lead_violet", cyan="lead_magenta", blue="lead_magenta", body="lead_violet",
                                  tint=True),
    # amber nodules and specks on the dark moss; the threads stay blue
    ("glowmound", "amber"): dict(green="lead_amber", cyan="lead_amber", blue="lead_blue", body="emerald", tint=False),
}

# the warm pass: which species take which warm colourway (exported in addition to COLORWAYS)
WARM = {
    "beadcluster": ["magenta", "coral", "rose"], "lanternberry": ["amber", "magenta", "rose", "orange"],
    "bulbstalk": ["orange", "amber", "coral"], "veilfern": ["rose", "deepviolet"], "filigree": ["magenta", "ambertip"],
    "glowmound": ["amber", "ambersparks"], "chalice": ["magenta", "rose"], "bellspire": ["magenta", "rose"],
    "glowmat": ["ambersparks"], "lacefan": ["deepviolet"], "reliquary": ["deepviolet"],
}

# per species: the colourways to make, and the body-glow level per material slot (0 = keep the slot as painted)
COLORWAYS = {
    "bulbstalk":    (["chartreuse", "gold", "blue", "violet"], {"*": 0.10, "leaf": 0.0}),
    "glowmat":      (["emerald", "blue", "violet", "chartreuse"], {"*": 0.0, "sporophyte": 0.1}),
    "glowmound":    (["emerald", "blue", "violet", "chartreuse"], {"*": 0.0}),
    "filigree":     (["violet", "goldtip", "blue"], {"*": 0.06}),
    "lanternberry": (["gold", "blue", "violet"], {"*": 0.08, "body": 0.0}),
    "veilcap":      (["emeraldcyan", "chartreuse", "violet", "blue"], {"*": 0.04, "stalk": 0.015}),
    "glowsedge":    (["blue", "violet", "chartreuse"], {"*": 0.0, "seedhead": 0.12}),
    "chalice":      (["violet", "blue", "chartreuse"], {"*": 0.06, "bell": 0.03}),
    "veilfern":     (["violet", "blue", "chartreuse"], {"*": 0.05}),
    "bellspire":    (["violet", "gold", "blue"], {"*": 0.06}),
    "lattice":      (["violet", "cyan"], {"*": 0.05}),
    "reliquary":    (["violet", "gold"], {"*": 0.06, "rib": 0.0}),
    "lacefan":      (["violet", "cyan", "emerald"], {"*": 0.06}),
    "beadcluster":  (["emerald", "violet", "gold"], {"*": 0.08}),
}

# ---- colour maths ---------------------------------------------------------------------------------------------


def rgb_to_hsv(a):
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = a.max(-1)
    mn = a.min(-1)
    d = mx - mn
    h = np.zeros_like(mx)
    m = d > 1e-6
    rr = m & (mx == r)
    gg = m & (mx == g) & ~rr
    bb = m & ~rr & ~gg
    h[rr] = ((g - b)[rr] / d[rr]) % 6
    h[gg] = (b - r)[gg] / d[gg] + 2
    h[bb] = (r - g)[bb] / d[bb] + 4
    h = h * 60.0
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0)
    return h, s, mx


def hsv_to_rgb(h, s, v):
    h = (h % 360) / 60.0
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p = v * (1 - s)
    q = v * (1 - s * f)
    t = v * (1 - s * (1 - f))
    out = np.zeros(h.shape + (3,))
    for k, (a, b, c) in enumerate([(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)]):
        m = i == k
        out[m] = np.stack([a[m], b[m], c[m]], -1)
    return out


def hue_of(rgb):
    h, s, v = rgb_to_hsv(np.array([rgb], dtype=float))
    return float(h[0]), float(s[0])


def band_weights(h):
    """Soft membership of a hue in the family's three source bands."""
    def bump(c, w):
        d = np.abs(((h - c + 180) % 360) - 180)
        return np.clip(1 - d / w, 0, 1)
    g = bump(140, 45)
    c = bump(193, 30)
    b = bump(240, 45)
    tot = g + c + b
    return g, c, b, tot


def remap(rgb, var, sat_boost=1.6, gain=1.15):
    """Recolour an (H, W, 3) display-referred image by bands; returns the new image."""
    h, s, v = rgb_to_hsv(rgb)
    g, c, b, tot = band_weights(h)
    tg = [hue_of(C[var[k]]) for k in ("green", "cyan", "blue")]
    # circular weighted mean of the target hues
    ang = np.zeros(h.shape + (2,))
    sats = np.zeros_like(h)
    for w, (th, ts) in zip((g, c, b), tg):
        ang[..., 0] += w * np.cos(np.radians(th))
        ang[..., 1] += w * np.sin(np.radians(th))
        sats += w * ts
    known = tot > 1e-3
    nh = np.where(known, np.degrees(np.arctan2(ang[..., 1], ang[..., 0])), h)
    tsat = np.where(known, sats / np.maximum(tot, 1e-6), s)
    # keep pale cores pale: blend the source saturation toward the target's, scaled by how saturated it was
    ns = np.clip(np.where(known, np.minimum(s * sat_boost, 1.0) * (0.35 + 0.65 * tsat), s), 0, 1)
    nv = np.clip(v * gain, 0, 1)
    return hsv_to_rgb(nh, ns, nv)


def srgb_to_lin(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def recolor_emissive(emis_png, base_png, var, body_level):
    E = np.asarray(Image.open(io.BytesIO(emis_png)).convert("RGB")).astype(float) / 255
    out = remap(E, var)
    if body_level > 0 and base_png is not None:
        B = np.asarray(Image.open(io.BytesIO(base_png)).convert("RGBA")).astype(float) / 255
        if B.shape[:2] == E.shape[:2]:
            lum = srgb_to_lin(B[..., :3]) @ np.array([0.2126, 0.7152, 0.0722])
            alpha = B[..., 3]
            solid = alpha > 0.5
            if solid.any():
                p95 = np.percentile(lum[solid], 95)
                thin = np.clip(lum / max(p95, 1e-4), 0, 1) ** 2.2  # lighter tissue is thinner: it glows more
                body = np.array(C[var["body"]])
                body_lin = srgb_to_lin(body) * body_level * 0.5 * (0.15 + 0.85 * thin)[..., None] * solid[..., None]
                out = lin_to_srgb(srgb_to_lin(out) + body_lin)
    img = Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).convert("RGBA")
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def tint_base(base_png, var):
    B = np.asarray(Image.open(io.BytesIO(base_png)).convert("RGBA")).astype(float) / 255
    rgb = remap(B[..., :3], var, sat_boost=1.0, gain=1.0)
    out = np.concatenate([rgb, B[..., 3:]], -1)
    buf = io.BytesIO()
    Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(buf, "PNG", optimize=True)
    return buf.getvalue()


# ---- GLB rewrite ------------------------------------------------------------------------------------------------


def read_glb(path):
    d = open(path, "rb").read()
    jl, = struct.unpack_from("<I", d, 12)
    js = json.loads(d[20:20 + jl])
    off = 20 + jl
    bl, = struct.unpack_from("<I", d, off)
    binc = d[off + 8:off + 8 + bl]
    views = []
    for bv in js["bufferViews"]:
        o = bv.get("byteOffset", 0)
        views.append(binc[o:o + bv["byteLength"]])
    return js, views


def write_glb(path, js, views):
    blob = bytearray()
    for bv, data in zip(js["bufferViews"], views):
        pad = (-len(blob)) % 4
        blob += b"\0" * pad
        bv["byteOffset"] = len(blob)
        bv["byteLength"] = len(data)
        blob += data
    blob += b"\0" * ((-len(blob)) % 4)
    js["buffers"] = [{"byteLength": len(blob)}]
    jb = json.dumps(js, separators=(",", ":")).encode()
    jb += b" " * ((-len(jb)) % 4)
    total = 12 + 8 + len(jb) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(jb), 0x4E4F534A))
        f.write(jb)
        f.write(struct.pack("<II", len(blob), 0x004E4942))
        f.write(bytes(blob))


def image_view(js, tex_index):
    img = js["images"][js["textures"][tex_index]["source"]]
    return img["bufferView"]


def recolor_glb(src, dst, var, levels, species):
    js, views = read_glb(src)
    done = {}
    for m in js["materials"]:
        slot = m["name"][len(species) + 1:] if m["name"].startswith(species + "_") else m["name"]
        level = levels.get(slot, levels.get("*", 0.0))
        et = m.get("emissiveTexture")
        bt = m.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        base_png = views[image_view(js, bt["index"])] if bt else None
        if et:
            v = image_view(js, et["index"])
            if v not in done:
                done[v] = recolor_emissive(views[v], base_png, var, level)
        if bt and var.get("tint"):
            v = image_view(js, bt["index"])
            if v not in done:
                done[v] = tint_base(views[v], var)
    for v, data in done.items():
        views[v] = data
    write_glb(dst, js, views)


def warm(raw, out):
    """Only the warm pass's variants, with each species' own body-glow levels."""
    os.makedirs(out, exist_ok=True)
    for sp, names in WARM.items():
        _, levels = COLORWAYS[sp]
        for colour in names:
            tag = f"{sp}_{colour}"
            for lod in (0, 1):
                src = os.path.join(raw, f"hero_{sp}_lod{lod}.glb")
                var = LEAD_VARIANTS.get((sp, colour), VARIANTS.get(colour))
                recolor_glb(src, os.path.join(out, f"hero_{tag}_lod{lod}.glb"), var, levels, sp)
            emit = os.path.join(raw, f"hero_{sp}.emit.json")
            if os.path.exists(emit):
                shutil.copy2(emit, os.path.join(out, f"hero_{tag}.emit.json"))
            print("colourway", tag)


def main(argv):
    if argv[0] == "--warm":
        return warm(argv[1], argv[2])
    raw, out = argv[0], argv[1]
    species = argv[2:] or list(COLORWAYS)
    os.makedirs(out, exist_ok=True)
    if not argv[2:]:  # assets without colourways pass through unchanged
        for f in sorted(os.listdir(raw)):
            sp = f[5:].rsplit("_lod", 1)[0].replace(".emit.json", "") if f.startswith("hero_") else None
            if sp and sp not in COLORWAYS:
                shutil.copy2(os.path.join(raw, f), os.path.join(out, f))
    for sp in species:
        names, levels = COLORWAYS[sp]
        for colour in ["default"] + names:
            var = VARIANTS[colour]
            tag = sp if colour == "default" else f"{sp}_{colour}"
            for lod in (0, 1):
                src = os.path.join(raw, f"hero_{sp}_lod{lod}.glb")
                if not os.path.exists(src):
                    continue
                recolor_glb(src, os.path.join(out, f"hero_{tag}_lod{lod}.glb"), var, levels, sp)
            emit = os.path.join(raw, f"hero_{sp}.emit.json")
            if os.path.exists(emit):
                shutil.copy2(emit, os.path.join(out, f"hero_{tag}.emit.json"))
            print("colourway", tag)


if __name__ == "__main__":
    main(sys.argv[1:])
