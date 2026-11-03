"""Hero-asset audit (night camp, brief 49 Phase 1.1): the numbers of a .glb without Blender.

    python3 -I examples/strange-attractor/hero/audit_glb.py <asset.glb> [...]  [--json out.json]

Per file: triangles and vertices per mesh, UV sets, tangents, skin joints, animation clips and lengths, and per material
the alpha mode, factors and which texture slots are bound (with each image's pixel size and format).
"""
import json
import struct
import sys

COMP = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def image_size(b):
    if b[:8] == b"\x89PNG\r\n\x1a\n":
        w, h = struct.unpack(">II", b[16:24])
        return "png", w, h
    if b[:2] == b"\xff\xd8":
        i = 2
        while i < len(b) - 9:
            if b[i] != 0xFF:
                i += 1
                continue
            m = b[i + 1]
            if m in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack(">HH", b[i + 5:i + 9])
                return "jpg", w, h
            i += 2 + struct.unpack(">H", b[i + 2:i + 4])[0]
    if b[:4] == b"RIFF" and b[8:12] == b"WEBP":
        return "webp", 0, 0
    return "?", 0, 0


def audit(path):
    b = open(path, "rb").read()
    n = struct.unpack("<I", b[12:16])[0]
    js = json.loads(b[20:20 + n])
    off = 20 + n
    binb = b""
    if off < len(b):
        ln = struct.unpack("<I", b[off:off + 4])[0]
        binb = b[off + 8:off + 8 + ln]
    acc = js.get("accessors", [])
    views = js.get("bufferViews", [])
    imgs = []
    for im in js.get("images", []):
        if "bufferView" in im:
            v = views[im["bufferView"]]
            data = binb[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]]
            imgs.append((im.get("name", ""),) + image_size(data) + (v["byteLength"],))
        else:
            imgs.append((im.get("uri", ""), "uri", 0, 0, 0))
    texsrc = [t.get("source") for t in js.get("textures", [])]

    def tex(ti):
        if ti is None:
            return None
        s = texsrc[ti["index"]]
        im = imgs[s] if s is not None and s < len(imgs) else None
        return f"{im[2]}x{im[3]} {im[1]}" + (f" uv{ti.get('texCoord', 0)}" if ti.get("texCoord") else "") if im else "?"

    meshes = []
    tot_t = tot_v = 0
    for m in js.get("meshes", []):
        t = v = 0
        attrs = set()
        mats = set()
        for p in m["primitives"]:
            pv = acc[p["attributes"]["POSITION"]]["count"]
            v += pv
            if "indices" in p:
                t += acc[p["indices"]]["count"] // 3
            else:
                t += pv // 3
            attrs |= set(p["attributes"].keys())
            mats.add(p.get("material"))
        meshes.append(dict(name=m.get("name"), tris=t, verts=v, prims=len(m["primitives"]),
                           attrs=sorted(attrs), targets=len(m["primitives"][0].get("targets", []))))
        tot_t += t
        tot_v += v
    mats = []
    for m in js.get("materials", []):
        pbr = m.get("pbrMetallicRoughness", {})
        mats.append(dict(name=m.get("name"), alpha=m.get("alphaMode", "OPAQUE"), double=m.get("doubleSided", False),
                         baseColor=tex(pbr.get("baseColorTexture")), baseFactor=pbr.get("baseColorFactor"),
                         metallicRoughness=tex(pbr.get("metallicRoughnessTexture")),
                         metallic=pbr.get("metallicFactor", 1.0), roughness=pbr.get("roughnessFactor", 1.0),
                         normal=tex(m.get("normalTexture")), occlusion=tex(m.get("occlusionTexture")),
                         emissive=tex(m.get("emissiveTexture")), emissiveFactor=m.get("emissiveFactor"),
                         ext=sorted(m.get("extensions", {}).keys())))
    anims = []
    for a in js.get("animations", []):
        dur = 0.0
        for s in a["samplers"]:
            mx = acc[s["input"]].get("max")
            if mx:
                dur = max(dur, mx[0])
        anims.append(dict(name=a.get("name"), channels=len(a["channels"]), seconds=round(dur, 3)))
    skins = [dict(name=s.get("name"), joints=len(s["joints"])) for s in js.get("skins", [])]
    return dict(file=path, bytes=len(b), tris=tot_t, verts=tot_v, meshes=meshes, materials=mats, images=imgs,
                skins=skins, animations=anims, nodes=len(js.get("nodes", [])),
                joint_names=[js["nodes"][j].get("name") for j in js["skins"][0]["joints"]] if js.get("skins") else [],
                extensionsUsed=js.get("extensionsUsed", []))


if __name__ == "__main__":
    args = sys.argv[1:]
    out = None
    if "--json" in args:
        i = args.index("--json")
        out = args[i + 1]
        del args[i:i + 2]
    res = [audit(p) for p in args]
    for r in res:
        print(f"== {r['file']}  {r['bytes'] / 1e6:.1f} MB  tris {r['tris']}  verts {r['verts']}  nodes {r['nodes']}")
        for m in r["meshes"]:
            print(f"   mesh {m['name']}: tris {m['tris']} verts {m['verts']} prims {m['prims']} attrs {','.join(m['attrs'])}"
                  + (f" morphs {m['targets']}" if m["targets"] else ""))
        for m in r["materials"]:
            print(f"   mat {m['name']}: {m['alpha']} base={m['baseColor']} {m['baseFactor'] or ''} mr={m['metallicRoughness']}"
                  f" (m{m['metallic']} r{m['roughness']}) nrm={m['normal']} ao={m['occlusion']} em={m['emissive']}"
                  f" {m['emissiveFactor'] or ''} {m['ext'] or ''}")
        for s in r["skins"]:
            print(f"   skin {s['name']}: {s['joints']} joints")
        for a in r["animations"]:
            print(f"   anim {a['name']}: {a['seconds']} s, {a['channels']} channels")
        if r["extensionsUsed"]:
            print("   extensions", r["extensionsUsed"])
    if out:
        json.dump(res, open(out, "w"), indent=1)
