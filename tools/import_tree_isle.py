#!/usr/bin/env python3
"""Import the Tree of Life hero export and the floating island into assets/treeisle/.

Run under Blender, which is the only thing here that reads FBX:

    /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python tools/import_tree_isle.py -- \
        --island ~/Desktop/Island \
        --tree ~/Desktop/tree_mdl/hero_pass \
        --out assets/treeisle

Sources are read and never written. The tree is copied byte for byte -- it is an authored hero
asset and this project presents it, it does not regenerate it. The island is the part that needs
work: it ships as a 3ds Max FBX with Unity URP `.mat` sidecars, and AV Gen reads glTF only.

Two things about the island conversion are not obvious.

**The Unity mask map is not a colour map.** Each material's `FloatingIsle_<name>_MaskMap.png`
packs four unrelated scalars: R metallic, G ambient occlusion, B unused detail, A *smoothness*.
glTF wants the opposite packing and the opposite sense of the last one -- R occlusion, G
*roughness*, B metallic -- so this script repacks every mask map into an ORM texture, taking
roughness as `1 - A`. Feeding a mask map in as a base colour, or forgetting the inversion, gives a
result that is wrong in a way that is easy to miss: everything reads slightly too shiny or slightly
too matte and nothing looks obviously broken.

**The island is a cottage, not a rock.** Nine objects: a rocky base and a grass cap, and then a
house, a roof, a chimney, a windmill, a fence, a staircase and a scattering of decor. The brief's
scene wants nothing competing with the Tree of Life, so this emits two GLBs from the same import --
`floating-isle.glb` with everything the artist built, and `floating-isle-bare.glb` with only the
landmass and its grass. Two exports rather than one edited source: the structures stay available,
and nothing is deleted from anything on the Desktop.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
from datetime import date
from pathlib import Path

import bpy  # noqa: E402  (only importable inside Blender)
import numpy as np  # noqa: E402  (bundled with Blender)

# The nine materials the FBX actually uses, with the two Unity URP scalars that are not identity.
# `Clouds.mat` sits in the source folder with no mesh assigned to it and no textures of its own, so
# there is nothing to import; it is a leftover, not part of the floating look.
#
# `_Smoothness` is the one that matters. In URP's Lit shader with `_METALLICSPECGLOSSMAP` set, the
# scalar still multiplies the map: `smoothness = maskMap.a * _Smoothness`. Every material here sets
# it to 0.5, so taking roughness as a plain `1 - a` would ship the island half as rough as Unity
# renders it. `_Metallic` is *not* multiplied in that path and is correctly ignored here.
MATERIALS = {
    "Chimney": {"smoothness": 0.5, "occlusion": 1.0},
    "Decor": {"smoothness": 0.5, "occlusion": 1.0},
    "Fence": {"smoothness": 0.5, "occlusion": 1.0},
    "Grass": {"smoothness": 0.5, "occlusion": 0.755},
    "House": {"smoothness": 0.5, "occlusion": 1.0},
    "Roof": {"smoothness": 0.5, "occlusion": 1.0},
    "Stairs": {"smoothness": 0.5, "occlusion": 1.0},
    "Stone": {"smoothness": 0.5, "occlusion": 1.0},
    "Windmill": {"smoothness": 0.5, "occlusion": 1.0},
}

# Unity `_EmissionColor` from House.mat, an HDR tint over FloatingIsle_House_Emissive.png. Kept so
# the cottage variant is the cottage the artist lit, rather than a version quietly improved.
HOUSE_EMISSION_TINT = (2.2679362, 4.867144, 0.58609587)

# The objects that make up the landmass itself: the rock and the grass cap over it. Everything else
# in the file is something somebody built on top -- and because the artist modelled each as its own
# object, dropping them is a selection, not surgery. No vertex of the landmass is shared with the
# cottage, so nothing tears.
BARE_OBJECTS = {"RockyBase", "GeoSphere001"}


def log(msg: str) -> None:
    print(f"[import_tree_isle] {msg}", flush=True)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def glb_stats(path: Path) -> dict:
    """Triangle, mesh and material counts read straight out of the GLB's JSON chunk."""
    with path.open("rb") as f:
        magic, _version, _length = struct.unpack("<III", f.read(12))
        if magic != 0x46546C67:
            raise SystemExit(f"{path} is not a GLB")
        chunk_len, _chunk_type = struct.unpack("<II", f.read(8))
        doc = json.loads(f.read(chunk_len))
    triangles = 0
    primitives = 0
    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            primitives += 1
            if "indices" in prim:
                triangles += doc["accessors"][prim["indices"]]["count"] // 3
            else:
                triangles += doc["accessors"][prim["attributes"]["POSITION"]]["count"] // 3
    # Bounds in *scene* space. An accessor's min/max is in its mesh's own space, and the island's
    # nodes each carry a ~0.01 scale from 3ds Max, so the raw extents overstate it a hundredfold.
    # Walk the node tree and transform the eight corners of every primitive's box.
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3

    def node_matrix(node: dict) -> np.ndarray:
        if "matrix" in node:
            return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        m = np.eye(4)
        if "scale" in node:
            m = np.diag([*node["scale"], 1.0]) @ m
        if "rotation" in node:
            x, y, z, w = node["rotation"]
            r = np.array([
                [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
                [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
                [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
                [0, 0, 0, 1],
            ])
            m = r @ m
        if "translation" in node:
            t = np.eye(4)
            t[:3, 3] = node["translation"]
            m = t @ m
        return m

    def walk(index: int, parent: np.ndarray) -> None:
        nonlocal lo, hi
        node = doc["nodes"][index]
        world = parent @ node_matrix(node)
        if "mesh" in node:
            for prim in doc["meshes"][node["mesh"]].get("primitives", []):
                acc = doc["accessors"][prim["attributes"]["POSITION"]]
                a, b = acc["min"], acc["max"]
                for cx in (a[0], b[0]):
                    for cy in (a[1], b[1]):
                        for cz in (a[2], b[2]):
                            p = world @ np.array([cx, cy, cz, 1.0])
                            for i in range(3):
                                lo[i] = min(lo[i], p[i])
                                hi[i] = max(hi[i], p[i])
        for child in node.get("children", []):
            walk(child, world)

    for root in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))
    return {
        "triangles": triangles,
        "meshes": len(doc.get("meshes", [])),
        "primitives": primitives,
        "materials": len(doc.get("materials", [])),
        "textures": len(doc.get("textures", [])),
        "images": len(doc.get("images", [])),
        "boundsMin": [round(v, 4) for v in lo],
        "boundsMax": [round(v, 4) for v in hi],
        "sizeApprox": [round(hi[i] - lo[i], 4) for i in range(3)],
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def load_image(path: Path, data: bool) -> bpy.types.Image:
    img = bpy.data.images.load(str(path), check_existing=False)
    # Set before anything reads `.pixels`: a colour-managed image hands back linearised floats, and
    # a mask map linearised is a mask map ruined.
    img.colorspace_settings.name = "Non-Color" if data else "sRGB"
    return img


def pack_orm(mask_path: Path, out_path: Path, smoothness: float, occlusion: float) -> Path:
    """Unity mask map (R metallic, G AO, B detail, A smoothness) -> glTF ORM (R AO, G rough, B metal)."""
    src = load_image(mask_path, data=True)
    w, h = src.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    src.pixels.foreach_get(buf)
    px = buf.reshape(-1, 4)
    orm = np.empty_like(px)
    # `_OcclusionStrength` folded into the channel rather than written as glTF's
    # occlusionTexture.strength, because Blender's exporter has no way to set that from a node.
    # The formula is glTF's own: `1 + strength * (ao - 1)`.
    orm[:, 0] = 1.0 + occlusion * (px[:, 1] - 1.0)
    orm[:, 1] = 1.0 - smoothness * px[:, 3]
    orm[:, 2] = px[:, 0]
    orm[:, 3] = 1.0
    dst = bpy.data.images.new(out_path.stem, w, h, alpha=False, float_buffer=False, is_data=True)
    dst.colorspace_settings.name = "Non-Color"
    dst.pixels.foreach_set(orm.reshape(-1))
    dst.file_format = "PNG"
    dst.filepath_raw = str(out_path)
    dst.save()
    bpy.data.images.remove(src)
    bpy.data.images.remove(dst)
    return out_path


def build_material(mat: bpy.types.Material, textures: Path, orm_dir: Path, name: str,
                   scalars: dict) -> None:
    """Rebuild an imported FBX material as the exact node shape Blender's glTF exporter understands."""
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (600, 0)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (300, 0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    base_tex = nt.nodes.new("ShaderNodeTexImage")
    base_tex.location = (-500, 300)
    base_tex.image = load_image(textures / f"FloatingIsle_{name}_BaseMap.png", data=False)
    nt.links.new(base_tex.outputs["Color"], bsdf.inputs["Base Color"])

    # Metallic and roughness must come out of one image through a Separate Color node for the
    # exporter to recognise the pair and emit a single `metallicRoughnessTexture`. Split them
    # across two Image Texture nodes and it silently bakes new ones instead.
    orm_path = pack_orm(textures / f"FloatingIsle_{name}_MaskMap.png", orm_dir / f"{name}_ORM.png",
                        scalars["smoothness"], scalars["occlusion"])
    orm_tex = nt.nodes.new("ShaderNodeTexImage")
    orm_tex.location = (-500, 0)
    orm_tex.image = load_image(orm_path, data=True)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    sep.location = (-200, 0)
    nt.links.new(orm_tex.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])

    normal_tex = nt.nodes.new("ShaderNodeTexImage")
    normal_tex.location = (-500, -320)
    normal_tex.image = load_image(textures / f"FloatingIsle_{name}_Normal.png", data=True)
    normal_map = nt.nodes.new("ShaderNodeNormalMap")
    normal_map.location = (-200, -320)
    nt.links.new(normal_tex.outputs["Color"], normal_map.inputs["Color"])
    nt.links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])

    # Occlusion only reaches a glTF through a node group the exporter looks for by name.
    group = bpy.data.node_groups.get("glTF Material Output")
    if group is None:
        group = bpy.data.node_groups.new("glTF Material Output", "ShaderNodeTree")
        group.interface.new_socket("Occlusion", in_out="INPUT", socket_type="NodeSocketFloat")
        group.nodes.new("NodeGroupInput")
    settings = nt.nodes.new("ShaderNodeGroup")
    settings.node_tree = group
    settings.location = (300, -400)
    nt.links.new(sep.outputs["Red"], settings.inputs["Occlusion"])

    emissive = textures / f"FloatingIsle_{name}_Emissive.png"
    if emissive.exists():
        em_tex = nt.nodes.new("ShaderNodeTexImage")
        em_tex.location = (-500, -640)
        em_tex.image = load_image(emissive, data=False)
        tint = nt.nodes.new("ShaderNodeMix")
        tint.data_type = "RGBA"
        tint.blend_type = "MULTIPLY"
        tint.location = (-200, -640)
        tint.inputs["Factor"].default_value = 1.0
        tint.inputs[6].default_value = (*[c / max(HOUSE_EMISSION_TINT) for c in HOUSE_EMISSION_TINT], 1.0)
        nt.links.new(em_tex.outputs["Color"], tint.inputs[7])
        nt.links.new(tint.outputs[2], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = max(HOUSE_EMISSION_TINT)


def export_glb(objects: list[str], path: Path) -> None:
    bpy.ops.object.select_all(action="DESELECT")
    for name in objects:
        bpy.data.objects[name].select_set(True)
    bpy.context.view_layer.objects.active = bpy.data.objects[objects[0]]
    bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_apply=True,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_cameras=False,
        export_lights=False,
        export_animations=False,
        export_skins=False,
        export_morph=False,
        export_extras=False,
    )
    log(f"wrote {path} ({path.stat().st_size / 1e6:.1f} MB)")


def convert_island(island_dir: Path, out_dir: Path) -> dict:
    fbx = island_dir / "FloatingIsleModel.fbx"
    textures = island_dir / "FloatingIsleTextures"
    for p in (fbx, textures):
        if not p.exists():
            raise SystemExit(f"missing island source: {p}")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(fbx))

    present = {o.name for o in bpy.data.objects if o.type == "MESH"}
    missing = BARE_OBJECTS - present
    if missing:
        raise SystemExit(f"island source no longer contains {sorted(missing)}; update BARE_OBJECTS")

    orm_dir = Path(tempfile.mkdtemp(prefix="isle-orm-"))
    for name, scalars in MATERIALS.items():
        mat = bpy.data.materials.get(name)
        if mat is None:
            raise SystemExit(f"island source no longer has material '{name}'")
        build_material(mat, textures, orm_dir, name, scalars)
        log(f"material {name}: BaseMap + repacked ORM + Normal"
            + (" + Emissive" if (textures / f"FloatingIsle_{name}_Emissive.png").exists() else ""))

    out_dir.mkdir(parents=True, exist_ok=True)
    full = out_dir / "floating-isle.glb"
    bare = out_dir / "floating-isle-bare.glb"
    export_glb(sorted(present), full)
    export_glb(sorted(BARE_OBJECTS), bare)
    shutil.rmtree(orm_dir, ignore_errors=True)

    return {
        "full": glb_stats(full),
        "bare": glb_stats(bare),
        "objects": sorted(present),
        "bareObjects": sorted(BARE_OBJECTS),
        "suppressed": sorted(present - BARE_OBJECTS),
    }


def copy_tree(tree_dir: Path, out_dir: Path) -> dict:
    src = tree_dir / "Tree_of_Life_Hero_static.glb"
    if not src.exists():
        raise SystemExit(f"missing tree source: {src}")
    out_dir.mkdir(parents=True, exist_ok=True)
    dst = out_dir / "tree-of-life-hero.glb"
    shutil.copyfile(src, dst)
    log(f"copied {src.name} -> {dst} ({dst.stat().st_size / 1e6:.1f} MB, unmodified)")
    stats = glb_stats(dst)
    validation = tree_dir / "validation.json"
    if validation.exists():
        source = json.loads(validation.read_text())
        if source.get("static_export", {}).get("triangles") != stats["triangles"]:
            raise SystemExit("copied tree triangle count disagrees with the source validation.json")
    return stats


def glowmere_tree(hero: Path) -> dict:
    """The Glowmere material variant, via the sibling tool that owns the palette."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import glowmere_tree_materials as gtm  # noqa: E402  (deliberately late; stdlib only)

    variant = hero.with_name("tree-of-life-hero-glowmere.glb")
    gtm.main(["--in", str(hero), "--out", str(variant)])
    stats = glb_stats(variant)
    if stats["triangles"] != glb_stats(hero)["triangles"]:
        raise SystemExit("the Glowmere variant's triangle count moved; it must not")
    return stats


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="import_tree_isle")
    ap.add_argument("--island", default="~/Desktop/Island")
    ap.add_argument("--tree", default="~/Desktop/tree_mdl/hero_pass")
    ap.add_argument("--out", default="assets/treeisle")
    ap.add_argument("--manifest", default=None, help="default: <out>/../treeisle.manifest.json")
    args = ap.parse_args(argv)

    island_dir = Path(os.path.expanduser(args.island)).resolve()
    tree_dir = Path(os.path.expanduser(args.tree)).resolve()
    out_dir = Path(os.path.expanduser(args.out)).resolve()
    manifest_path = Path(args.manifest).resolve() if args.manifest else out_dir.parent / "treeisle.manifest.json"

    tree = copy_tree(tree_dir, out_dir)
    glowmere = glowmere_tree(out_dir / "tree-of-life-hero.glb")
    island = convert_island(island_dir, out_dir)

    manifest = {
        "source": (
            "Two externally authored hero assets, imported 2026-09-18 by "
            "tools/import_tree_isle.py for the tree-of-life-floating-island project (ADR-338)."
        ),
        "license": (
            "Not recorded for either asset. The Tree of Life was generated for this project by the "
            "owner's Blender pipeline; the floating island was supplied by the owner and carries no "
            "licence text. Fill this in before either is distributed outside the project."
        ),
        "regenerate": (
            "/Applications/Blender.app/Contents/MacOS/Blender --background --python "
            "tools/import_tree_isle.py -- --island ~/Desktop/Island --tree ~/Desktop/tree_mdl/hero_pass "
            "--out assets/treeisle   (it calls tools/glowmere_tree_materials.py for the tree variant)"
        ),
        "importedOn": str(date.today()),
        "assets": [
            {
                "name": "tree-of-life-hero",
                "file": "treeisle/tree-of-life-hero.glb",
                "sourcePath": str(tree_dir / "Tree_of_Life_Hero_static.glb"),
                "sourceFile": "Tree_of_Life_Hero_static.glb",
                "sourceFormat": "glb",
                "importedFormat": "glb",
                "conversion": "none -- copied byte for byte",
                "upAxis": "Y (glTF; the source .blend is Z-up and the Blender exporter converted it)",
                "note": (
                    "The static hero snapshot, 3.16 M triangles across seven meshes and seven "
                    "materials, with no textures and no UVs: every surface is a flat PBR factor. "
                    "The source's own validation.json calls it a 'Mature static snapshot; simple "
                    "PBR, no shader baking or growth export', and it means it -- the Glowmere look "
                    "in hero_pass/renders/Glowmere.png is a Blender shading setup that did not "
                    "survive this export. Only one of the seven materials carries any emission at "
                    "all. The scene restores the palette with material overrides; see "
                    "docs/decisions/ADR-338."
                ),
                **tree,
            },
            {
                "name": "tree-of-life-hero-glowmere",
                "file": "treeisle/tree-of-life-hero-glowmere.glb",
                "sourcePath": str(out_dir / "tree-of-life-hero.glb"),
                "sourceFile": "tree-of-life-hero.glb",
                "sourceFormat": "glb",
                "importedFormat": "glb",
                "conversion": (
                    "tools/glowmere_tree_materials.py rewrites the GLB's seven material "
                    "definitions to the Glowmere palette read off hero_pass/renders/Glowmere.png, "
                    "and copies the binary chunk through unchanged -- it verifies the copy's "
                    "SHA-256 against the hero's and refuses to write if it moved. The vertex data "
                    "of this file is not merely equivalent to the hero's, it is the same bytes."
                ),
                "upAxis": "Y",
                "note": (
                    "The variant the scene actually loads. A `material` block on a `kind: \"gltf\"` "
                    "node is parsed and then dropped with a warning, so a scene cannot restore a "
                    "palette the export dropped; the only levers it has on a glTF node are "
                    "`emissiveBoost` and `roughnessScale`, and boosting emission on leaves that "
                    "emit nothing multiplies zero. Brief §15's 'minimum required conversion'."
                ),
                **glowmere,
            },
            {
                "name": "floating-isle",
                "file": "treeisle/floating-isle.glb",
                "sourcePath": str(island_dir / "FloatingIsleModel.fbx"),
                "sourceFile": "FloatingIsleModel.fbx",
                "sourceFormat": "fbx (Kaydara binary 7400, 3ds Max 2023, authored as FloatingHouse_v3.fbx)",
                "importedFormat": "glb",
                "conversion": (
                    "Blender 5.2.1 LTS FBX import -> glTF export, with all nine Unity URP materials "
                    "rebuilt from FloatingIsleTextures/: BaseMap as base colour, Normal as the normal "
                    "map, and the MaskMap repacked into a glTF ORM texture (occlusion from its G, "
                    "roughness from 1 - its A, metallic from its R). The .mat files themselves are "
                    "Unity YAML and are not importable; the mapping is written down in "
                    "tools/import_tree_isle.py."
                ),
                "upAxis": "Y (converted from the source's Z-up on export)",
                "note": (
                    "Everything the artist built: the landmass and grass plus a house, roof, "
                    "chimney, windmill, fence, staircase and decor. The house's windows are "
                    "emissive, tinted by Unity's HDR _EmissionColor (2.27, 4.87, 0.59)."
                ),
                **island["full"],
                "objects": island["objects"],
            },
            {
                "name": "floating-isle-bare",
                "file": "treeisle/floating-isle-bare.glb",
                "sourcePath": str(island_dir / "FloatingIsleModel.fbx"),
                "sourceFile": "FloatingIsleModel.fbx",
                "sourceFormat": "fbx",
                "importedFormat": "glb",
                "conversion": (
                    "The same import, exported with only the landmass objects selected. A second "
                    "export, not an edit: nothing is removed from the FBX, and floating-isle.glb "
                    "still carries every object."
                ),
                "upAxis": "Y",
                "note": (
                    "The island with nobody living on it. Kept because the built structures compete "
                    "with the Tree of Life for the frame -- at the scene's island scale the cottage "
                    "is taller than the tree. See docs/decisions/ADR-338."
                ),
                **island["bare"],
                "objects": island["bareObjects"],
                "suppressed": island["suppressed"],
            },
        ],
    }
    manifest_path.write_text(json.dumps(manifest, indent=1) + "\n")
    log(f"wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
