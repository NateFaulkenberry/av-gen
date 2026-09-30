#!/usr/bin/env python3
"""Sonic Garden review media (docs/prototypes/sonic-garden): renders the variants and assembles the §34-36 videos.

    python3 tools/sonic_garden_review.py render --out <dir> [--size 1920x1080] [--stills-at 17]
    python3 tools/sonic_garden_review.py assemble --out <dir> --review <dir>
    python3 tools/sonic_garden_review.py beforeafter --out <dir> --review <dir> --before <earlier review dir>

`render` writes low-cost-to-regenerate inputs into <dir>: the variants (from the master, via sonic_garden_variants.py),
one video per variant (under tools/gpu-lock.sh, one lock for the whole batch), one supersampled still per §34 sound,
and a --sonic-trace CSV per variant (CPU). `assemble` turns them into the review folder's files:

  34-same-midi-four-sounds.mp4                       the four sounds as a labelled, silent 2x2 grid
  34-same-midi-four-sounds-sequence-with-audio.mp4   the four one after another, with their audio
  34-still-<sound>.png, 34-stills-at-<t>s.png        the stills, and a labelled sheet of them
  35-musical-context.mp4, 35-musical-context-bell.mp4   §35, with section labels and a readout of notes.* mappings
  36-timbre-morph.mp4                                §36, with stage labels and a readout of the family weights

Needs ffmpeg and Pillow. This ffmpeg has no drawtext, so every label is a PIL image overlaid.
"""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile
import wave

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AVGEN = os.path.join(REPO, "build", "release", "src", "avgen")
FONT = "/System/Library/Fonts/Avenir Next.ttc"
SOUNDS = [("pad", "A  warm analog pad", "organic / living"),
          ("bell", "B  FM bell", "crystalline / synthetic"),
          ("bass", "C  distorted wavetable bass", "chaotic, heavy (tectonic)"),
          ("perc", "D  noisy percussion", "chaotic, light (impact)")]
VARIANTS = ["pad", "bell", "bass", "perc", "context-pad", "context-bell", "morph"]
AUDIO = {"pad": "sonic-pad", "bell": "sonic-bell", "bass": "sonic-bass", "perc": "sonic-perc",
         "context-pad": "sonic-context-pad", "context-bell": "sonic-context-bell", "morph": "sonic-morph"}
PHRASE = [(0.0, "bars 1-2: sustained chords"), (5.0, "bars 3-4: arpeggio"), (10.0, "bars 5-6: melody"),
          (15.0, "bars 7-8: dense stabs"), (18.75, "final chord")]
CONTEXT = [(0.0, "sustained notes"), (10.0, "rapid arpeggio"), (20.0, "dense chords")]
MORPH = [(0.0, "clean"), (3.0, "-> brighter"), (12.0, "-> more resonant"), (18.0, "-> more distorted"),
         (24.0, "-> noisier"), (30.0, "(tail)")]


def font(size, bold=False):
    return ImageFont.truetype(FONT, size, index=(2 if bold else 0))


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.stderr.write(p.stderr.decode()[-3000:])
        raise SystemExit("failed (%d): %s" % (p.returncode, " ".join(cmd[:6])))
    return p.stdout.decode()


def video_size(path):
    out = run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of",
               "csv=p=0", path]).strip()
    w, h = out.split(",")
    return int(w), int(h)


def seconds(v):
    with wave.open(os.path.join(REPO, "assets", "audio", AUDIO[v] + ".wav"), "rb") as w:
        return w.getnframes() / float(w.getframerate())


# ---- render ------------------------------------------------------------------------------------------------------

def render(args):
    out = os.path.abspath(args.out)
    proj = os.path.join(out, "proj")
    os.makedirs(proj, exist_ok=True)
    run(["python3", os.path.join(REPO, "tools", "sonic_garden_variants.py"), "--out", proj])
    for v in VARIANTS:  # the traces are CPU-only and feed the readouts
        run([AVGEN, "--project", os.path.join(proj, v + ".json"), "--sonic-trace", os.path.join(out, v + ".csv")])
    lines = ["#!/bin/bash", "set -e"]
    for v in VARIANTS:
        lines.append("%s --headless --project %s --render %s --range 0:%.3f --size %s --quality 92" % (
            AVGEN, os.path.join(proj, v + ".json"), os.path.join(out, v + ".mp4"), seconds(v), args.size))
    for v, _, _ in SOUNDS:
        lines.append("%s --headless --project %s --render %s --format png --range %s:%s --size %s --supersample 2" % (
            AVGEN, os.path.join(proj, v + ".json"), os.path.join(out, "still-" + v), args.stills_at, args.stills_at,
            args.size))
    script = os.path.join(out, "render.sh")
    with open(script, "w") as f:
        f.write("\n".join(lines) + "\n")
    os.chmod(script, 0o755)
    subprocess.run([os.path.join(REPO, "tools", "gpu-lock.sh"), script], check=True)
    with open(os.path.join(out, "stills-at.txt"), "w") as f:
        f.write(str(args.stills_at))


# ---- assemble ----------------------------------------------------------------------------------------------------

def label_png(path, w, h, text, sub=None, scale=1.0):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    fs = max(14, int(h * 0.036 * scale))
    f = font(fs, True)
    pad = int(fs * 0.55)
    tw = d.textlength(text, font=f)
    box_h = fs + pad * 2
    f2 = font(int(fs * 0.74))
    if sub:
        tw = max(tw, d.textlength(sub, font=f2))
        box_h += int(fs * 0.95)
    d.rectangle([0, 0, tw + pad * 2 + 4, box_h], fill=(0, 0, 0, 120))
    d.text((pad, pad - int(fs * 0.12)), text, font=f, fill=(240, 240, 240, 255))
    if sub:
        d.text((pad, pad + fs), sub, font=f2, fill=(205, 205, 205, 255))
    img.save(path)


def readout(trace, outdir, title, specs, w=1920, h=1080):
    """One transparent PNG per trace row: small labelled bars in the lower right corner."""
    os.makedirs(outdir, exist_ok=True)
    with open(trace) as f:
        r = csv.reader(f)
        head = next(r)
        rows = [[float(x) for x in row] for row in r]
    idx = [head.index(c) for c, _, _ in specs]
    fs = max(12, h // 48)
    f, fb = font(fs), font(fs, True)
    pad, barw, lab_w = fs // 2, w // 7, int(fs * 5.2)
    box_w = pad * 3 + lab_w + barw
    box_h = pad * 2 + int(fs * 1.5) * (len(specs) + 1)
    x0, y0 = w - box_w - fs, h - box_h - fs
    for n, row in enumerate(rows):
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rectangle([x0, y0, x0 + box_w, y0 + box_h], fill=(0, 0, 0, 110))
        d.text((x0 + pad, y0 + pad), title, font=fb, fill=(235, 235, 235, 255))
        for k, ((_, label, rgb), i) in enumerate(zip(specs, idx)):
            v = max(0.0, min(1.0, row[i]))
            yy = y0 + pad + int(fs * 1.5) * (k + 1)
            bx = x0 + pad * 2 + lab_w
            d.text((x0 + pad, yy), label, font=f, fill=(215, 215, 215, 255))
            d.rectangle([bx, yy + fs * 0.25, bx + barw, yy + fs * 0.95], outline=(120, 120, 120, 200))
            d.rectangle([bx, yy + fs * 0.25, bx + int(barw * v), yy + fs * 0.95], fill=rgb + (230,))
        im.save(os.path.join(outdir, "o%05d.png" % n))


def labelled(src, out, title, sections, overlay_dir=None):
    """A video with a title, a subtitle per section and an optional readout sequence; the audio is kept."""
    tmp = tempfile.mkdtemp()
    w, h = video_size(src)
    cmd, fc, chain, inputs = ["ffmpeg", "-y", "-i", src], [], "[0:v]", 1
    for k, (t0, sub) in enumerate(sections):
        t1 = sections[k + 1][0] if k + 1 < len(sections) else 1e9
        p = os.path.join(tmp, "l%d.png" % k)
        label_png(p, w, h, title, sub)
        cmd += ["-i", p]
        fc.append("%s[%d:v]overlay=0:0:enable='between(t,%g,%g)'[x%d]" % (chain, inputs, t0, t1 - 1e-3, k))
        chain, inputs = "[x%d]" % k, inputs + 1
    if overlay_dir:
        cmd += ["-framerate", "30", "-i", os.path.join(overlay_dir, "o%05d.png")]
        fc.append("%s[%d:v]overlay=0:0:eof_action=pass[ov]" % (chain, inputs))
        chain = "[ov]"
    cmd += ["-filter_complex", ";".join(fc), "-map", chain, "-map", "0:a?", "-c:v", "libx264", "-crf", "17",
            "-preset", "slow", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out]
    run(cmd)


def assemble(args):
    src, rev = os.path.abspath(args.out), os.path.abspath(args.review)
    os.makedirs(rev, exist_ok=True)
    tmp = tempfile.mkdtemp()
    # §34: the grid (silent) and the sequence (with audio)
    cmd = ["ffmpeg", "-y"]
    for v, _, _ in SOUNDS:
        cmd += ["-i", os.path.join(src, v + ".mp4")]
    for i, (_, t, s) in enumerate(SOUNDS):
        p = os.path.join(tmp, "g%d.png" % i)
        label_png(p, 960, 540, t, s, scale=1.25)
        cmd += ["-i", p]
    fc = ["[%d:v]scale=960:540:flags=lanczos[s%d];[s%d][%d:v]overlay=0:0[o%d]" % (i, i, i, 4 + i, i) for i in range(4)]
    fc.append("[o0][o1][o2][o3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0[v]")
    run(cmd + ["-filter_complex", ";".join(fc), "-map", "[v]", "-c:v", "libx264", "-crf", "17", "-preset", "slow",
               "-pix_fmt", "yuv420p", "-an", "-movflags", "+faststart",
               os.path.join(rev, "34-same-midi-four-sounds.mp4")])
    parts = []
    for v, t, s in SOUNDS:
        p = os.path.join(tmp, v + ".mp4")
        labelled(os.path.join(src, v + ".mp4"), p, t + "   (" + s + ")", PHRASE)
        parts.append(p)
    lst = os.path.join(tmp, "list.txt")
    with open(lst, "w") as f:
        f.writelines("file '%s'\n" % p for p in parts)
    run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", "-movflags", "+faststart",
         os.path.join(rev, "34-same-midi-four-sounds-sequence-with-audio.mp4")])
    # the stills and their sheet
    at = open(os.path.join(src, "stills-at.txt")).read().strip()
    sheet = Image.new("RGB", (1920, 1080))
    f1, f2 = font(30, True), font(22)
    for i, (v, t, s) in enumerate(SOUNDS):
        png = os.path.join(src, "still-" + v, "frame_000000.png")
        shutil.copy(png, os.path.join(rev, "34-still-%s.png" % v))
        im = Image.open(png).convert("RGB").resize((960, 540), Image.LANCZOS)
        d = ImageDraw.Draw(im, "RGBA")
        d.rectangle([0, 0, max(d.textlength(t, font=f1), d.textlength(s, font=f2)) + 28, 78], fill=(0, 0, 0, 130))
        d.text((14, 8), t, font=f1, fill=(240, 240, 240, 255))
        d.text((14, 44), s, font=f2, fill=(205, 205, 205, 255))
        sheet.paste(im, ((i % 2) * 960, (i // 2) * 540))
    sheet.save(os.path.join(rev, "34-stills-at-%ss.png" % at))
    # §35: one sound, three contexts, with the context layer read out (and the families holding still)
    ctx = [("visual.sustain", "sustain", (255, 190, 120)), ("visual.figure", "figure", (150, 230, 200)),
           ("visual.stack", "stack", (235, 140, 190)), ("visual.organic", "organic", (255, 150, 70)),
           ("visual.crystalline", "crystalline", (140, 210, 255))]
    for v, name, sound in (("context-pad", "35-musical-context.mp4", "warm pad"),
                           ("context-bell", "35-musical-context-bell.mp4", "FM bell")):
        od = os.path.join(tmp, v + "-readout")
        readout(os.path.join(src, v + ".csv"), od, "musical context (MIDI)", ctx,
                *video_size(os.path.join(src, v + ".mp4")))
        labelled(os.path.join(src, v + ".mp4"), os.path.join(rev, name), "One sound (%s), three contexts" % sound,
                 CONTEXT, od)
    # §36: the morph, with the family weights read out
    od = os.path.join(tmp, "morph-readout")
    readout(os.path.join(src, "morph.csv"), od, "visual families",
            [("visual.organic", "organic", (255, 150, 70)), ("visual.crystalline", "crystalline", (140, 210, 255)),
             ("visual.chaotic", "chaotic", (170, 110, 255)), ("visual.mass", "mass", (200, 200, 200))],
            *video_size(os.path.join(src, "morph.mp4")))
    labelled(os.path.join(src, "morph.mp4"), os.path.join(rev, "36-timbre-morph.mp4"),
             "One phrase, one synth, its sound morphing", MORPH, od)


def before_after(args):
    """An earlier pass against this one: the same phrase window per sound, the earlier render on the left and the new
    one on the right, with that sound's audio; and the stills paired, one row per sound.

    --before is an earlier review folder (its 34-same-midi-four-sounds.mp4 grid and 34-still-<sound>.png), --out the
    new render directory, --review the new review folder."""
    src, rev, old = os.path.abspath(args.out), os.path.abspath(args.review), os.path.abspath(args.before)
    os.makedirs(rev, exist_ok=True)
    tmp = tempfile.mkdtemp()
    t0, dur = (float(x) for x in args.window.split(":"))
    parts = []
    for i, (v, t, s) in enumerate(SOUNDS):
        # the earlier grid's quadrant i (960x540, its own label kept), and the new render scaled to match
        qx, qy = (i % 2) * 960, (i // 2) * 540
        left, right, title = (os.path.join(tmp, "%s%d.png" % (k, i)) for k in "lrt")
        for path, text in ((left, "before: " + args.before_name), (right, "after: " + args.after_name)):
            # drawn as a strip for the panel's foot, clear of the earlier grid's own label in its top-left corner
            label_png(path, 960, 540, text, None, scale=1.1)
            Image.open(path).crop((0, 0, 960, 44)).save(path)
        label_png(title, 1920, 1080, t, s, scale=1.3)
        out = os.path.join(tmp, "ba%d.mp4" % i)
        fc = ("[0:v]crop=960:540:%d:%d,setpts=PTS-STARTPTS[a];[1:v]scale=960:540:flags=lanczos,setpts=PTS-STARTPTS[b];"
              "[a][2:v]overlay=0:H-h[a2];[b][3:v]overlay=0:H-h[b2];[a2][b2]hstack=inputs=2[row];"
              "[row]pad=1920:1080:0:270:black[p];[p][4:v]overlay=0:0[v]" % (qx, qy))
        new = os.path.join(src, v + ".mp4")
        run(["ffmpeg", "-y", "-ss", str(t0), "-t", str(dur), "-i", os.path.join(old, "34-same-midi-four-sounds.mp4"),
             "-ss", str(t0), "-t", str(dur), "-i", new, "-i", left, "-i", right, "-i", title,
             "-filter_complex", fc, "-map", "[v]", "-map", "1:a?", "-c:v", "libx264", "-crf", "17", "-preset", "slow",
             "-pix_fmt", "yuv420p", "-r", "30", "-c:a", "aac", "-b:a", "192k", "-shortest", out])
        parts.append(out)
    lst = os.path.join(tmp, "list.txt")
    with open(lst, "w") as f:
        f.writelines("file '%s'\n" % p for p in parts)
    run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", "-movflags", "+faststart",
         os.path.join(rev, "before-after-with-audio.mp4")])
    # the stills, paired: one row per sound, the earlier pass left
    sheet = Image.new("RGB", (1920, 2160))
    f1 = font(26, True)
    for i, (v, t, s) in enumerate(SOUNDS):
        for j, (png, tag) in enumerate(((os.path.join(old, "34-still-%s.png" % v), args.before_name),
                                        (os.path.join(src, "still-" + v, "frame_000000.png"), args.after_name))):
            im = Image.open(png).convert("RGB").resize((960, 540), Image.LANCZOS)
            d = ImageDraw.Draw(im, "RGBA")
            text = "%s   (%s)" % (t, tag)
            d.rectangle([0, 0, d.textlength(text, font=f1) + 24, 44], fill=(0, 0, 0, 130))
            d.text((12, 6), text, font=f1, fill=(240, 240, 240, 255))
            sheet.paste(im, (j * 960, i * 540))
    sheet.save(os.path.join(rev, "before-after-stills.png"))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("render")
    r.add_argument("--out", required=True)
    r.add_argument("--size", default="1920x1080")
    r.add_argument("--stills-at", default="17")
    a = sub.add_parser("assemble")
    a.add_argument("--out", required=True, help="the render directory")
    a.add_argument("--review", required=True)
    b = sub.add_parser("beforeafter")
    b.add_argument("--out", required=True, help="the new render directory")
    b.add_argument("--review", required=True, help="the new review folder")
    b.add_argument("--before", required=True, help="the earlier review folder (its grid and stills)")
    b.add_argument("--window", default="12:6", help="start:seconds of the phrase to compare")
    b.add_argument("--before-name", default="art pass 1")
    b.add_argument("--after-name", default="art pass 2")
    args = ap.parse_args()
    {"render": render, "assemble": assemble, "beforeafter": before_after}[args.cmd](args)


if __name__ == "__main__":
    main()
