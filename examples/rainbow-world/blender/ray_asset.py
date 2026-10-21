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


def write_burst_glb(path, rays):
    """The whole sunburst as one mesh (one entity: a moving node costs per node, and 22 of them moving every frame
    spiked the frame time). `rays`: (base point, along unit, across unit, facing unit, length, width), metres, in
    the node's own frame (its origin the camera)."""
    pos, nrm, uv, idx = [], [], [], []
    for k, (b, al, ac, fc, ln, wd) in enumerate(rays):
        b, al, ac, fc = (np.asarray(x, np.float64) for x in (b, al, ac, fc))
        for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1)):
            pos.append(b + ac * (cu - 0.5) * wd + al * cv * ln)
            nrm.append(fc)
            uv.append((cu, cv))
        o = 4 * k
        idx += [o, o + 1, o + 2, o, o + 2, o + 3]
    pos = np.array(pos, np.float32); nrm = np.array(nrm, np.float32); uv = np.array(uv, np.float32)
    idx = np.array(idx, np.uint16)
    _write(path, pos, nrm, uv, idx)


def glare_texture(n=512, seed=5, streaks=34):
    """Gate 7 (the owner: "the sun rays look weird ... more realistic"): the sun's glare as the air scatters it, not
    spokes -- a forward-scattering halo (a tight core and a broad aureole, both exponential in the distance from the
    sun) and, inside it, many faint soft streaks of uneven width and strength that start AT the sun and die with
    the halo (no gap, no hard edges, no countable spokes), all to nothing at the quad's rim. Alpha and emission alike
    (the node's tint colours it, its glow lane brightens it)."""
    rng = np.random.default_rng(seed)
    c = (np.arange(n) + 0.5) / n * 2 - 1
    x, y = np.meshgrid(c, c)
    r = np.hypot(x, y)
    th = np.arctan2(y, x)
    halo = 0.8 * np.exp(-r / 0.08) + 0.55 * np.exp(-r / 0.28) + 0.3 * np.exp(-r / 0.6)
    ang = np.zeros_like(r)
    for _ in range(streaks):
        t0, w, a = rng.uniform(-np.pi, np.pi), rng.uniform(0.05, 0.16), rng.uniform(0.3, 1.0)
        d = np.angle(np.exp(1j * (th - t0)))
        ang += a * np.exp(-(d / w) ** 2)
    ang = (ang - ang.min()) / max(ang.max() - ang.min(), 1e-6)
    reach = rng.uniform(0.55, 1.0, streaks)   # noqa: F841 (kept: the draw order fixes the texture)
    rays = ang * np.clip(1 - r, 0, 1) ** 1.4 * (1 - np.exp(-r / 0.2))
    prof = (halo * (1.0 + 0.4 * rays) + 0.3 * rays) * np.clip((1 - r) / 0.25, 0, 1) ** 2
    prof = np.clip(prof, 0, 1)
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 0:3] = (255 * prof[..., None]).astype(np.uint8)
    img[..., 3] = (255 * 0.8 * prof).astype(np.uint8)
    return img


def write_glare_glb(path, centre, facing, size):
    """One quad `size` metres a side at `centre`, facing `facing` (unit), carrying the glare texture."""
    centre, facing = np.asarray(centre, np.float64), np.asarray(facing, np.float64)
    e1 = np.cross(facing, [0.0, 1.0, 0.0] if abs(facing[1]) < 0.9 else [1.0, 0.0, 0.0]); e1 /= np.linalg.norm(e1)
    e2 = np.cross(facing, e1)
    pos = np.array([centre + (cu - 0.5) * size * e1 + (cv - 0.5) * size * e2
                    for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1))], np.float32)
    nrm = np.array([facing] * 4, np.float32)
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32)
    idx = np.array([0, 1, 2, 0, 2, 3], np.uint16)
    _write(path, pos, nrm, uv, idx, _png_rgba(glare_texture()))


def write_ray_glb(path, size=1000.0):
    """One quad, `size` metres a side."""
    pos = np.array([[-0.5, 0, 0], [0.5, 0, 0], [0.5, 1, 0], [-0.5, 1, 0]], np.float32) * size
    nrm = np.array([[0, 0, 1]] * 4, np.float32)
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], np.float32)
    idx = np.array([0, 1, 2, 0, 2, 3], np.uint16)
    _write(path, pos, nrm, uv, idx)


def _write(path, pos, nrm, uv, idx, png=None):
    png = png if png is not None else _png_rgba(ray_texture())
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
              {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
               "min": [float(v) for v in pos.min(axis=0)], "max": [float(v) for v in pos.max(axis=0)]},
              {"bufferView": 1, "componentType": 5126, "count": len(pos), "type": "VEC3"},
              {"bufferView": 2, "componentType": 5126, "count": len(pos), "type": "VEC2"},
              {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"}],
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
