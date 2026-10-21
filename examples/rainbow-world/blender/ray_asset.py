"""Rainbow World 2, gate 5: the sunburst's ray -- one quad (x -0.5..0.5, y 0..1: the sun end at 0, the tip at 1)
whose emissive and alpha textures fade toward its tip and its edges, written as a .glb by hand (numpy and zlib only:
it runs inside Blender, which has no imaging library). Blended, double-sided; the node's tint takes the look's sun
colour and its glow the synth's light."""
import json
import struct
import zlib

import numpy as np


def _png_rgba(img):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ray_texture(w=64, h=256, alpha_peak=0.32):
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    across = np.clip(1.0 - (2 * u - 1) ** 2, 0, 1) ** 2.5
    along = (1.0 - v) ** 1.3
    prof = along[:, None] * across[None, :]
    img = np.zeros((h, w, 4), np.uint8)
    img[..., 0:3] = (255 * prof[..., None]).astype(np.uint8)
    img[..., 3] = (255 * alpha_peak * prof).astype(np.uint8)
    return img[::-1]   # glTF's v runs down the image: v = 0 (the sun end) is the image's top row


def write_ray_glb(path, size=1000.0):
    """`size` metres a side: a node's scale is capped at 100, so the quad is built at the sunburst's own scale."""
    pos = np.array([[-0.5, 0, 0], [0.5, 0, 0], [0.5, 1, 0], [-0.5, 1, 0]], np.float32) * size
    nrm = np.array([[0, 0, 1]] * 4, np.float32)
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32)
    idx = np.array([0, 1, 2, 0, 2, 3], np.uint16)
    png = _png_rgba(ray_texture())
    blobs, views, off = [], [], 0
    for data, target in ((pos.tobytes(), 34962), (nrm.tobytes(), 34962), (uv.tobytes(), 34962),
                         (idx.tobytes(), 34963), (png, None)):
        pad = (-len(data)) % 4
        v_ = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v_["target"] = target
        views.append(v_)
        blobs.append(data + b"\x00" * pad)
        off += len(data) + pad
    gl = {"asset": {"version": "2.0", "generator": "rainbow-world ray_asset.py"},
          "buffers": [{"byteLength": off}], "bufferViews": views,
          "accessors": [
              {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-0.5 * size, 0, 0],
               "max": [0.5 * size, size, 0]},
              {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
              {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
              {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}],
          "images": [{"bufferView": 4, "mimeType": "image/png"}],
          "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
          "textures": [{"source": 0, "sampler": 0}],
          "materials": [{"name": "sunray", "alphaMode": "BLEND", "doubleSided": True,
                         "pbrMetallicRoughness": {"baseColorFactor": [0.0, 0.0, 0.0, 1.0],
                                                  "baseColorTexture": {"index": 0}, "metallicFactor": 0.0,
                                                  "roughnessFactor": 1.0},
                         "emissiveTexture": {"index": 0}, "emissiveFactor": [1.0, 1.0, 1.0]}],
          "meshes": [{"name": "ray", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                     "indices": 3, "material": 0}]}],
          "nodes": [{"mesh": 0, "name": "ray"}], "scenes": [{"nodes": [0]}], "scene": 0}
    js = json.dumps(gl, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    binb = b"".join(blobs)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binb), 0x004E4942) + binb
    with open(path, "wb") as f:
        f.write(out)
