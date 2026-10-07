"""A minimal binary glTF (GLB) writer for generated organism meshes: one mesh, one primitive, positions, normals,
uvs and 32-bit indices, one plain material. No dependencies beyond numpy.

The organisms are generated geometry, owned by this repository (unlike the commercial packs under assets/), so the
GLBs are written next to the scene and are reproducible from `organisms.py`.
"""
from __future__ import annotations

import json
import struct

import numpy as np


def _pad(b: bytes, fill: bytes = b"\x00") -> bytes:
    return b + fill * ((4 - len(b) % 4) % 4)


def write_glb(path, positions, normals, uvs, indices, name="organism", color=(0.5, 0.5, 0.5)):
    pos = np.ascontiguousarray(positions, dtype=np.float32)
    nrm = np.ascontiguousarray(normals, dtype=np.float32)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-8)
    uv = np.ascontiguousarray(uvs, dtype=np.float32)
    idx = np.ascontiguousarray(indices, dtype=np.uint32).reshape(-1)
    assert len(pos) == len(nrm) == len(uv) and len(idx) % 3 == 0 and idx.max() < len(pos)

    blobs = [pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()]
    views, offset, binary = [], 0, b""
    for i, b in enumerate(blobs):
        b = _pad(b)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blobs[i]),
                      "target": 34963 if i == 3 else 34962})
        binary += b
        offset += len(b)
    lo, hi = pos.min(axis=0).tolist(), pos.max(axis=0).tolist()
    doc = {
        "asset": {"version": "2.0", "generator": "av-gen bioluminescent organisms.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": [*color, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(idx), "type": "SCALAR"},
        ],
    }
    js = _pad(json.dumps(doc, separators=(",", ":")).encode(), b" ")
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)
    return len(idx) // 3
