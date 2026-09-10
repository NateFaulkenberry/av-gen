#!/usr/bin/env python3
"""Fetch a curated set of CC0 assets from Poly Haven into the asset library.

Poly Haven is CC0, so these can be committed and redistributed without
attribution obligations -- which is why it is the first source. Every asset
records where it came from in its metadata so provenance survives.

    tools/fetch_polyhaven.py --list          # sizes only, downloads nothing
    tools/fetch_polyhaven.py --fetch         # download at the chosen resolution
    tools/fetch_polyhaven.py --hdri --fetch --hdri-resolution 4k   # the sky library only
"""
import argparse, json, os, sys, urllib.request

API = "https://api.polyhaven.com"
ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets")

# A small curated library: the brief asks for a few excellent assets, not hundreds.
# category -> (asset ids, natural scale hint in metres, placement note)
CURATED = {
    "terrain/rocks": ["rock_moss_set_01", "rock_moss_set_02", "boulder_01", "rock_07", "rock_09",
                      "namaqualand_boulder_03", "stone_01"],
    "nature/roots": ["root_cluster_01", "root_cluster_02", "single_root", "pine_roots"],
    "nature/trunks": ["dead_tree_trunk", "dead_tree_trunk_02", "tree_stump_01", "tree_stump_02"],
    "nature/plants": ["fern_02", "moss_01", "shrub_01", "shrub_02", "shrub_03", "shrub_04",
                      "pachira_aquatica_01", "calathea_orbifolia_01", "anthurium_botany_01",
                      "nettle_plant", "grass_medium_02", "bark_debris_01"],
    # The large end of the scale hierarchy: without something an order of magnitude bigger than a
    # boulder, every scene reads as a field of same-sized rocks.
    "terrain/formations": ["coastal_cliff_02", "coastal_cliff_04", "mountainside", "rock_face_01",
                           "namaqualand_cliff_02"],
}


# Sky library (ADR-049). An equirectangular HDRI is the sky, so these are chosen for what the sky
# itself does, not for what is on the ground: a moon with a real disc, stars that survive tone
# mapping, and Milky Way structure. `_puresky` variants have the photographed ground replaced by a
# synthetic gradient, which is what a scene with its own terrain wants.
CURATED_HDRIS = ["kloppenheim_02_puresky", "qwantani_moonrise_puresky"]


# The API rejects the default urllib user agent, so every request carries a real one.
UA = {"User-Agent": "av-gen/1.0 (asset fetcher; +https://github.com/)"}


def _open(url, timeout):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout)


def get_json(url):
    with _open(url, 60) as r:
        return json.load(r)


def gltf_entry(files, resolution):
    gltf = files.get("gltf", {})
    if resolution in gltf:
        return gltf[resolution]
    for fallback in ("2k", "1k", "4k"):
        if fallback in gltf:
            return gltf[fallback]
    return None


def download(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with _open(url, 300) as r, open(path, "wb") as f:
        f.write(r.read())


def fetch_hdris(ids, resolutions, do_fetch):
    """Download equirectangular .hdr skies and record their provenance.

    Resolution is a per-scene choice rather than a global one -- the sky is the only asset whose
    smallest acceptable size is decided by whether point-sized stars survive the tone map -- so the
    manifest lists every resolution fetched and the scene JSON names the file it wants.
    """
    out_dir = os.path.join(ROOT, "hdri")
    manifest = []
    total = 0
    for asset_id in ids:
        try:
            info = get_json(f"{API}/info/{asset_id}")
            files = get_json(f"{API}/files/{asset_id}")
        except Exception as exc:
            print(f"  !! {asset_id}: {exc}", file=sys.stderr)
            continue
        hdri = files.get("hdri", {})
        for resolution in resolutions:
            node = hdri.get(resolution, {}).get("hdr")
            if node is None:
                print(f"  !! {asset_id}: no .hdr at {resolution}", file=sys.stderr)
                continue
            name = os.path.basename(node["url"])
            total += node.get("size", 0)
            print(f"{asset_id:28s} {resolution:4s} {node.get('size', 0)/1e6:8.2f} MB  {name}")
            manifest.append({
                "id": asset_id, "resolution": resolution, "file": f"hdri/{name}",
                "source": "polyhaven", "license": "CC0",
                "url": f"https://polyhaven.com/a/{asset_id}",
                "authors": list((info or {}).get("authors", {}).keys()),
                "categories": (info or {}).get("categories", []),
                "tags": (info or {}).get("tags", []),
                "whitebalance": (info or {}).get("whitebalance"),
            })
            if do_fetch:
                download(node["url"], os.path.join(out_dir, name))
    print(f"\ntotal: {total/1e6:.1f} MB across {len(manifest)} sky files")
    if do_fetch:
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(out_dir, "manifest.json"), "w") as f:
            json.dump({"source": "polyhaven.com", "license": "CC0",
                       "note": "Fetched by tools/fetch_polyhaven.py --hdri. CC0: no attribution "
                               "required, recorded here anyway so provenance survives. The .hdr "
                               "files are gitignored; re-run the fetcher to restore them.",
                       "assets": manifest}, f, indent=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resolution", default="2k")
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--hdri", action="store_true", help="fetch the sky library instead of the meshes")
    ap.add_argument("--hdri-resolution", default="2k,4k,8k",
                    help="comma-separated resolutions to fetch for each sky")
    ap.add_argument("--hdri-id", action="append", default=[],
                    help="fetch this sky instead of the curated list (repeatable)")
    args = ap.parse_args()

    if args.hdri:
        fetch_hdris(args.hdri_id or CURATED_HDRIS,
                    [r.strip() for r in args.hdri_resolution.split(",") if r.strip()], args.fetch)
        return

    total = 0
    manifest = []
    for category, ids in CURATED.items():
        for asset_id in ids:
            try:
                info = get_json(f"{API}/info/{asset_id}")
                files = get_json(f"{API}/files/{asset_id}")
            except Exception as exc:  # a missing asset should not stop the rest
                print(f"  !! {asset_id}: {exc}", file=sys.stderr)
                continue
            entry = gltf_entry(files, args.resolution)
            if entry is None or "gltf" not in entry:
                print(f"  !! {asset_id}: no glTF at any resolution", file=sys.stderr)
                continue
            node = entry["gltf"]
            size = node.get("size", 0) + sum(v.get("size", 0) for v in node.get("include", {}).values())
            total += size
            out_dir = os.path.join(ROOT, category, asset_id)
            main_name = os.path.basename(node["url"])
            print(f"{asset_id:28s} {category:16s} {size/1e6:7.2f} MB")
            manifest.append({
                "id": asset_id, "category": category, "file": f"{category}/{asset_id}/{main_name}",
                "source": "polyhaven", "license": "CC0",
                "url": f"https://polyhaven.com/a/{asset_id}",
                "authors": list((info or {}).get("authors", {}).keys()),
                "resolution": args.resolution,
            })
            if not args.fetch:
                continue
            download(node["url"], os.path.join(out_dir, main_name))
            for rel, meta in node.get("include", {}).items():
                download(meta["url"], os.path.join(out_dir, rel))

    print(f"\ntotal: {total/1e6:.1f} MB across {len(manifest)} assets at {args.resolution}")
    if args.fetch:
        os.makedirs(ROOT, exist_ok=True)
        with open(os.path.join(ROOT, "manifest.json"), "w") as f:
            json.dump({"source": "polyhaven.com", "license": "CC0",
                       "note": "Fetched by tools/fetch_polyhaven.py. CC0: no attribution required, "
                               "recorded here anyway so provenance survives.",
                       "assets": manifest}, f, indent=1)


if __name__ == "__main__":
    main()
