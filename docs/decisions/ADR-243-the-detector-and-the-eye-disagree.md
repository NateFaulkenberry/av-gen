# ADR-243: The detector and the eye disagree, and the eye decides what ships

**Status:** Accepted
**Date:** 2026-09-15
**Context:** Level 3 §4, the temporal artifact inventory, reviewed by a human at last
**Follows:** ADR-182 (a probe that cannot fail proves nothing), ADR-212 (an offline render may spend
pixels), ADR-170 (the lock does not establish exclusivity)

## Context

§4 has been open for weeks with the same note beside it: *needs the representative suite rendered and
looked at; an agent cannot honestly certify this by measurement.* The suite was rendered — five arms,
static camera, the documented river view, every arm's sequence hash distinct so none was vacuous —
and a person watched it.

They were right to find the question confusing, because **the question was wrong**, twice.

**The first error was mine about the instrument.** I asked whether the artifact was *edge crawl* — a
staircase that marches along an edge. Crawl needs the edge to move relative to the pixel grid, and I
then told the reviewer that a static camera could not produce it. That is only true of static
geometry. Glowmere's grass is wind-animated: the geometry moves while the camera does not, and the
reviewer located the artifact precisely there — *"the grass looked jagged in wind animations"*, at
*"where a grass blade meets what's behind it"*. The footage did contain the thing I had just told
them it could not contain.

**The second error is the one worth an ADR.**

## The finding: the detector is anti-correlated with the eye on the artifact that gets reported

`temporal_stats.py` measures the second difference in time, `|x(t+1) - 2x(t) + x(t-1)|`. That design
is right for what it measures: a pixel that animates smoothly scores near zero however fast it moves,
and a pixel that alternates scores high. Every Priority 1 number came from it and every one of them
stands as a measurement.

On the region the reviewer named, it ranks the arms backwards.

| grass beds | temporal (2nd difference) | spatial (mean \|laplacian\|) | the reviewer |
|---|---:|---:|---|
| t1 baseline | — | — | *"probably not"* ship it |
| t2 FXAA off | **−42%** "better" | **+31%** worse | *"more distracting"* |
| t4 supersample 2× | **+9%** "worse" | **−9%** better | *"much calmer"* |

Both arms, in the opposite direction. A spatial measure — the mean absolute spatial Laplacian, which
is high for a staircased edge and low for a resolved or filtered one — agrees with the reviewer on
both arms and in the same order.

**Why they disagree is not a mystery and not a bug in either.** The reviewer is objecting to *spatial
aliasing on moving geometry*: a grass blade thinner than a pixel, whose staircase changes shape as
the wind moves it. FXAA filters that staircase, which the eye likes — and its per-frame edge decision
flips on pixels near its threshold, which is temporal alternation, which the detector scores as
*worse*. Supersampling resolves the blade, which the eye likes — and resolves more sub-pixel detail
into the frame, putting more edges near threshold, which the detector again scores as worse. The two
instruments measure different artifacts, and the fixes for one raise the other's number.

### The same root cause has a second, unmistakable form

The reviewer also reported, unprompted, that the hero mushroom's gill filaments *"look a little like
they're breaking apart or half rendered"*. They are: at 1080p those tendrils are thinner than a
pixel, so the rasteriser misses them entirely on some pixels and catches them on others, and a
continuous filament is drawn as a dashed line. At 2× they are continuous. Same cause as the grass —
**geometry below the sampling rate** — and it does not need a detector at all, because it is visible
in a single still.

## Decisions

### 1. FXAA stays, and "do not run FXAA at the offline tier" is refused

The mandate offered three options for FXAA's measured 19–21% share: a temporal term, threshold
hysteresis, or not running it offline. The third is dead. Removing FXAA makes the reported artifact
**31% worse** by the measure that tracks the reviewer, and the reviewer independently called t2 the
more distracting of the two without being told which was which. This also finally explains the
standing oddity that switching FXAA off makes neighbour-to-neighbour chroma noise 14.5% *worse*: it
is suppressing an artifact, not causing one, and its share of the temporal metric is the price.

The other two options survive, but as improvements to FXAA rather than replacements for it, and
neither is scheduled: the artifact they would address is not the one being reported.

### 2. The Glowmere deliverables supersample

ADR-212 built `--supersample`, measured it (720p chroma noise 2.816% → 2.227%, within 2% of native
1080p), and left it off by default so that "nothing changes for a render that does not ask". The
reviewer has now asked: *"worth 2.7× the render time? absolutely"*, against *"is t4 visibly calmer?
yes, much"*.

`supersample: 2.0` is set on `glowmere-valley-2` and `glowmere-stylized`. Valley 2 renders at
**1280×720**, which is the output the same person reported as looking worse earlier, and which is the
exact case ADR-212 measured. Two independent routes to the same remedy.

**The global default is deliberately not changed.** Every `--render` defaults to `tier: offline`, so
flipping it there would make every proof render 2.7× slower and move three test suites' pinned
sequence hashes. A setting that costs everyone should be chosen per deliverable, which is what a
project file is for.

### 3. The unattributed river residue is retired, not pursued

Priority 1's largest open thread was that ~60% of the river's flicker is reached by no authored
parameter, with three suspects inside `water.wgsl` — a shader this project has refused to edit on a
hypothesis since doing so cost three rounds on the anamorphic comb.

Asked to compare the river with its ripple term off, the reviewer said it was **calmer, and no longer
sparkled**. So the ripple accounts for the *visible* sparkle, while the detector says 41% of the
river's flicker survives it. That residue is real and it is not visible. The thread is closed as not
worth opening the shader for — which is the opposite of what I predicted before asking, and is the
more valuable of the two answers.

The river is not nothing: asked whether it bothered them, the reviewer said *"only when I look
straight at it"*. Supersampling improves it too (temporal −9%, spatial −13%) without touching the
authored animation, which is what an arm that switches the ripple off would have done.

### 4. Priority 1's attribution stands as measurement and is withdrawn as a basis for choosing work

Every number in it is a correct measurement of temporal alternation. None of them should be used
again to decide what to fix without a spatial measure beside it and a person having looked. The
ranking that matters — what a viewer objects to — was not the ranking being produced.

`tools/spatial_stats.py` ships that second measure, with the limit that matters stated in its own
header: **it cannot separate aliasing from detail**, so it is meaningful only between arms of one
view, never between scenes and never as an absolute bar. That is `--ab`'s rule, for `--ab`'s reason.

## Consequences

**§4 does not close.** The reviewer would not ship t1. The artifact is identified, its cause is
identified, and a remedy that already existed is now switched on for the scenes that were judged.
What remains open is whether supersampling is *enough* — that needs the same person to look at a
supersampled deliverable, which is a much narrower question than the one they were first handed.

**A rendered suite plus a person cost less than the measurement programme it corrected.** Priority 1
consumed multiple rounds, two withdrawn results and a harness rewrite. One afternoon of looking
overturned the use being made of all of it. That is an argument for looking earlier, not for
measuring less.

**What this does not say.** It does not say the temporal detector is wrong, or that temporal
artifacts do not exist in this engine — a moving camera has not been reviewed at all, and crawl on
static geometry is exactly what a moving camera would produce. It says that on *this* scene, at
*this* view, the artifact a viewer objects to is spatial, and the instrument in use ranked its
remedies backwards.
