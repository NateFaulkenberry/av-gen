#!/usr/bin/env python3
"""The application end to end: launch, play synthetic audio, drive MIDI, switch scenes, render, shut down.

    python3 tools/ci/e2e.py [--bin build/release] [--out <dir>] [--switches 4] [--frames 480]

Runs `avgen` twice, each under tools/gpu-lock.sh (a real GPU and a window are required: this is not a hosted-CI job):

  live      the editor, windowed: Hyperspace with a deterministic 120 BPM test track playing (tools/make_test_audio.py),
            the `midi` ui-script arm (two CoreMIDI virtual sources, a scripted performance, Learn), and
            `--open-sequence` switching between Hyperspace and Glitch Signal (both procedural: no private asset)
            every 90 frames, then a capture of the last frame and an orderly exit.
  headless  an offline render (`--render`) of 60 frames of Glitch Signal from 4 s (a seek) to a PNG sequence.

Each run is judged on its exit code and its log, and the captures on being non-black:
  exit 0; "GPU errors: 0"; no `[error]` line; no "unknown targets" (a project switch binding the last project's
  timeline); every switch done; the MIDI arm's mappings installed; resident memory after the last switch within
  25% of after the second (a leak across switches); a clean shutdown line.

Exit 0 when every check passes, 1 otherwise; results in <out>/e2e.json. What it does not cover is listed in
docs/testing.md ("End to end").
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCENES = ["Sonic Abstract - Glitch Signal", "Hyperspace"]


def png_mean(path):
    """Mean of the RGB channels of an 8-bit RGBA/RGB PNG, 0..255 (no third-party dependency)."""
    data = open(path, "rb").read()
    pos, idat, w, h, channels = 8, bytearray(), 0, 0, 4
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, colour = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4}.get(colour, 4)
        elif ctype == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    prev = bytearray(stride)
    total = 0
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        total += sum(line[i] for i in range(stride) if i % channels < 3)
        prev = line
    return total / (w * h * 3)


def run(name, args, out):
    log = os.path.join(out, name + ".log")
    with open(log, "w") as f:
        rc = subprocess.run([os.path.join(ROOT, "tools", "gpu-lock.sh")] + args, cwd=ROOT, stdout=f,
                            stderr=subprocess.STDOUT).returncode
    return rc, open(log, errors="replace").read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "release"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "e2e"))
    ap.add_argument("--switches", type=int, default=4)
    ap.add_argument("--frames", type=int, default=480)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    avgen = os.path.join(os.path.abspath(a.bin), "src", "avgen")
    if not os.access(avgen, os.X_OK):
        sys.exit("e2e: no %s (build the release preset first)" % avgen)
    track = os.path.join(out, "track.wav")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_test_audio.py"), track], check=True,
                   stdout=subprocess.DEVNULL)
    checks = []

    def check(what, ok, detail=""):
        checks.append({"check": what, "ok": bool(ok), "detail": detail})
        print("%s  %s%s" % ("ok  " if ok else "FAIL", what, (": " + detail) if detail else ""), flush=True)

    # ---- live ----
    seq = ";".join("example:" + SCENES[i % 2] for i in range(a.switches))
    cap = os.path.join(out, "live-last.png")
    rc, log = run("live", [avgen, "--example", "Hyperspace", "--audio", track, "--play", "--size", "1280x720",
                           "--ui-script", "midi", "--open-sequence", seq, "--open-every", "90",
                           "--frames", str(a.frames), "--capture", cap], out)
    check("live: exit 0", rc == 0, "exit %d" % rc)
    check("live: no GPU errors", "GPU errors: 0" in log)
    errors = [l for l in log.splitlines() if "[error]" in l]
    check("live: no error lines", not errors, errors[0][:160] if errors else "")
    stale = [l for l in log.splitlines() if "unknown targets" in l]
    check("live: no stale timeline across switches", not stale, stale[0][:160] if stale else "")
    done = re.findall(r"open-sequence: (\d+) of (\d+) after .*resident ([\d.]+) MB", log)
    check("live: every switch done", len(done) == a.switches + 1 and done and done[-1][0] == done[-1][1],
          "%d of %d" % (len(done) - 1, a.switches))
    if len(done) >= 3:
        r2, rl = float(done[2][2]), float(done[-1][2])
        check("live: resident memory across switches within 25%", rl <= 1.25 * r2, "%.0f -> %.0f MB" % (r2, rl))
    check("live: MIDI arm installed its mappings", "midi: mappings installed" in log)
    check("live: orderly shutdown", "shutdown:" in log)
    check("live: a non-black capture", os.path.exists(cap) and png_mean(cap) > 2.0,
          "mean %.1f" % png_mean(cap) if os.path.exists(cap) else "no file")

    # ---- headless: an offline render from 4 s (a seek), 60 frames ----
    frames = os.path.join(out, "headless-frames")
    rc, log = run("headless", [avgen, "--headless", "--example", SCENES[0], "--audio", track, "--render", frames,
                               "--range", "4:6", "--fps", "30", "--size", "640x360"], out)
    check("headless: exit 0", rc == 0, "exit %d" % rc)
    written = sorted(f for f in os.listdir(frames) if f.endswith(".png")) if os.path.isdir(frames) else []
    check("headless: 60 frames written", len(written) == 60, "%d" % len(written))
    check("headless: no GPU errors", "GPU errors: 0" in log)
    errors = [l for l in log.splitlines() if "[error]" in l]
    check("headless: no error lines", not errors, errors[0][:160] if errors else "")
    last = os.path.join(frames, written[-1]) if written else ""
    check("headless: a non-black last frame", bool(last) and png_mean(last) > 2.0,
          "mean %.1f" % png_mean(last) if last else "no file")
    check("headless: orderly shutdown", "shutdown:" in log)

    failed = [c for c in checks if not c["ok"]]
    with open(os.path.join(out, "e2e.json"), "w") as f:
        json.dump({"checks": checks, "failed": len(failed)}, f, indent=1)
    print("E2E: %s (%d of %d checks)" % ("FAIL" if failed else "PASS", len(checks) - len(failed), len(checks)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
