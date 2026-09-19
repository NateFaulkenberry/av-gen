#!/usr/bin/env python3
"""Split the Tree of Life hero GLB into five Glowmere emission layers.

    /Applications/Blender.app/Contents/Resources/5.2/python/bin/python3.13 \
        tools/glowmere_tree_layers.py \
        --in assets/treeisle/tree-of-life-hero.glb \
        --out assets/treeisle

(any Python 3.11+ with numpy will do; Blender's bundled interpreter is named because this
machine's system Python has no numpy.)

WHY THIS SUPERSEDES tools/glowmere_tree_materials.py
----------------------------------------------------

ADR-338 read the Glowmere palette off a *render* -- `hero_pass/renders/Glowmere.png` -- and wrote
seven flat material factors. That was the right move with the information it had, and it is why
the canopy came out flat: seven flat factors cannot hold a gradient, and they certainly cannot
hold "6% of the leaves are lit and the other 94% are not".

This tool reads the palette off the *source*, `~/Desktop/tree_mdl/hero_pass/Tree_of_Life_Hero.blend`,
and it turns out the Blender shading is not a palette at all. It is four masks:

  leaves    emission strength = Bioluminescence * ((ObjectInfo.Random > 0.94) * 1.1 + 0.035)
            base colour       = ramp01( clamp((z - 20)/28) + ObjectInfo.Random * 0.23 )
  tracery   emission strength = 1.6 * 1.2 * (noise(pos, scale 2.6, detail 3) > 0.53)
            emission colour   = ramp( z / 43 )
  fungi     emission strength = 1.3 * (Normal.z < -0.25)          <- the gills, not the caps
            base colour       = mix(copper, teal, Normal.z < -0.25)
  wood      emission strength = 0.0                               <- the dark that makes the rest read

`ObjectInfo.Random` is per *instance*. That is the whole ballgame for the canopy, and the export
preserved it by accident: the leaf meshes are contiguous instance blocks of 18 vertices / 6
triangles each -- 51,633 leaves on the peripheral mesh and 122,767 on the broadleaf canopy -- so
`instance = vertex_index // 18` recovers exactly the grouping Blender randomised over. The masks
are therefore not approximated away; they are baked into glTF materials, one material per bucket.

The wood/twig masks are 3D noise fields. Blender's Perlin is not reimplemented here -- a value
noise of the same spatial frequency is used and its threshold is *calibrated to the same coverage
fraction*, which is the property the picture depends on. The coverage actually achieved is printed
and recorded in the manifest, so the approximation is auditable rather than asserted.

WHAT IS AND IS NOT TOUCHED
--------------------------

Five files come out, split by emission role so each can be an AV Gen node with its own
`material/emissiveBoost` -- which is the only per-node material lever a `kind: "gltf"` node has,
and which is *scalar and whole-instance*. Splitting by role is what turns one scalar into the five
separate control channels the brief's §8 hierarchy and §10 parameter list ask for:

    tree-glowmere-tracery.glb   major luminous structures   tree.glow.branchIntensity
    tree-glowmere-twigs.glb     secondary glow              tree.glow.secondaryIntensity
    tree-glowmere-foliage.glb   foliage                     tree.glow.foliageIntensity
    tree-glowmere-lumens.glb    special organisms           tree.glow.lumenIntensity
    tree-glowmere-wood.glb      dark structural             (non-emissive by construction)

**Vertex data is copied byte for byte.** POSITION and NORMAL bufferViews are memcpy'd out of the
source's binary chunk and the SHA-256 of every one is checked against the source's before the file
is written. Nothing here can move a vertex. Index buffers *are* rewritten -- that is how a
primitive gets split -- and the tool verifies instead that the multiset of triangles per source
mesh is unchanged: every triangle lands in exactly one bucket, none is invented and none is lost.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import numpy as np

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

COMPONENT = {5120: (np.int8, 1), 5121: (np.uint8, 1), 5122: (np.int16, 2),
             5123: (np.uint16, 2), 5125: (np.uint32, 4), 5126: (np.float32, 4)}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

# ---------------------------------------------------------------------------------------------
# The Blender shading model, transcribed. Every number is from Tree_of_Life_Hero.blend, read with
# tools/inspect (see the ADR); the preset knobs Magic / Bioluminescence / Vein brightness /
# Root glow are all 1.0 and Timeline growth is 300.0 as saved, which is the Glowmere preset.
#
# Blender is Z-up and the glTF export is Y-up: Blender's Z is the GLB's Y, in both positions and
# normals. Every `z` in the .blend below is read against the GLB's y.
# ---------------------------------------------------------------------------------------------

LEAF_EMISSIVE = [0.06, 0.45, 0.70]     # Principled Emission Color, both leaf materials
LEAF_BRIGHT = 1.1 + 0.035              # ObjectInfo.Random > 0.94
LEAF_DIM = 0.035                       # the floor the other 94% sit at
LEAF_RANDOM_CUT = 0.94
LEAF_HEIGHT_LO, LEAF_HEIGHT_SPAN = 20.0, 28.0   # (z - 20) / 28
LEAF_RANDOM_TINT = 0.23                # + ObjectInfo.Random * 0.23
LEAF_ROUGHNESS = 0.48

# Color Ramp.001 in each leaf material: the one Mix (Legacy) selects at Magic = 1.0.
LEAF_RAMP = {
    # GLB mesh 3, node "EXPORT | LIFE | Delicate peripheral leaves": the ".001" material,
    # whose top stop is the warm terracotta that reads as the cream-white break in the reference.
    3: ([0.025, 0.17, 0.12], [0.60, 0.22, 0.13]),
    # GLB mesh 4, node "EXPORT | LIFE | Layered broadleaf canopy".
    4: ([0.025, 0.17, 0.12], [0.35, 0.46, 0.22]),
}

TRACERY_BASE = [0.025, 0.10, 0.08]
TRACERY_ROUGHNESS = 0.58
TRACERY_STRENGTH = 1.6 * ((300.0 - 270.0) / 25.0)   # Math.012 * the timeline-growth term = 1.92
TRACERY_NOISE_SCALE = 2.6
TRACERY_COVERAGE = 0.47            # what Blender's noise(2.6, detail 3) > 0.53 covers
TRACERY_RAMP = [(0.00, [0.04, 0.65, 0.40]),
                (0.42, [0.08, 0.36, 0.42]),
                (1.00, [0.95, 0.29, 0.10])]
TRACERY_RAMP_DIVISOR = 43.0

FUNGI_CAP = [0.25, 0.105, 0.075]       # Mix Color1, normal.z >= -0.25
FUNGI_GILL = [0.12, 0.38, 0.30]        # Mix Color2, normal.z < -0.25
FUNGI_EMISSIVE = [0.05, 0.65, 0.32]
FUNGI_STRENGTH = 1.3
FUNGI_NORMAL_CUT = -0.25
FUNGI_ROUGHNESS = 0.57

# GLOWMERE | mineral charcoal, old plates and sapwood. Color Ramp.001 is the one Mix (Legacy).001
# selects at Magic = 1.0; the other chain (the one that multiplies by [0.38, 0.46, 0.5]) has its
# output socket unlinked and is dead. Roughness is 0.58 + 0.32 * noise.
WOOD_RAMP = [(0.25, [0.018, 0.026, 0.026]), (0.77, [0.18, 0.16, 0.14])]
WOOD_NOISE_SCALE = 0.32
WOOD_ROUGH_BASE, WOOD_ROUGH_GAIN = 0.58, 0.32

# AV Gen-native, and declared as such: the brief's §8 asks for a "secondary glow" on "smaller
# portions of the branch network", and the .blend gives the twigs the same non-emissive wood
# material as the trunk. This is the approximation §9 permits -- the same noise field the bark
# colour uses, thresholded high so the glow runs in streaks along branch runs rather than
# speckling, and kept dim enough that it reads as sap under bark and not as a neon tube.
TWIG_GLOW_COVERAGE = 0.12
TWIG_GLOW_EMISSIVE = [0.05, 0.30, 0.45]
TWIG_GLOW_STRENGTH = 0.25

# Which source mesh goes in which output layer, and what that layer is for.
LAYERS = {
    "tracery": {"meshes": [2], "role": "major luminous structures (brief §8)"},
    "twigs":   {"meshes": [6], "role": "secondary glow (brief §8)"},
    "foliage": {"meshes": [3, 4], "role": "foliage (brief §8)"},
    "lumens":  {"meshes": [0, 1], "role": "special organisms: bark fungi and seed lanterns (brief §8)"},
    "wood":    {"meshes": [5], "role": "dark structural, non-emissive (brief §8 'dark areas')"},
}


# ---------------------------------------------------------------------------------------------
# glTF plumbing
# ---------------------------------------------------------------------------------------------

def read_glb(path: Path) -> tuple[dict, bytes]:
    raw = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", raw, 0)
    if magic != GLB_MAGIC:
        raise SystemExit(f"{path}: not a GLB (magic {magic:#x})")
    if length != len(raw):
        raise SystemExit(f"{path}: header length {length} but file is {len(raw)} bytes")
    offset, doc, binary = 12, None, None
    while offset < len(raw):
        clen, ctype = struct.unpack_from("<II", raw, offset)
        payload = raw[offset + 8: offset + 8 + clen]
        if ctype == CHUNK_JSON:
            doc = json.loads(payload)
        elif ctype == CHUNK_BIN:
            binary = payload
        offset += 8 + clen + (-clen % 4)
    if doc is None or binary is None:
        raise SystemExit(f"{path}: missing a JSON or BIN chunk")
    return doc, binary


def write_glb(path: Path, doc: dict, binary: bytes) -> None:
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    bn = binary + b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(js) + 8 + len(bn)
    with path.open("wb") as handle:
        handle.write(struct.pack("<III", GLB_MAGIC, 2, total))
        handle.write(struct.pack("<II", len(js), CHUNK_JSON))
        handle.write(js)
        handle.write(struct.pack("<II", len(bn), CHUNK_BIN))
        handle.write(bn)


def accessor_bytes(doc: dict, binary: bytes, index: int) -> bytes:
    """The exact source bytes an accessor covers, for a tightly packed bufferView."""
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    dtype, size = COMPONENT[acc["componentType"]]
    width = NCOMP[acc["type"]] * size
    stride = view.get("byteStride") or width
    if stride != width:
        raise SystemExit(f"accessor {index}: interleaved bufferViews are not handled")
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return binary[start: start + width * acc["count"]]


def accessor_array(doc: dict, binary: bytes, index: int) -> np.ndarray:
    acc = doc["accessors"][index]
    dtype, _ = COMPONENT[acc["componentType"]]
    arr = np.frombuffer(accessor_bytes(doc, binary, index), dtype=dtype)
    return arr.reshape(acc["count"], NCOMP[acc["type"]])


# ---------------------------------------------------------------------------------------------
# Masks
# ---------------------------------------------------------------------------------------------

def instance_random(count: int, seed: int) -> np.ndarray:
    """A per-instance random in [0, 1), standing in for Blender's ObjectInfo.Random.

    Blender's is a hash of the instance id whose exact form is not part of the look; what the
    look depends on is that there is *one* value per leaf and that the values are uniform. This
    is splitmix64 over the instance index, which has both properties.
    """
    x = (np.arange(count, dtype=np.uint64) + np.uint64(seed) + np.uint64(0x9E3779B97F4A7C15))
    x = x ^ (x >> np.uint64(30))
    x = (x * np.uint64(0xBF58476D1CE4E5B9)) & np.uint64(0xFFFFFFFFFFFFFFFF)
    x = x ^ (x >> np.uint64(27))
    x = (x * np.uint64(0x94D049BB133111EB)) & np.uint64(0xFFFFFFFFFFFFFFFF)
    x = x ^ (x >> np.uint64(31))
    return (x >> np.uint64(11)).astype(np.float64) / float(1 << 53)


def value_noise(points: np.ndarray, scale: float, seed: int, octaves: int = 4) -> np.ndarray:
    """Fractal value noise in [0, 1], trilinearly interpolated, smoothstep-faded.

    Not Blender's Perlin. Used only where the .blend thresholds a noise field to a mask, and the
    threshold is always recalibrated to the coverage Blender's produces, so what carries over is
    the spatial frequency and the fraction of surface covered -- which is what the picture is
    made of -- rather than the particular lumps.
    """
    total = np.zeros(len(points), dtype=np.float64)
    amplitude, weight, freq = 1.0, 0.0, scale
    for octave in range(octaves):
        p = points * freq
        base = np.floor(p)
        frac = p - base
        fade = frac * frac * (3.0 - 2.0 * frac)
        cell = base.astype(np.int64)
        acc = np.zeros(len(points), dtype=np.float64)
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    h = ((cell[:, 0] + dx) * np.int64(73856093)) ^ \
                        ((cell[:, 1] + dy) * np.int64(19349663)) ^ \
                        ((cell[:, 2] + dz) * np.int64(83492791)) ^ \
                        np.int64(seed + octave * 7919)
                    h = (h.astype(np.uint64) * np.uint64(0x2545F4914F6CDD1D)) & np.uint64(0xFFFFFFFFFFFFFFFF)
                    h = h ^ (h >> np.uint64(33))
                    v = (h >> np.uint64(11)).astype(np.float64) / float(1 << 53)
                    w = np.ones(len(points))
                    w *= fade[:, 0] if dx else (1.0 - fade[:, 0])
                    w *= fade[:, 1] if dy else (1.0 - fade[:, 1])
                    w *= fade[:, 2] if dz else (1.0 - fade[:, 2])
                    acc += v * w
        total += acc * amplitude
        weight += amplitude
        amplitude *= 0.5
        freq *= 2.0
    return total / weight


def ramp(stops: list[tuple[float, list[float]]], t: np.ndarray | float) -> np.ndarray:
    """Blender's linear Color Ramp: clamped at both ends, linear between stops."""
    t = np.atleast_1d(np.asarray(t, dtype=np.float64))
    out = np.empty((len(t), 3))
    positions = [s[0] for s in stops]
    colors = [np.asarray(s[1], dtype=np.float64) for s in stops]
    out[:] = colors[0]
    out[t >= positions[-1]] = colors[-1]
    for i in range(len(stops) - 1):
        lo, hi = positions[i], positions[i + 1]
        sel = (t >= lo) & (t < hi)
        if not sel.any():
            continue
        f = ((t[sel] - lo) / (hi - lo))[:, None] if hi > lo else 0.0
        out[sel] = colors[i] * (1.0 - f) + colors[i + 1] * f
    return out


def emissive_material(name: str, base: list[float], rough: float,
                      emissive: np.ndarray | None) -> dict:
    """A glTF material. Emission above 1.0 goes into KHR_materials_emissive_strength, which this
    engine's loader understands, rather than being clamped away."""
    mat = {
        "name": name,
        "doubleSided": True,
        "pbrMetallicRoughness": {
            "baseColorFactor": [round(float(c), 6) for c in base] + [1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": round(float(rough), 6),
        },
    }
    if emissive is not None and float(np.max(emissive)) > 1e-6:
        peak = float(np.max(emissive))
        if peak > 1.0:
            mat["emissiveFactor"] = [round(float(c / peak), 6) for c in emissive]
            mat["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": round(peak, 6)}}
        else:
            mat["emissiveFactor"] = [round(float(c), 6) for c in emissive]
    return mat


# ---------------------------------------------------------------------------------------------
# Bucketing: one entry per source mesh, producing (bucket index per triangle, material list)
# ---------------------------------------------------------------------------------------------

def bucket_leaves(mesh: int, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray,
                  bands: int, report: dict) -> tuple[np.ndarray, list[dict]]:
    stride = 18                                  # vertices per instanced leaf, verified by caller
    count = len(pos) // stride
    rnd = instance_random(count, seed=9001 + mesh)
    centroid_y = pos[:, 1].reshape(count, stride).mean(axis=1)
    f = np.clip((centroid_y - LEAF_HEIGHT_LO) / LEAF_HEIGHT_SPAN, 0.0, 1.0) + rnd * LEAF_RANDOM_TINT
    f = np.clip(f, 0.0, 1.0)
    bright = rnd > LEAF_RANDOM_CUT
    band = np.minimum((f * bands).astype(np.int64), bands - 1)
    inst_bucket = band * 2 + bright.astype(np.int64)
    tri_inst = tris[:, 0] // stride              # every triangle lies inside one instance block
    tri_bucket = inst_bucket[tri_inst]

    c0, c1 = (np.asarray(c, dtype=np.float64) for c in LEAF_RAMP[mesh])
    mats = []
    for b in range(bands):
        centre = (b + 0.5) / bands
        base = c0 * (1.0 - centre) + c1 * centre
        for is_bright in (0, 1):
            strength = LEAF_BRIGHT if is_bright else LEAF_DIM
            emissive = np.asarray(LEAF_EMISSIVE, dtype=np.float64) * strength
            label = "lit" if is_bright else "dim"
            mats.append(emissive_material(
                f"GLOWMERE | leaves {mesh} | band {b} {label}", base, LEAF_ROUGHNESS, emissive))
    report["leaves"] = report.get("leaves", [])
    report["leaves"].append({
        "mesh": mesh, "instances": int(count),
        "litInstances": int(bright.sum()),
        "litFraction": round(float(bright.mean()), 5),
        "heightBands": bands,
    })
    return tri_bucket, mats


def bucket_fungi(mesh: int, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray,
                 report: dict) -> tuple[np.ndarray, list[dict]]:
    # Blender reads Geometry.Normal, the shading normal; average the three vertex normals.
    ny = nrm[tris, 1].mean(axis=1)
    gill = ny < FUNGI_NORMAL_CUT
    emissive = np.asarray(FUNGI_EMISSIVE, dtype=np.float64) * FUNGI_STRENGTH
    mats = [
        emissive_material(f"GLOWMERE | lumens {mesh} | cap", FUNGI_CAP, FUNGI_ROUGHNESS, None),
        emissive_material(f"GLOWMERE | lumens {mesh} | gills", FUNGI_GILL, FUNGI_ROUGHNESS, emissive),
    ]
    report.setdefault("lumens", []).append({
        "mesh": mesh, "triangles": int(len(tris)),
        "gillFraction": round(float(gill.mean()), 5),
    })
    return gill.astype(np.int64), mats


def bucket_tracery(mesh: int, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray,
                   bands: int, report: dict) -> tuple[np.ndarray, list[dict]]:
    centre = pos[tris].mean(axis=1)
    noise = value_noise(centre, TRACERY_NOISE_SCALE, seed=4242)
    cut = float(np.quantile(noise, 1.0 - TRACERY_COVERAGE))
    lit = noise > cut
    y = centre[:, 1]
    lo, hi = float(y.min()), float(y.max())
    band = np.minimum(((y - lo) / max(hi - lo, 1e-6) * bands).astype(np.int64), bands - 1)
    tri_bucket = band * 2 + lit.astype(np.int64)

    mats = []
    for b in range(bands):
        y_mid = lo + (b + 0.5) / bands * (hi - lo)
        colour = ramp(TRACERY_RAMP, y_mid / TRACERY_RAMP_DIVISOR)[0]
        for is_lit in (0, 1):
            emissive = colour * TRACERY_STRENGTH if is_lit else None
            label = "lit" if is_lit else "dark"
            mats.append(emissive_material(
                f"GLOWMERE | tracery | band {b} {label}", TRACERY_BASE, TRACERY_ROUGHNESS, emissive))
    report.setdefault("tracery", []).append({
        "mesh": mesh, "triangles": int(len(tris)),
        "litFraction": round(float(lit.mean()), 5),
        "targetCoverage": TRACERY_COVERAGE,
        "heightBands": bands, "yRange": [round(lo, 3), round(hi, 3)],
    })
    return tri_bucket, mats


def _wood_bands(centre: np.ndarray, bands: int) -> tuple[np.ndarray, list[tuple[list[float], float]]]:
    noise = value_noise(centre, WOOD_NOISE_SCALE, seed=1717)
    band = np.minimum((noise * bands).astype(np.int64), bands - 1)
    spec = []
    for b in range(bands):
        n = (b + 0.5) / bands
        t = np.clip((n - WOOD_RAMP[0][0]) / (WOOD_RAMP[1][0] - WOOD_RAMP[0][0]), 0.0, 1.0)
        c0 = np.asarray(WOOD_RAMP[0][1], dtype=np.float64)
        c1 = np.asarray(WOOD_RAMP[1][1], dtype=np.float64)
        spec.append(((c0 * (1.0 - t) + c1 * t).tolist(), WOOD_ROUGH_BASE + WOOD_ROUGH_GAIN * n))
    return band, spec


def bucket_wood(mesh: int, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray,
                bands: int, report: dict) -> tuple[np.ndarray, list[dict]]:
    centre = pos[tris].mean(axis=1)
    band, spec = _wood_bands(centre, bands)
    mats = [emissive_material(f"GLOWMERE | wood | band {b}", colour, rough, None)
            for b, (colour, rough) in enumerate(spec)]
    report.setdefault("wood", []).append({"mesh": mesh, "triangles": int(len(tris)), "bands": bands})
    return band, mats


def bucket_twigs(mesh: int, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray,
                 bands: int, report: dict) -> tuple[np.ndarray, list[dict]]:
    centre = pos[tris].mean(axis=1)
    band, spec = _wood_bands(centre, bands)
    glow_noise = value_noise(centre, WOOD_NOISE_SCALE * 3.0, seed=2323)
    cut = float(np.quantile(glow_noise, 1.0 - TWIG_GLOW_COVERAGE))
    glow = glow_noise > cut
    tri_bucket = np.where(glow, bands, band)
    mats = [emissive_material(f"GLOWMERE | twigs | band {b}", colour, rough, None)
            for b, (colour, rough) in enumerate(spec)]
    emissive = np.asarray(TWIG_GLOW_EMISSIVE, dtype=np.float64) * TWIG_GLOW_STRENGTH
    mats.append(emissive_material("GLOWMERE | twigs | sapwood glow",
                                  spec[bands // 2][0], spec[bands // 2][1], emissive))
    report.setdefault("twigs", []).append({
        "mesh": mesh, "triangles": int(len(tris)),
        "glowFraction": round(float(glow.mean()), 5),
        "targetCoverage": TWIG_GLOW_COVERAGE, "bands": bands,
    })
    return tri_bucket, mats


# ---------------------------------------------------------------------------------------------

def build_layer(name: str, meshes: list[int], src: dict, binary: bytes,
                bucketer, bands: int, report: dict) -> tuple[dict, bytes, dict]:
    out_bin = bytearray()
    views: list[dict] = []
    accessors: list[dict] = []
    out_meshes: list[dict] = []
    out_nodes: list[dict] = []
    materials: list[dict] = []
    checks: list[dict] = []

    def add_view(payload: bytes, target: int | None) -> int:
        while len(out_bin) % 4:
            out_bin.append(0)
        offset = len(out_bin)
        out_bin.extend(payload)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        views.append(view)
        return len(views) - 1

    for mesh_index in meshes:
        prim = src["meshes"][mesh_index]["primitives"][0]
        pos_acc = prim["attributes"]["POSITION"]
        nrm_acc = prim["attributes"]["NORMAL"]
        pos = accessor_array(src, binary, pos_acc).astype(np.float64)
        nrm = accessor_array(src, binary, nrm_acc).astype(np.float64)
        tris = accessor_array(src, binary, prim["indices"]).reshape(-1, 3).astype(np.int64)

        # Vertex data: the exact source bytes, and a hash of them, carried through untouched.
        for attr, acc_index in (("POSITION", pos_acc), ("NORMAL", nrm_acc)):
            payload = accessor_bytes(src, binary, acc_index)
            digest = hashlib.sha256(payload).hexdigest()
            view = add_view(payload, 34962)
            source = src["accessors"][acc_index]
            entry = {"bufferView": view, "componentType": source["componentType"],
                     "count": source["count"], "type": source["type"]}
            if "min" in source:
                entry["min"], entry["max"] = source["min"], source["max"]
            accessors.append(entry)
            checks.append({"mesh": mesh_index, "attribute": attr, "bytes": len(payload), "sha256": digest})
        pos_out, nrm_out = len(accessors) - 2, len(accessors) - 1

        tri_bucket, mats = bucketer(mesh_index, pos, nrm, tris, bands, report) if bands else \
            bucketer(mesh_index, pos, nrm, tris, report)
        material_base = len(materials)
        materials.extend(mats)

        idx_dtype = np.uint16 if len(pos) <= 65535 else np.uint32
        idx_component = 5123 if idx_dtype is np.uint16 else 5125
        primitives = []
        emitted = 0
        for bucket in range(len(mats)):
            sel = tri_bucket == bucket
            n = int(sel.sum())
            if n == 0:
                continue
            emitted += n
            payload = tris[sel].astype(idx_dtype).tobytes()
            view = add_view(payload, 34963)
            accessors.append({"bufferView": view, "componentType": idx_component,
                              "count": n * 3, "type": "SCALAR"})
            primitives.append({"attributes": {"POSITION": pos_out, "NORMAL": nrm_out},
                               "indices": len(accessors) - 1,
                               "material": material_base + bucket})
        if emitted != len(tris):
            raise SystemExit(f"mesh {mesh_index}: bucketing lost {len(tris) - emitted} triangles")

        out_meshes.append({"name": src["meshes"][mesh_index]["name"], "primitives": primitives})
        node_name = next((n["name"] for n in src["nodes"] if n.get("mesh") == mesh_index),
                         f"mesh-{mesh_index}")
        out_nodes.append({"name": node_name, "mesh": len(out_meshes) - 1})

    doc = {
        "asset": {"version": "2.0",
                  "generator": f"tools/glowmere_tree_layers.py -- Glowmere layer '{name}'"},
        "extensionsUsed": ["KHR_materials_emissive_strength"],
        "scene": 0,
        "scenes": [{"name": f"Glowmere {name}", "nodes": list(range(len(out_nodes)))}],
        "nodes": out_nodes,
        "meshes": out_meshes,
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(out_bin)}],
    }
    return doc, bytes(out_bin), {"vertexChecks": checks,
                                 "materials": [m["name"] for m in materials],
                                 "primitives": sum(len(m["primitives"]) for m in out_meshes)}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="source", required=True, type=Path)
    ap.add_argument("--out", dest="out", required=True, type=Path)
    ap.add_argument("--prefix", default="tree-glowmere")
    ap.add_argument("--leaf-bands", type=int, default=6)
    ap.add_argument("--tracery-bands", type=int, default=4)
    ap.add_argument("--wood-bands", type=int, default=4)
    ap.add_argument("--leaf-dim", type=float, default=None,
                    help="override the unlit leaves' emission floor (Blender's is 0.035). Lower "
                         "values sharpen the 6%% speckle against the 94%% wash; this exists so the "
                         "owner can compare rather than be told.")
    ap.add_argument("--report", type=Path, default=None)
    args = ap.parse_args(argv)

    if args.leaf_dim is not None:
        global LEAF_DIM
        LEAF_DIM = args.leaf_dim
    src, binary = read_glb(args.source)
    if len(src["meshes"]) != 7 or len(src["materials"]) != 7:
        raise SystemExit(f"{args.source}: expected the 7-mesh hero export, found "
                         f"{len(src['meshes'])} meshes / {len(src['materials'])} materials")
    # The leaf split depends on the export's instance blocking. Prove it rather than assume it.
    for mesh_index in (3, 4):
        prim = src["meshes"][mesh_index]["primitives"][0]
        tris = accessor_array(src, binary, prim["indices"]).reshape(-1, 3).astype(np.int64)
        block = tris // 18
        if not np.all((block[:, 0] == block[:, 1]) & (block[:, 1] == block[:, 2])):
            raise SystemExit(f"mesh {mesh_index}: triangles cross the 18-vertex instance blocks; "
                             "the per-leaf random cannot be recovered this way")

    args.out.mkdir(parents=True, exist_ok=True)
    report: dict = {"source": str(args.source),
                    "sourceSha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                    "masks": {}, "layers": {}}
    masks: dict = {}
    bucketers = {
        "tracery": (bucket_tracery, args.tracery_bands),
        "twigs": (bucket_twigs, args.wood_bands),
        "foliage": (bucket_leaves, args.leaf_bands),
        "lumens": (bucket_fungi, 0),
        "wood": (bucket_wood, args.wood_bands),
    }
    for name, spec in LAYERS.items():
        bucketer, bands = bucketers[name]
        doc, blob, info = build_layer(name, spec["meshes"], src, binary, bucketer, bands, masks)
        path = args.out / f"{args.prefix}-{name}.glb"
        write_glb(path, doc, blob)
        # Read it back and re-check the vertex bytes against the source's hashes: the guarantee is
        # about the file on disk, not about an array in this process.
        back_doc, back_bin = read_glb(path)
        pos_nrm = [a for a in range(len(back_doc["accessors"]))
                   if back_doc["accessors"][a]["type"] == "VEC3"]
        digests = [hashlib.sha256(accessor_bytes(back_doc, back_bin, a)).hexdigest() for a in pos_nrm]
        expected = [c["sha256"] for c in info["vertexChecks"]]
        if digests != expected:
            raise SystemExit(f"{path}: vertex bytes moved on write -- refusing to keep the file")
        tri_total = sum(a["count"] // 3 for a in back_doc["accessors"] if a["type"] == "SCALAR")
        source_tris = sum(src["accessors"][src["meshes"][m]["primitives"][0]["indices"]]["count"] // 3
                          for m in spec["meshes"])
        if tri_total != source_tris:
            raise SystemExit(f"{path}: {tri_total} triangles out, {source_tris} in")
        report["layers"][name] = {
            "file": path.name, "role": spec["role"], "sourceMeshes": spec["meshes"],
            "triangles": tri_total, "materials": len(back_doc["materials"]),
            "primitives": info["primitives"], "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "materialNames": info["materials"],
            "vertexBytesVerified": True,
        }
        print(f"{path.name}: {tri_total:,} tris, {len(back_doc['materials'])} materials, "
              f"{info['primitives']} primitives, {path.stat().st_size / 1e6:.1f} MB")
    report["masks"] = masks
    if args.report:
        args.report.write_text(json.dumps(report, indent=1) + "\n")
        print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
