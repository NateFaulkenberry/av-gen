"""The musical grid of "Rebuild", as the one place every time in Glowmere Valley 3 comes from.

Everything in the production -- a cut, a key, an accent, an effect window -- is placed with `bar()`
and `beat()` rather than written as seconds, so the whole film re-times from two numbers if the grid
is ever re-measured, and so a reader sees "bar 97" (the drop) instead of 177.711.

The grid was fitted exhaustively (129.90-130.10 BPM in 0.01 steps, all phases) on four independent
onset envelopes and agrees with a blind beat tracker to 7 ms RMS; see
docs/glowmere-valley-3/01-music.md.
"""

BPM = 130.0
BEAT = 60.0 / BPM            # 0.461538 s
BAR = 4.0 * BEAT             # 1.846154 s
FIRST_DOWNBEAT = 0.480       # the audio starts on bar 1 beat 1; before it is silent pre-roll
LAST_HIT = 224.788           # bar 122 beat 3; a hard stop
TAIL_END = 225.38            # where the reverb ring dies


def beat(bar_number: float, beat_number: float = 1.0) -> float:
    """Seconds at `bar_number` (1-based) beat `beat_number` (1-based, fractional allowed)."""
    return FIRST_DOWNBEAT + (bar_number - 1.0) * BAR + (beat_number - 1.0) * BEAT


def bar(bar_number: float) -> float:
    """Seconds at the downbeat of `bar_number` (1-based)."""
    return beat(bar_number, 1.0)


def bars(count: float) -> float:
    """A duration of `count` bars, in seconds."""
    return count * BAR


def beats(count: float) -> float:
    """A duration of `count` beats, in seconds."""
    return count * BEAT


def bar_of(seconds: float) -> float:
    """The (fractional, 1-based) bar a time falls in -- for labels and checks, never for placing."""
    return 1.0 + (seconds - FIRST_DOWNBEAT) / BAR


def label(seconds: float) -> str:
    """'bar.beat' for a time on the grid, e.g. 177.711 -> '97.1'."""
    b = bar_of(seconds)
    whole = int(b + 1e-6)
    within = (b - whole) * 4.0 + 1.0
    return f"{whole}.{within:g}" if abs(within - round(within)) > 1e-3 else f"{whole}.{int(round(within))}"


# The segmentation (docs/glowmere-valley-3/01-music.md §1.3). (name, first bar, bar after the last)
SEGMENTS = [
    ("cold-open",        1,   5),
    ("riff-groove",      5,  15),
    ("first-pullback",  15,  17),
    ("groove-2",        17,  33),
    ("lift",            33,  41),
    ("arrival",         41,  49),
    ("melodic-plateau", 49,  73),
    ("lead-forward",    73,  81),
    ("suspension",      81,  89),
    ("submerged-break", 89,  93),
    ("riser",           93,  97),
    ("drop",            97, 121),
    ("tail",           121, 123),
]


def segment(name: str) -> tuple:
    """(start seconds, end seconds) of a named segment. The tail ends at the reverb, not bar 123."""
    for n, first, after in SEGMENTS:
        if n == name:
            end = TAIL_END if n == "tail" else bar(after)
            return bar(first), end
    raise KeyError(name)


def segment_at(seconds: float) -> str:
    for n, first, after in SEGMENTS:
        if bar(first) - 1e-6 <= seconds < bar(after) - 1e-6:
            return n
    return SEGMENTS[-1][0] if seconds >= bar(121) else SEGMENTS[0][0]


# Musical events a picture can land on (01-music.md §1.4 and the event list). (seconds, kind, note)
KICK_GAPS = [beat(b, 4) for b in (24, 40, 56, 72)]


def kicks():
    """Every kick the track plays, as (seconds, strength): four on the floor from the first downbeat,
    out for the pull-back (bars 15-16), cut on the gap beats, low-passed in the break (bars 89-92,
    half strength), and alone with the clap in the tail up to the last hit (01-music.md §1.4; the
    pull-back, the break and the riser were checked on the spectrogram, not assumed)."""
    out = []
    for b in range(1, 123):
        if b in (15, 16):
            continue
        for n in range(1, 5):
            t = beat(b, n)
            if t > LAST_HIT + 1e-3:
                break
            if t in KICK_GAPS:
                continue
            out.append((t, 0.5 if 89 <= b <= 92 else 1.0))
    return out
TURNAROUNDS = [beat(b, 3) for b in (24, 32, 40, 48, 56, 64, 72, 80, 88, 104, 112)]
EVENTS = [
    (bar(1),   "impact",   "cold start on the full groove"),
    (bar(5),   "entry",    "plucked riff enters"),
    (bar(15),  "exit",     "pull-back: drums and top drop out"),
    (bar(17),  "impact",   "full drums return"),
    (bar(33),  "entry",    "lift: mid synth and busier hats"),
    (bar(41),  "entry",    "arrival: shimmer top layer and 16th shakers"),
    (bar(49),  "entry",    "lead shifts to F# over G"),
    (bar(73),  "entry",    "mid lead pushed forward"),
    (bar(81),  "exit",     "suspension: shimmer and 16ths cut"),
    (bar(89),  "dropout",  "submerged break: top closes, sub swells"),
    (bar(93),  "riser",    "riser and 8th-note roll begin"),
    (bar(95),  "fill",     "roll doubles to 16ths"),
    (beat(96, 4), "fill",  "last beat of the roll, ~32nds"),
    (bar(97),  "drop",     "THE DROP: crash, full kit, widest stereo"),
    (bar(111), "fill",     "last push: mids lift for 2 bars"),
    (bar(113), "entry",    "final phrase, slightly thinner"),
    (bar(121), "exit",     "tail: bass, riff and hats out"),
    (LAST_HIT, "impact",   "final hit"),
]


if __name__ == "__main__":
    for n, first, after in SEGMENTS:
        a, b = segment(n)
        print(f"{n:16s} bars {first:3d}-{after - 1:3d}  {a:8.3f} - {b:8.3f}")
    assert abs(bar(97) - 177.711) < 0.001, bar(97)
    assert abs(bar(41) - 74.326) < 0.001, bar(41)
    assert label(bar(97)) == "97.1"
