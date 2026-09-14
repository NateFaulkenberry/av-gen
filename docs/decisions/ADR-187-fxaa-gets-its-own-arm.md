# ADR-187: FXAA gets its own isolation arm

**Status:** Accepted
**Date:** 2026-09-14

The Priority 1 re-measurement found the post-process antialiasing to be the largest identified
source of temporal instability in Glowmere — about 20% of the flickering area at two views. It found
that through a *scene* arm (`post.antialias = 0`), because the only pass-level arm available was
`--disable post`, and that one removes the tone map along with everything else: the frame's transfer
function moves and every threshold in the detector moves with it.

So the finding was real and the instrument that produced it was borrowed. `PassToggles::antialias`
makes it a first-class arm — `--disable fxaa`, and a **no FXAA** checkbox beside the others in the
forensics section — which removes exactly one filter and nothing else.

Two things this buys beyond tidiness. A person asking "are these crawling edges FXAA?" can tick a box
and look, rather than editing a scene file and having to put it back. And the two routes to the same
number now agree exactly: `--disable fxaa` and `post.antialias = 0` both report −21% at the river
view, on identical pixel counts, which is the check that the arm is the arm it claims to be.

How *much* antialiasing there is remains `post/output/antialias` in the scene, where it belongs. This
is the on/off for looking, not a second place to author the amount.
