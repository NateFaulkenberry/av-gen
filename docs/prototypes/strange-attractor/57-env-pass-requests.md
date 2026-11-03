# Pass 3: requests between the agents

The hero agent writes engine and shared-file requests here; the SA agent implements them and answers in place.
The SA agent also hands over work here.

Format: one section per item, `Status: open | doing | done (commit)`.

## From the SA agent to the hero agent

### H-1: Mars drift edges (M3g §6 leftover, 54- §3)
Status: open, handed over.
The marsDrift sand drifts show a hard edge where a drift meets the rock in places. The sand in the pyramids' courses
was raised in e3b2d57b (`pass2/build_pyramids.py`, outputs `pyramid-*-m3g.glb`, `pyramid-*-sand-m3g.glb`); the drift
edge blending on the terrain was not done. Files: `build_mars.py` (marsRock sand smoothstep [0.45, 0.75], the
marsDrift program), now yours.

### H-2: the Mars screenshot camera before/after (M3g §6 leftover)
Status: open, handed over.
The owner's screenshot from the Mars arrival (about 70.5 s) needs a before/after from the same camera once the Mars
work lands. The M3g frames at 70.5 s are the "before".

### H-3: the seated Sasquatch (sasquatch-c) on its seat rock
Status: open, for Phase C (the camp clearing is yours).
check_contacts (hero/check_contacts.py) results:
- before the drop (node y 1.76): a 6.3 cm gap over the seat; the fur shells clip the rock in 18/193 frames, 9.2 cm deep.
- after the 3 cm drop now in `build_journey.py` (SEAT_SIT = 0.03, node y 1.73): the body mesh goes into the rock in
  94/193 frames and the shells in 166/193, up to 9 cm deep.
- the best search result (seat-search, rock moved dx 0.021, dz 0.077, sy 0.88) had 0 penetrating frames and a 3 cm gap.
The 3 cm drop is too much for the animation's lowest frames. Suggest: the seat rock reshaped or swapped for one with a
flat top in Phase C, then SEAT_SIT re-fitted with check_contacts. SEAT_SIT stays in `build_journey.py` (shared file):
request the value here and the SA agent sets it.

## From the hero agent to the SA agent

(none yet)
