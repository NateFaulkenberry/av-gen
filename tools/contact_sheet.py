#!/usr/bin/env python3
"""Lay rendered frames out on one labelled sheet, so a whole edit can be judged at a glance.

A render is judged shot by shot, and a shot cannot be judged from one frame: a push-in that ends
on empty ground looks fine at its first frame, and a cut that lands on the wrong subject is only
visible beside its neighbours. This puts the frames that matter side by side with their times and
labels, as one PNG the reviewer (human or agent) can read in a single look.

Three ways to choose the frames:

    # a list of PNGs, in the order given
    tools/contact_sheet.py sheet.png renders/a.png renders/b.png --cols 2

    # instants of a video, labelled
    tools/contact_sheet.py sheet.png --video preview.mov --times 12.5,30.02,177.71 \
        --labels "open,groove,drop"

    # one row per shot: frames at the shot's start, middle and end (inset from the cuts)
    tools/contact_sheet.py sheet.png --video preview.mov --shots shots.json --per-shot 3

`shots.json` is a list of objects with `start` and `end` in seconds and optional `id` and
`label`, or an object with such a list under `shots`. Frames are pulled from the video with
ffmpeg, which must be on PATH for the video forms. Needs Pillow.
"""
import argparse
import io
import json
import pathlib
import subprocess
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover - a clear message beats a traceback
    print("contact_sheet.py needs Pillow (python3 -m pip install pillow)", file=sys.stderr)
    sys.exit(2)

# Frames are sampled this far inside a shot's cuts, so a sample never lands on the neighbour's
# first frame when the cut is not frame-aligned.
CUT_INSET_SECONDS = 0.12
LABEL_HEIGHT = 22
GUTTER = 6
BACKGROUND = (16, 16, 18)
LABEL_COLOUR = (230, 230, 230)
DIM_COLOUR = (150, 150, 155)


def frame_at(video: pathlib.Path, seconds: float) -> Image.Image:
    """One decoded frame of `video` at `seconds` (input seeking, so it is fast on long files)."""
    command = ["ffmpeg", "-v", "error", "-ss", f"{max(0.0, seconds):.4f}", "-i", str(video),
               "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"]
    result = subprocess.run(command, capture_output=True)
    if result.returncode != 0 or not result.stdout:
        raise RuntimeError(f"ffmpeg could not read {video} at {seconds:.3f}s: "
                           f"{result.stderr.decode(errors='replace').strip()[-200:]}")
    return Image.open(io.BytesIO(result.stdout)).convert("RGB")


def load_shots(path: pathlib.Path) -> list:
    data = json.loads(path.read_text())
    shots = data["shots"] if isinstance(data, dict) else data
    for index, shot in enumerate(shots):
        if "start" not in shot or "end" not in shot:
            raise ValueError(f"shot {index} in {path} has no start/end")
    return shots


def sample_times(start: float, end: float, count: int) -> list:
    """`count` instants spread over [start, end], inset from both cuts."""
    lo, hi = start + CUT_INSET_SECONDS, end - CUT_INSET_SECONDS
    if hi <= lo:
        return [0.5 * (start + end)] * count
    if count == 1:
        return [0.5 * (lo + hi)]
    return [lo + (hi - lo) * i / (count - 1) for i in range(count)]


def font(size: int):
    for candidate in ("/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Monaco.ttf",
                      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"):
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def compose(cells: list, cols: int, width: int, row_titles: list = None) -> Image.Image:
    """`cells` is a list of (image, label) laid out left to right, top to bottom.

    With `row_titles`, each row gets a title strip above it (used for one-row-per-shot sheets).
    """
    if not cells:
        raise ValueError("nothing to put on the sheet")
    aspect = cells[0][0].height / cells[0][0].width
    thumb_h = round(width * aspect)
    rows = (len(cells) + cols - 1) // cols
    title_h = LABEL_HEIGHT if row_titles else 0
    sheet_w = cols * width + (cols + 1) * GUTTER
    row_h = title_h + thumb_h + LABEL_HEIGHT + GUTTER
    sheet = Image.new("RGB", (sheet_w, rows * row_h + GUTTER), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    small, title = font(13), font(15)
    for index, (image, label) in enumerate(cells):
        row, col = divmod(index, cols)
        x = GUTTER + col * (width + GUTTER)
        y = GUTTER + row * row_h
        if row_titles and col == 0:
            draw.text((GUTTER, y + 2), row_titles[row], fill=LABEL_COLOUR, font=title)
        thumb = image.resize((width, thumb_h), Image.LANCZOS)
        sheet.paste(thumb, (x, y + title_h))
        draw.text((x + 2, y + title_h + thumb_h + 3), label, fill=DIM_COLOUR, font=small)
    return sheet


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("frames", nargs="*", type=pathlib.Path, help="PNG files, in order")
    parser.add_argument("--video", type=pathlib.Path)
    parser.add_argument("--times", help="comma-separated seconds (with --video)")
    parser.add_argument("--labels", help="comma-separated labels for --times or the frames")
    parser.add_argument("--shots", type=pathlib.Path, help="shot list JSON (with --video)")
    parser.add_argument("--per-shot", type=int, default=3, help="frames per shot row")
    parser.add_argument("--cols", type=int, default=4, help="columns (ignored with --shots)")
    parser.add_argument("--width", type=int, default=400, help="thumbnail width in pixels")
    args = parser.parse_args()

    labels = args.labels.split(",") if args.labels else None
    cells, row_titles, cols = [], None, args.cols

    if args.shots:
        if not args.video:
            parser.error("--shots needs --video")
        shots = load_shots(args.shots)
        cols, row_titles = args.per_shot, []
        for index, shot in enumerate(shots):
            name = str(shot.get("id", index + 1))
            span = f"{shot['start']:.2f}-{shot['end']:.2f}s"
            row_titles.append(f"{name}  {span}  {shot.get('label', '')}".rstrip())
            for t in sample_times(float(shot["start"]), float(shot["end"]), args.per_shot):
                cells.append((frame_at(args.video, t), f"{t:.2f}s"))
    elif args.video:
        if not args.times:
            parser.error("--video needs --times or --shots")
        times = [float(t) for t in args.times.split(",")]
        for i, t in enumerate(times):
            label = labels[i] if labels and i < len(labels) else ""
            cells.append((frame_at(args.video, t), f"{t:.2f}s {label}".rstrip()))
    else:
        if not args.frames:
            parser.error("give PNG frames, or --video with --times/--shots")
        for i, path in enumerate(args.frames):
            label = labels[i] if labels and i < len(labels) else path.stem
            cells.append((Image.open(path).convert("RGB"), label))

    sheet = compose(cells, cols, args.width, row_titles)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    print(f"{args.output}: {len(cells)} frame(s), {sheet.width}x{sheet.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
