"""Pass 3 (environment art): renders the shared camera set in cameras.json, so both agents and every phase use the
same cameras (56- "Baselines", the regression guard).

  python3 examples/strange-attractor/pass3/render_cameras.py <out_dir> [--only id,id|prefix*] [--set regression|eval]
      [--size 1920x1080] [--supersample 2]

- A "journey" camera is the film itself at time t: the last frame of a 2 s run-up (t-2 .. t at 30 fps), so the
  portals, bursts and looks are exactly the film's.
- A "fixed" camera is one world scene seen from a still camera: the scene's portal, corruption and wire are taken out,
  one look is applied from t=0, and the project's camera and journey tracks are dropped (the exposure track stays).
  It writes examples/strange-attractor/tmp-camref*.json, which are never committed and are deleted after each render.
Every render goes through tools/gpu-lock.sh. Output: <out_dir>/<id>.png, and <out_dir>/<id>.log.
"""
import argparse, fnmatch, json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
E = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(E))
AVGEN = os.path.join(ROOT, "build/release/src/avgen")


def fixed_project(cam):
    s = json.load(open(os.path.join(E, cam["scene"])))
    for k in ("portal", "portals", "corruption", "meshWire"):
        s.pop(k, None)
    look = cam.get("look", "none")
    if "looks" in s:
        if look != "none":
            sel = [l for l in s["looks"] if l["name"] == look][0]
            sel["from"] = 0
            s["looks"] = [sel]
        else:
            s.pop("looks")
    if "attractorWorld" in s:
        s["attractorWorld"]["camera"] = "scene"
    s["camera"] = {"mode": 1, "position": cam["pos"], "target": cam["tgt"], "fov": cam["fov"], "orbitSpeed": 0.0}
    json.dump(s, open(os.path.join(E, "tmp-camref.scene.json"), "w"), indent=1)
    p = json.load(open(os.path.join(E, "strange-attractor.json")))
    p["assets"]["scene"]["path"] = "tmp-camref.scene.json"
    p["timeline"]["tracks"] = [t for t in p["timeline"]["tracks"]
                               if not (t["target"].startswith(("camera", "journey"))
                                       and not t["target"].startswith("camera/exposure"))]
    json.dump(p, open(os.path.join(E, "tmp-camref.json"), "w"), indent=1)
    return os.path.join(E, "tmp-camref.json")


def render(cam, out_dir, size, ss):
    t = float(cam["t"])
    fixed = cam["kind"] == "fixed"
    project = fixed_project(cam) if fixed else os.path.join(E, "strange-attractor.json")
    tmp = tempfile.mkdtemp(prefix="p3cam-")
    fps = 5 if fixed else 30   # a still camera needs no 30 fps run-up; the film's frames do (particles, trails)
    cmd = [os.path.join(ROOT, "tools/gpu-lock.sh"), AVGEN, "--headless", "--project", project, "--render", tmp,
           "--format", "png", "--range", f"{t - 2.0:.4f}:{t:.4f}", "--fps", str(fps), "--size", size,
           "--supersample", str(ss)]
    env = {**os.environ, "AVGEN_GPU_LOCK_TIMEOUT": os.environ.get("AVGEN_GPU_LOCK_TIMEOUT", "14400")}
    try:
        with open(os.path.join(out_dir, cam["id"] + ".log"), "w") as log:
            r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        frames = sorted(f for f in os.listdir(tmp) if f.endswith(".png"))
        if r.returncode != 0 or not frames:
            print(f"FAILED {cam['id']} (exit {r.returncode})", flush=True)
            return False
        shutil.copy(os.path.join(tmp, frames[-1]), os.path.join(out_dir, cam["id"] + ".png"))
        print(f"ok {cam['id']}", flush=True)
        return True
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        if fixed:
            for f in ("tmp-camref.json", "tmp-camref.scene.json"):
                try:
                    os.remove(os.path.join(E, f))
                except FileNotFoundError:
                    pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--only", default="")
    ap.add_argument("--set", default="", help="regression | eval (default: both)")
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--supersample", type=int, default=2)
    a = ap.parse_args()
    cams = json.load(open(os.path.join(HERE, "cameras.json")))["cameras"]
    if a.set:
        cams = [c for c in cams if c["set"] == a.set]
    if a.only:
        pats = a.only.split(",")
        cams = [c for c in cams if any(fnmatch.fnmatch(c["id"], p) for p in pats)]
    os.makedirs(a.out_dir, exist_ok=True)
    bad = [c["id"] for c in cams if not render(c, a.out_dir, a.size, a.supersample)]
    print("DONE" + (" with failures: " + ",".join(bad) if bad else ""), flush=True)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
