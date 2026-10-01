#!/usr/bin/env python3
"""All You Got, art pass 2: the owner's timeline in the owner's bar numbering, as data.

The owner's brief (docs/prototypes/liminal-space/02-art-pass-2.md, section 13) is authoritative. It counts
bar 1 from the first real downbeat, AFTER the one-bar count-in, so

    owner bar N  ==  analysis bar N + 1      (SONG-ANALYSIS.md, all-you-got.sections.json)
    owner bar 0  ==  the count-in (0.00-2.20 s)

The tempo map is the song's own (two cue markers in the file): 109 BPM from 0.00 s, a step to 111 BPM on the
downbeat of owner bar 75 (165.1376 s). Beats are 1-based and fractional: beat 4.5 is the "and" of 4.

    python3 tools/liminal/pass2_grid.py            # writes tools/liminal/all-you-got.pass2.json, prints tables
    python3 tools/liminal/pass2_grid.py --md       # the tables as markdown (for PASS2-PLAN.md)

Everything else in pass 2 (the plan, the generator, the analyzers) imports `t()` from here, so there is one
grid. The BIG CLAP positions were checked against the audio (owner numbering confirmed: at 15 of the 16
groove claps the clap-band hit, 1.2-8 kHz in the first 60 ms, is louder than the same beat one bar
earlier; scratch script in PROGRESS-art.md). The hook words were checked with a local recogniser and a
centre-channel vocal envelope per eighth note: LET IT GO and FEEL IT GROW come twice a bar (beats 1, 1.5,
2 and 3, 3.5, 4); IS THAT ALL YOU once a bar, IS on the previous bar's 4.5.
"""

from __future__ import annotations

import argparse
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "all-you-got.pass2.json")

BPM1, BPM2 = 109.0, 111.0
BAR1 = 4 * 60.0 / BPM1          # 2.2018 s
BAR2 = 4 * 60.0 / BPM2          # 2.1622 s
STEP_BAR = 75                   # owner bar where 111 BPM begins
T_STEP = STEP_BAR * BAR1        # 165.1376 s
SONG_END = 253.786              # the file's length
LAST_BAR = 115                  # owner bar 115 is the near-silent tail (analysis bar 116)


def t(bar: float, beat: float = 1.0) -> float:
    """Seconds at owner bar.beat (1-based beat; 4.5 is the 'and' of 4)."""
    b = bar + (beat - 1.0) / 4.0
    if b <= STEP_BAR:
        return b * BAR1
    return T_STEP + (b - STEP_BAR) * BAR2


def beat_len(bar: float) -> float:
    return (BAR1 if bar < STEP_BAR else BAR2) / 4.0


def where(sec: float) -> tuple[int, float]:
    """(owner bar, beat) at a time."""
    if sec < T_STEP:
        b = sec / BAR1
    else:
        b = STEP_BAR + (sec - T_STEP) / BAR2
    bar = int(b)
    return bar, round((b - bar) * 4.0 + 1.0, 3)


# ---- the sections, in the owner's words (section 13), with the musical facts that matter ----------------
SECTIONS = [
    # id, name, first bar, last bar, what the music does, what the owner asks for
    ("countin", "Count-in (skipped)", 0, 0, "one bar of four beats before the song",
     "black"),
    ("intro-a", "Intro, bars 1-4", 1, 4, "the downbeat; quarter-note pulse; sparse call and response",
     "SUDDEN SPLASH OF COLOUR + GEOMETRY out of black; every quarter note a meaningful response"),
    ("intro-b", "Intro, bars 5-8", 5, 8, "bass enters: quarter pulse + eighth pulse + bass eighths + a sustained "
     "bass note building tension; sound design enters", "add the eighth-note subdivision: more tension, movement, "
     "light, colour, growth"),
    ("intro-c", "Intro, bars 9-16", 9, 16, "the riser builds; the 'all you got' chop from bar 13",
     "keep ramping: pulsing, tension, density, growth, anticipation; progressively more elaborate"),
    ("release", "First major release, bars 17-24", 17, 24, "crash into 'All you got'; the full groove; settles",
     "MAJOR VISUAL IMPACT on the downbeat, then small dense Euclidean rooms; quarter pulse drives geometry, "
     "light, colour, objects; settle after the impact"),
    ("verse1-a", "Verse 1, bars 1-8", 25, 32, "the voice; a quarter-note pulse in the low end",
     "slow down; ~4 bars per space; rooms breathe, objects animate, lyrics appear; one related palette"),
    ("verse1-b", "Verse 1, bars 9-16", 33, 40, "pre-chorus, restless; ends 'it's steps in a process, let it go'",
     "a different, deliberate palette"),
    ("pause", "The pause bar", 41, 41, "bass and kick out for exactly one bar under 'let it go'",
     "cut to black or a dramatic pause; LET IT GO animates; falling off, abrupt stop, quick shift of tone"),
    ("letgo", "LET IT GO, 8 bars", 42, 49, "kick and bass return; 'let it go' twice a bar; rebuild at 48-49",
     "first serious environmental lyrics: LET on one wall, IT elsewhere, GO on another surface; quicker rooms"),
    ("verse2", "Verse 2, 16 bars", 50, 65, "full groove; the densest lyric; noise sweeps at phrase ends",
     "verse pacing with more tension: faster objects, denser rooms, more aggressive effects, stronger colour"),
    ("bridge-t", "Bridge transition", 66, 66, "an impact on the downbeat; the key lifts to F; 'feel it grow' "
     "begins; snare fills on beats 3 and 4", "lifting, growth, elevation; leave the rooms"),
    ("bridge1-a", "Bridge 1, bars 1-6", 67, 72, "'feel it grow' twice a bar, a chorus joins; sub drops, air "
     "rises", "growth and elevation, not the quarter pulse: lift, soar, grow, expand, rise"),
    ("bridge1-b", "Bridge 1, bars 7-8", 73, 74, "the chorus drops out, the drums pause, synths ring out",
     "growth completes; landing; a controlled descent"),
    ("bridge2", "Bridge 2, 8 bars", 75, 82, "111 BPM; a driving eighth-note thump, no sub, dark top; 'is that "
     "all you' once a bar; the pulse stops on 82.3", "one room, the objects themselves: spin, glow, transform; "
     "reflection"),
    ("bridge3", "Bridge 3, dance, 8 bars", 83, 90, "the beat returns, strongest offbeat hats; 90.3 drums pause, "
     "a bright synth sweep over beats 3-4, bass fill on 4", "warm, celebratory, euphoric, playful; the camera "
     "soars and glides; the quarter note through effects, never the camera"),
    ("chorus-a", "Final chorus, bars 1-8", 91, 98, "the release: the F world, sustained; LET IT GO chanted",
     "celebration; open beyond the rooms; quarter-note FEEL, not PULSE"),
    ("chorus-b", "Final chorus, bars 9-16", 99, 106, "'it's just steps in a process, for your life'",
     "support the lyric change"),
    ("chorus-c", "Final chorus, bars 17-22", 107, 112, "the loudest bars; LET IT GO chants join; 112.3-4 snare "
     "fills", "keep building; the fill prepares the crash"),
    ("crash", "Final chorus, bars 23-24", 113, 114, "113.1 the final crash; it rings out",
     "HUGE FINAL CRASH: colour, light, geometry, transformation, release; ring out"),
    ("ending", "Ending", 115, 115, "the tail: near silence", "wrap up, close out, dawn, fade, wash out; connect "
     "back to the beginning"),
]

# ---- BIG CLAPs (section 11 and 13): owner bar, beat ------------------------------------------------------
BIG_CLAPS = [
    (20, 4.0), (24, 4.0),                                   # first release
    (28, 4.0), (32, 4.0), (36, 4.0), (40, 4.0),             # verse 1
    (45, 4.0), (49, 4.0),                                   # let it go
    (51, 4.0), (53, 4.0), (55, 4.0), (57, 4.0), (59, 4.0), (61, 4.0), (63, 4.0), (65, 4.0),   # verse 2
    (78, 2.5),                                              # bridge 2: "GOT?" -- a colourful sparkle
    (82, 4.0),                                              # bridge 2: transition / collapse / black
]

# ---- other timed events the plan answers -----------------------------------------------------------------
EVENTS = [
    ("splash", 1, 1.0, "out of black: the first splash of colour and geometry"),
    ("bass-in", 5, 1.0, "bass enters: the eighth-note subdivision begins"),
    ("riser", 9, 1.0, "the riser begins"),
    ("hook-chop", 13, 1.0, "the 'all you got' vocal chop begins (about once a bar)"),
    ("gap", 16, 4.0, "a one-beat gap before the release"),
    ("impact", 17, 1.0, "MAJOR VISUAL IMPACT: 'All you got'"),
    ("voice", 25, 1.0, "the voice enters: 'How little do I know?'"),
    ("palette-turn", 33, 1.0, "verse 1 bars 9-16: a different palette"),
    ("pause", 41, 1.0, "the pause bar: bass and kick out for one bar"),
    ("return", 42, 1.0, "kick and bass return"),
    ("rebuild", 48, 1.0, "the synth rebuild (mids gated, hats building)"),
    ("verse2", 50, 1.0, "verse 2"),
    ("pivot", 66, 1.0, "impact; the key lifts to F; 'feel it grow' begins"),
    ("fill-a", 66, 3.0, "snare fill, beat 3"),
    ("fill-b", 66, 4.0, "snare fill, beat 4"),
    ("lift", 67, 1.0, "bridge 1: growth"),
    ("landing", 73, 1.0, "chorus out, drums pause, synths ring out"),
    ("tempo", 75, 1.0, "111 BPM; 'is that all you'"),
    ("pulse-stop", 82, 3.0, "the eighth-note pulse stops"),
    ("dance", 83, 1.0, "the beat returns: the dance"),
    ("sweep", 90, 3.0, "drums pause; bright synth sweep over beats 3-4"),
    ("bass-fill", 90, 4.0, "bass fill"),
    ("chorus", 91, 1.0, "the final chorus"),
    ("lyric-change", 99, 1.0, "'it's just steps in a process, for your life'"),
    ("chants", 107, 1.0, "LET IT GO chants join; the loudest bars"),
    ("final-fill-a", 112, 3.0, "snare fill, beat 3"),
    ("final-fill-b", 112, 4.0, "snare fill, beat 4"),
    ("crash", 113, 1.0, "HUGE FINAL CRASH"),
    ("bass-out", 114, 1.0, "the bass leaves; the ring-out"),
    ("tail", 115, 1.0, "the near-silent tail"),
]


def hook(words, bar, beats, phrase):
    return [{"text": w, "bar": bar, "beat": b, "t": round(t(bar, b), 4), "phrase": phrase}
            for w, b in zip(words, beats)]


def lyrics() -> list[dict]:
    """Every timed lyric word or line the plan places in the world."""
    out = []
    # The pause bar and the LET IT GO section: twice a bar (beats 1, 1.5, 2 and 3, 3.5, 4).
    for bar in range(41, 50):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            out += hook(["LET", "IT", "GO"], bar, [b0, b0 + 0.5, b0 + 1.0], f"letgo-{bar}-{half}")
    # FEEL IT GROW: from the bridge transition through bridge 1 bar 6 (owner 66-72), twice a bar.
    for bar in range(66, 73):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            out += hook(["FEEL", "IT", "GROW"], bar, [b0, b0 + 0.5, b0 + 1.0], f"grow-{bar}-{half}")
    # IS THAT ALL YOU (bridge 2, owner 75-82): IS on the previous bar's 4.5, THAT 1, ALL 1.5, YOU 2; GOT? on
    # bar 78 beat 2.5; the last one drifts away unfinished.
    for bar in range(75, 83):
        out += hook(["IS"], bar - 1, [4.5], f"isthat-{bar}")
        out += hook(["THAT", "ALL", "YOU"], bar, [1.0, 1.5, 2.0], f"isthat-{bar}")
        if bar == 78:
            out += hook(["GOT?"], bar, [2.5], f"isthat-{bar}")
        if bar == 82:
            out[-1]["text"] = "YOU..."
    # Bridge 3 (owner 83-90): IS THAT ALL? and, on bars 84 and 88, IS THAT ALL YOU GOT?
    for bar in range(83, 91):
        out += hook(["IS"], bar - 1, [4.5], f"isthatall-{bar}")
        if bar in (84, 88):
            out += hook(["THAT", "ALL", "YOU", "GOT?"], bar, [1.0, 1.5, 2.0, 2.5], f"isthatall-{bar}")
        else:
            out += hook(["THAT", "ALL?"], bar, [1.0, 1.5], f"isthatall-{bar}")
    # The final chorus, bars 1-8 (owner 91-98): LET IT GO chanted, twice a bar.
    for bar in range(91, 99):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            out += hook(["LET", "IT", "GO"], bar, [b0, b0 + 0.5, b0 + 1.0], f"chorus-{bar}-{half}")
    # Bars 9-22 (owner 99-112): the lead line every four bars, a two-bar phrase with its pickup on the bar
    # before ("it's just" on beat 4); the LET IT GO chants rejoin from bar 17 (owner 107).
    for bar in (99, 103, 107, 111):
        out.append({"text": "IT'S JUST STEPS IN A PROCESS", "bar": bar - 1, "beat": 4.0, "t": round(t(bar - 1, 4.0), 4),
                    "phrase": f"steps-{bar}", "line": True})
        out.append({"text": "FOR YOUR LIFE", "bar": bar, "beat": 3.0, "t": round(t(bar, 3.0), 4),
                    "phrase": f"steps-{bar}", "line": True})
    for bar in range(107, 113):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            out += hook(["LET", "IT", "GO"], bar, [b0, b0 + 0.5, b0 + 1.0], f"chant-{bar}-{half}")
    # The verses: lines (approximate starts from SONG-ANALYSIS.md, placed by a recogniser), not words.
    verse_lines = [
        (55.3, "HOW LITTLE DO I KNOW?"), (61.5, "TAKE STEPS IN THE PROCESS, BREATHE AND GROW"),
        (63.7, "HOW LITTLE DO I KNOW"), (68.4, "CAN YOU TELL ME IT'S FINE THOUGH?"),
        (70.2, "IT'S STEPS IN THE PROCESS, FEEL AND GROW"), (72.4, "COME ON, TELL ME WHAT YOU WANNA"),
        (76.3, "MAYBE CAUSE A LITTLE DRAMA"), (78.5, "IF YOU FEEL IT, SAY IT, LET IT SHOW"),
        (81.1, "CAN YOU TELL ME IT'S FINE THOUGH?"), (85.1, "I KNOW, GET A LITTLE PEACE OF MIND THOUGH"),
        (87.5, "IT'S STEPS IN A PROCESS, LET IT GO"),
        (109.8, "DO YOU WANNA HAVE FUN?"), (111.4, "AS THE FIRES KEEP BURNING"),
        (113.1, "AND THE WORLD STOPS TURNING"), (114.5, "AND EVERYONE UNDER THE SUN"),
        (115.9, "TAKES STEPS IN THE PROCESS TO HEAL AND GROW"), (118.7, "TELL ME YOU'RE THE ONE"),
        (120.3, "TO MAKE THE WORLD STOP HURTING"), (122.0, "AND MY HEART KEEP PUMPING"),
        (123.7, "WHILE EVERYONE UNDER THE SUN"), (125.3, "TAKE STEPS IN THE PROCESS TO FEEL IT GROW"),
        (127.5, "HOW LITTLE DO I KNOW?"), (136.0, "STEPS IN THE PROCESS, BREATHE AND GROW"),
        (140.0, "TELL ME IT'LL BE FINE THOUGH?"), (143.3, "STEPS IN THE PROCESS, FEEL IT GROW"),
    ]
    for sec, text in verse_lines:
        bar, beat = where(sec)
        out.append({"text": text, "bar": bar, "beat": beat, "t": sec, "phrase": "verse", "line": True,
                    "approximate": True})
    return sorted(out, key=lambda w: w["t"])


def build() -> dict:
    sections = []
    for sid, name, b0, b1, music, ask in SECTIONS:
        t0 = t(b0)
        t1 = t(b1 + 1) if b1 < LAST_BAR else SONG_END
        sections.append({"id": sid, "name": name, "bars": [b0, b1], "analysisBars": [b0 + 1, b1 + 1],
                         "t0": round(t0, 4), "t1": round(t1, 4), "bpm": BPM1 if b0 < STEP_BAR else BPM2,
                         "music": music, "ask": ask})

    def section_of(sec):
        for s in sections:
            if s["t0"] - 1e-6 <= sec < s["t1"]:
                return s["id"]
        return sections[-1]["id"]
    claps = []
    for i, (bar, beat) in enumerate(BIG_CLAPS):
        sec = t(bar, beat)
        claps.append({"id": f"C{i + 1:02d}", "bar": bar, "beat": beat, "t": round(sec, 4),
                      "analysisBar": bar + 1, "section": section_of(sec)})
    events = [{"id": e, "bar": bar, "beat": beat, "t": round(t(bar, beat), 4), "note": note, "section": section_of(t(bar, beat))}
              for e, bar, beat, note in EVENTS]
    return {
        "format": "avgen-liminal-pass2-grid", "version": 1,
        "song": {"path": "~/Desktop/All You Got.wav", "duration": SONG_END},
        "numbering": "owner bar N = analysis bar N + 1; owner bar 0 is the count-in; beats 1-based, 4.5 = the and of 4",
        "tempo": [{"bar": 0, "bpm": BPM1, "t": 0.0, "barSeconds": BAR1},
                  {"bar": STEP_BAR, "bpm": BPM2, "t": round(T_STEP, 4), "barSeconds": BAR2}],
        "sections": sections,
        "bigClaps": claps,
        "events": events,
        "lyrics": lyrics(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", action="store_true", help="print the tables as markdown")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    doc = build()
    with open(a.out, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    if a.md:
        print("| section | owner bars | analysis bars | start (s) | end (s) | BPM |")
        print("|---|---|---|---|---|---|")
        for s in doc["sections"]:
            print(f"| {s['name']} | {s['bars'][0]}-{s['bars'][1]} | {s['analysisBars'][0]}-{s['analysisBars'][1]} | "
                  f"{s['t0']:.2f} | {s['t1']:.2f} | {s['bpm']:.0f} |")
        print()
        print("| clap | owner bar.beat | time (s) | section |")
        print("|---|---|---|---|")
        for c in doc["bigClaps"]:
            print(f"| {c['id']} | {c['bar']}.{c['beat']:g} | {c['t']:.3f} | {c['section']} |")
    else:
        for s in doc["sections"]:
            print(f"{s['id']:<10} bars {s['bars'][0]:3d}-{s['bars'][1]:3d}  {s['t0']:8.3f}-{s['t1']:8.3f}  {s['name']}")
        for c in doc["bigClaps"]:
            print(f"{c['id']}  {c['bar']:3d}.{c['beat']:<4g} {c['t']:8.3f}  {c['section']}")
        print(f"{len(doc['lyrics'])} lyric entries -> {a.out}")


if __name__ == "__main__":
    main()
