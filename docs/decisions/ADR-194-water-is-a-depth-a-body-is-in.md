# ADR-194: Water is a depth a body is in, not a line it stops at

**Status:** Accepted
**Date:** 2026-09-14

## What was there

`TerrainQuery::waterDepthAt` has been in the world layer since ADR-090, and its header says what it
is for:

> Metres of water over the bed at p; 0 on dry land. **The one number a water renderer, a floating
> object and a wading walker all need**, and the continuous half of the water seam.

The water renderer uses it. The floating object uses it. **The wading walker has never existed.**
`TerrainQuery::at` rejected any point standing less than `waterMargin` (0.35 m) above the water
surface as `Submerged`, `Navigator::sample` translated that rejection through, `NavGrid` flagged the
cell `NavWater` and left it out of the walkable set, and `NavSample` -- which carries
`waterSurface` -- did not carry the depth at all, so anything downstream that wanted to know how
deep the water was sampled the world a second time to find out.

The result is that to anything that walks, **a puddle and a lake are the same wall**. A world with a
river through it is two worlds, which ADR-093's own test file records as a thing it had to design
around: *"that world has a river through it, and two points on opposite banks are genuinely
unreachable -- a correct refusal that looks exactly like a broken planner."* Correct against the
rule. The rule was the problem.

## The decision

Water becomes a band rather than a line, in four places, each of which had to move or the other
three would be describing different worlds.

**`WalkRules::wadeDepth` / `NavSettings::wadeDepth`** -- the deepest water a body will enter. Above
it, still `Submerged`. This is one number in the one place ADR-090 put the shared rules, so the
terrain query and the navigator cannot disagree about it.

**`NavSample::waterDepth`** -- carried from the `TerrainPoint` that `Navigator::sample` already
reads, which is derived from the one `WorldMap::sample` that call already takes. **Zero extra
evaluation of the world**; the only reason it was not there before is that nothing asked.

**`NavCell::wade`** -- the depth, quantised over the wade band, in what used to be the cell's pad
byte. The cell is still eight bytes and A* still walks two of them per cache line.

**`NavPathCost::wadePenalty`** -- a wadeable cell is in the walkable set, so without a price A*
routes through a river the moment the river is one centimetre shorter. It is not shorter: the mover
crosses it at half speed.

## The default is 0, and that is the decision, not an omission

Zero reproduces the old rule exactly, `waterMargin` and all. Every scene in the repository was
authored against a walkable set that stops at the waterline, and a default of "0.4 m, which is about
ankle deep" would have quietly opened every shoreline in every one of them to a walker that was
routed around it the day before.

It is also the honest model. **Whether a body wades is a property of the body, not of the water.**
A duck, a person and Glowmere's nine-metre elder disagree about the same ford, and the water has no
opinion. So a scene declares it, through a top-level `navWadeDepth` key validated rather than
clamped, emitted only when it differs from the default so an untouched scene round-trips
byte-identical -- the shape ADR-193 established for `navCellSize`.

**Above 0, `waterMargin` is replaced rather than added to.** Holding both would mean a body that
crosses a half-metre ford and still refuses to stand on a bank two handspans above the water, which
is not a model of anything.

## The price on a ford is a time, not a taste

`explore` walks through water at `1 - wadeDrag * depth/wadeDepth` of its speed, floored at 0.15 --
floored because a drag of 1.0 at full depth would stop the character dead mid-ford, the stuck
watchdog would fire, it would replan and walk back into the same water. A knob at its documented
maximum producing a character that cannot move is a knob that is wrong at the end of its range.

At the default drag of 0.55, a cell at full wade depth takes `1/0.45 = 2.22` times as long to cross
as a dry one. `wadePenalty` is **1.2**, so A*'s cost for that cell is `1 + 1.2 = 2.2`: the planner
is minimising roughly **the time the mover will actually take** rather than a distance the mover does
not experience. The two numbers are a pair, and the comment on each says so -- move one and the
planner and the walker start disagreeing about which way is quicker.

Linear in depth because the resistance is the submerged cross-section, which over ankle-to-thigh is
very close to linear.

## What the string pull would have undone

The string pull straightens A*'s cell staircase using `lineOfSight`, which knows **only
walkability**. A wadeable cell is walkable. So a route the search had just paid to route round the
end of a channel has a perfectly clear straight line back across the channel, and the pull takes it:
the price on water would be a cost paid during the search and discarded immediately after it, which
is worse than never charging it -- the route would *look* considered and be the straight line
anyway.

The rule added is narrow on purpose: **a shortcut may shorten a route; it may not wade on the
route's behalf.** A shortcut whose deepest water exceeds the deepest water on the sub-path it
replaces is refused, and a shorter one is tried. It is not a general cost-guarded pull -- the same
argument applies to scree and thickets, and making the pull respect *those* would change how every
existing scene's routes look, which is not this change's business.

The test for it is the one that would have caught it: the steeply-priced arm asserts the route's
deepest water is under 5 cm, and without the guard it is half a metre.

## Costs nothing where nobody wades

Every new term is multiplied by a quantity that is exactly zero under the default, and the test
suite asserts it rather than claiming it:

* `NavCell::wade` is 0 in every cell of a grid built by a walker with `wadeDepth == 0` -- checked by
  counting them, not by spot-reading one.
* the A* penalty term is therefore exactly 0, so the search is bit-identical.
* `wadeScale` early-outs on `wadeDepth <= 0` before it queries the world, so no existing scene pays
  a water lookup per walking frame to be told the answer is 1.
* the string-pull guard is reached only when a line crosses a cell with `wade > 0`.

The control that makes the speed measurement mean something is the third one: the same flooded
world, the same seed, drag turned to 0, and the distance travelled matches the dry run to within a
millimetre. Without it, "the character went less far in the flooded world" could have been a
different ground height, a different walkable set or a different destination roll -- the measurement
would have been real and would have attributed nothing, which is ADR-182's rule and ADR-193's.

## Measured

On Glowmere, whose river is the thing this exists for, declaring `navWadeDepth: 0.9`:

| | dry walker | wading at 0.9 m |
|---|---:|---:|
| walkable cells | 21,743 | **21,804** |
| water cells | 218 | 218 |
| water cells in the walkable set | 0 | **45** |
| cells carrying a recorded depth | **0** | 218 |

Forty-five of two hundred and eighteen, which is the right shape of answer: the shallow margins of
the run and the west tarn join the walkable set and **the channel does not**. A number nearer 218
would have meant the band was doing nothing, and 0 would have meant the key reached nothing. The
grid is otherwise the one ADR-193 measured -- 154x154 at 4 m, 838 blocked, 172 shore and 67 vista
points -- unchanged, because the default changes nothing.

The speed law, measured at the behaviour layer: the same character from the same seed walks 167.1 m
in a minute on dry ground and 121.9 m through a world flooded to half its wade band. The ratio is
**0.729** against the 0.725 the law predicts, and with `wadeDrag` at 0 the flooded run reproduces
the dry one to within a millimetre.

## What was not done

**Wade depth is not a per-character setting**, although `explore` already resizes the world's
navigator per character for `bodyRadius` and `headroom`. It cannot be, as things stand: the
navigation graph is baked once from the world's navigator and shared by every walker, so a character
that waded deeper than the grid was built for would plan against a walkable set it does not agree
with. `bodyRadius` and `headroom` **already have this problem** -- a wide character's grid is
somebody else's -- and it is unrecorded. Recording it here is most of what can be done about it
without a per-body grid or a grid keyed by body size, neither of which this change needed.

## Revisit triggers

* A scene wants two bodies with different wade depths in the same world. That is the per-body grid
  question above, and it should be answered for `bodyRadius` at the same time.
* Current. A river that carries a body downstream is a force rather than a cost, and nothing
  here models one -- a walker crosses a torrent and a millpond at the same reduced speed.
* `wadeDrag` and `wadePenalty` drifting apart. If a scene tunes one, the planner and the mover stop
  agreeing about the short way and routes will start looking indecisive at fords.
