"""The production's own directives: what each stretch of the music should feel like, and the look
state and camera language that realise it.

A directive is intent first -- the sentence is the brief for everything else in the segment -- and
then the numbers the generator turns into keys. `look.py` reads the look states, `shots.py` is
written against the camera lines, and the generator writes this table into the production record
(docs/glowmere-valley-3/03-directives.md) so the record and the film cannot disagree.

Look state fields, all relative to the BASE look in look.py:
  ev        exposure compensation offset, stops
  sat       grade saturation, absolute
  fog       volumetric density multiplier on BASE
  light     the valley's own light: multiplier on every hero and bioluminescent emission
  aurora    the aurora's intensity, absolute
  sparkle   spores and fireflies: multiplier on their spawn rate and glow
  temp      grade temperature (-1 cool .. +1 warm)
"""

from . import music

# The concept, in one line: the valley's light is the protagonist. It wakes layer by layer with the
# music, dims when the visitor passes, is drained to almost nothing while the saucer takes one of the
# valley's animals, and comes back rebuilt -- brighter and wider than it began -- on the drop.
CONCEPT = ("The valley's light is the protagonist. It wakes layer by layer as the track does, dims when "
           "a visitor passes over, is drained to almost nothing while the saucer takes one of the "
           "valley's animals -- and comes back rebuilt, brighter and wider than it began, on the drop.")

DIRECTIVES = [
    {"segment": "cold-open", "name": "Nocturne",
     "intent": "Already alive on the first kick: a night valley breathing, seen from inside it -- low, "
               "among the ferns, the elder's gold the only warm thing in the world.",
     "camera": "Low and slow. One unbroken move from the first downbeat; the camera is a visitor on foot.",
     "look": {"ev": 0.0, "sat": 0.95, "fog": 1.0, "light": 0.85, "aurora": 1.2, "sparkle": 0.5, "temp": -0.10},
     "motifs": ["heartbeat (elder gills on the kick)", "breath (caps on the 2-bar bass glide)"]},
    {"segment": "riff-groove", "name": "The valley introduced",
     "intent": "Meet the place and who lives in it: the river, the heroes, an alien going about its "
               "business, a horse grazing by the elder. Unhurried; the riff is a statement, not a build.",
     "camera": "Cuts every 2-4 bars. Eye height for aliens, low angles for mushrooms, one long-lens wide "
               "with a foreground. Lateral moves for parallax.",
     "look": {"ev": 0.0, "sat": 1.0, "fog": 1.0, "light": 0.9, "aurora": 1.3, "sparkle": 0.6, "temp": -0.10},
     "motifs": ["heartbeat", "breath"]},
    {"segment": "first-pullback", "name": "A shadow passes",
     "intent": "The music drops out for two bars and so does the valley's light: something crosses the "
               "sky. A promise the drop keeps later.",
     "camera": "One held shot looking up the valley at the sky, so the shape crosses the frame.",
     "look": {"ev": -0.6, "sat": 0.7, "fog": 1.2, "light": 0.3, "aurora": 0.6, "sparkle": 0.1, "temp": -0.30},
     "motifs": ["the flyby"]},
    {"segment": "groove-2", "name": "Waking",
     "intent": "The light comes back a little brighter than before. The aliens wander; the camera starts "
               "to travel with them. Curiosity.",
     "camera": "2-4 bar cuts; a tracking shot through foliage; a river-level travel.",
     "look": {"ev": 0.05, "sat": 1.02, "fog": 1.0, "light": 1.0, "aurora": 1.4, "sparkle": 0.7, "temp": -0.05},
     "motifs": ["heartbeat", "breath", "kick gap (a held breath at bar 24)"]},
    {"segment": "lift", "name": "Something stirs",
     "intent": "The body of the track arrives: movement becomes purposeful, compositions grow, the camera "
               "rises for the first time.",
     "camera": "Longer moves; a crane up past a foreground to a wider view; 2-bar cuts.",
     "look": {"ev": 0.1, "sat": 1.06, "fog": 0.95, "light": 1.1, "aurora": 1.6, "sparkle": 0.85, "temp": 0.0},
     "motifs": ["heartbeat", "breath", "kick gap (bar 40)"]},
    {"segment": "arrival", "name": "The valley lights up",
     "intent": "The shimmer arrives and the whole valley answers: spores, fireflies, every cap. The first "
               "grand wide -- a place, not a model.",
     "camera": "A long-lens wide from far and low with the horizon in frame, then closer sparkle.",
     "look": {"ev": 0.2, "sat": 1.14, "fog": 0.9, "light": 1.35, "aurora": 2.0, "sparkle": 1.25, "temp": 0.05},
     "motifs": ["heartbeat", "breath", "sparkle on the top of the mix"]},
    {"segment": "melodic-plateau", "name": "Communion",
     "intent": "Hypnotic and warm: long takes among the lit heroes, the aliens gathered in the glow. The "
               "film's steadiest stretch -- variety of angle, not of pace.",
     "camera": "Long flowing takes of 4-8 bars: a slow orbit of the elder, a river travel, a follow.",
     "look": {"ev": 0.15, "sat": 1.1, "fog": 0.95, "light": 1.3, "aurora": 1.9, "sparkle": 1.15, "temp": 0.05},
     "motifs": ["heartbeat", "breath", "sparkle", "kick gaps (bars 56, 72)"]},
    {"segment": "lead-forward", "name": "Looking up",
     "intent": "The lead comes forward and the aliens look up. The aurora answers the melody.",
     "camera": "Low angles up past the aliens toward the sky; slow push-ins.",
     "look": {"ev": 0.1, "sat": 1.08, "fog": 1.0, "light": 1.2, "aurora": 2.4, "sparkle": 1.0, "temp": 0.0},
     "motifs": ["heartbeat", "breath", "sparkle", "aurora carries the lead"]},
    {"segment": "suspension", "name": "The visitor",
     "intent": "The sparkle is cut from the music and from the valley: the saucer comes in over the rim, "
               "slow and heavy, and the valley's light drains as it comes.",
     "camera": "Locked-off wides on long lenses; stillness is the tension. The saucer grows in frame.",
     "look": {"ev": -0.2, "sat": 0.85, "fog": 1.15, "light": 0.7, "aurora": 1.0, "sparkle": 0.2, "temp": -0.25},
     "motifs": ["breath (slowing)", "the approach"]},
    {"segment": "submerged-break", "name": "Submerged",
     "intent": "Underwater: dark, heavy, blue. The valley's light is almost gone; only the saucer's own "
               "lights. The world holds its breath.",
     "camera": "Slow push-ins, one per two bars. Heavy, low.",
     "look": {"ev": -0.8, "sat": 0.5, "fog": 1.8, "light": 0.25, "aurora": 0.3, "sparkle": 0.0, "temp": -0.45},
     "motifs": ["the approach"]},
    {"segment": "riser", "name": "The lift",
     "intent": "The beam lights and the horse rises; light floods up the column and back into the world. "
               "Everything accelerates toward the drop.",
     "camera": "Cuts compress: 2 bars, 1 bar, half bars, beats. Low angles up the beam; the aliens' faces.",
     "look": {"ev": -0.3, "sat": 0.9, "fog": 1.2, "light": 0.6, "aurora": 1.2, "sparkle": 0.4, "temp": -0.1},
     "motifs": ["the beam", "roll accelerates the cutting"]},
    # The drop is a new state of the same world, not a new style (the owner, brief section 7). The
    # first pass stepped the whole grade here -- exposure +0.5, saturation 1.3, temperature +0.12 --
    # and the valley became another picture. Now the grade holds at the plateau's (ev +0.15, sat 1.10,
    # temp +0.05, all inside the range the film has already shown by the arrival), and the drop is
    # carried by what the world does: its own light brighter than ever, the air a little clearer,
    # more spores and fireflies, every route answering at the section's full depth, the bar's waves
    # of light reaching the whole valley, the wind up and the water torn by it.
    # The world's side was first set below the first pass's too (light 1.6, aurora 2.4, sparkle 1.5
    # against 2.1, 3.2, 1.7), and the drop came out darker than the plateau it follows (mean luma 0.146
    # against 0.186, gv3-look iteration 3), its sky 0.82 of the first pass's, the spores round the elder
    # thin. The grade stays held; the valley's own light (2.0, 1.5x the plateau's), its sky (2.8) and
    # its spores and fireflies (2.0) carry the drop.
    {"segment": "drop", "name": "Rebuilt",
     "intent": "The horse is gone and the valley's light comes back rebuilt: the same night and the same "
               "colours, but every hero answering at full strength, rings of light running out from the "
               "elder across the whole valley on every bar, the wind up. The music gets wider, not busier, "
               "and so does the picture.",
     "camera": "Downbeat cut on the crash, then big, wide, lateral moves and a crane; bar cuts for the "
               "first phrase, then long wides. The saucer climbs away over the north rim.",
     "look": {"ev": 0.15, "sat": 1.10, "fog": 0.85, "light": 2.0, "aurora": 2.8, "sparkle": 2.0, "temp": 0.05},
     "motifs": ["heartbeat (full depth)", "every hero on its layer at full depth",
                "the bar's rings across the whole valley, and the drop's own ring", "flash on the crash"]},
    {"segment": "tail", "name": "Afterglow",
     "intent": "Kick and clap alone: one image holds while the valley settles -- then black on the last hit.",
     "camera": "One held shot. Cut to black at 224.79 s.",
     "look": {"ev": 0.0, "sat": 1.0, "fog": 1.0, "light": 1.1, "aurora": 1.4, "sparkle": 0.5, "temp": 0.05},
     "motifs": ["heartbeat alone"]},
]


def directive(segment):
    for d in DIRECTIVES:
        if d["segment"] == segment:
            return d
    raise KeyError(segment)


def markdown():
    """The directives as the production record's table."""
    lines = ["# 3. Directives", "", f"**Concept.** {CONCEPT}", "",
             "Each segment's directive is intent first; the look state and the camera language are how "
             "the generator (`tools/gv3/`) realises it. Numbers are relative to the base look.", "",
             "| Segment | Time (s) | Name | Intent | Camera | ev | sat | fog | light | aurora | sparkle |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for d in DIRECTIVES:
        a, b = music.segment(d["segment"])
        L = d["look"]
        lines.append(f"| {d['segment']} | {a:.2f}-{b:.2f} | {d['name']} | {d['intent']} | {d['camera']} | "
                     f"{L['ev']:+.2f} | {L['sat']:.2f} | {L['fog']:.2f} | {L['light']:.2f} | {L['aurora']:.1f} | "
                     f"{L['sparkle']:.2f} |")
    lines.append("")
    return "\n".join(lines)
