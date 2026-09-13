# ADR-119: attachment load/store bandwidth is not this frame's constraint

Status: accepted

## Context

Apple's guidance is that render-pass load and store actions "consume the majority of your app's
system bandwidth", and the scope contract (§21, §37) asked for a transient-attachment and
load/store audit on that basis. The scene pass writes five colour targets:

| target | format | bytes/px |
| --- | --- | --- |
| 0 HDR radiance | RGBA16Float | 8 |
| 1 normal + roughness | RGBA16Float | 8 |
| 2 velocity | RG16Float | 4 |
| 3 emission | RGBA16Float | 8 |
| 4 identifiers | R32Uint | 4 |

**The five-target layout was already measured not to be a tile-occupancy constraint** -- 32 B/sample
against Apple's 128 B/px tile budget -- so the only thing left to look for here is *bandwidth*, and
stating it that way matters: a result about bandwidth will otherwise be read as a result about tile
occupancy, which is a different question that already has a different answer.

## What was measured

**An arm that stops writing them.** `--ab auxstore` sets `StoreOp::Discard` on the four auxiliary
attachments. The fragment shader still computes and still writes them, so the arm isolates the
tile-to-memory store and nothing else: 24 bytes a pixel, **24.6 MB a frame** at 1280x800. The frame
it produces is wrong wherever post reads one of them, which is why this is an arm and not a setting.

Glowmere, 1280x800, three interleaved pairs of 120 frames, one process, GPU lock held:

| | GPU median | scene pass |
| --- | --- | --- |
| baseline | 13.30 ms | 10.68 / 10.75 / 10.75 |
| `no-auxstore` | 13.43 ms | 10.81 / 10.75 / 11.01 |

**-0.99%: not a result.** Per-pair deltas -0.13, -0.13, -0.13 -- consistently and slightly on the
*wrong* side of zero, which is what a null looks like when the quantum is 0.066 ms.

The arithmetic agrees and is worth writing down, because it is what makes the null credible rather
than merely unsurprising. The whole scene pass moves roughly 48 B/px -- 32 stored, 8 loaded for
target 0, 8 for depth in and out -- which is about 49 MB a frame. At this machine's memory bandwidth
that is on the order of 0.1 ms against a 10.88 ms scene pass: **under 1%, and therefore under the
noise floor by construction.** §4.2 excluded attachment bandwidth on a scaling argument; this
measures it directly and agrees.

## Decision

**No transient attachments, and no load/store restructuring.** Three specific things were evaluated
and are rejected on this evidence:

**Memoryless attachments.** The adapter *does* advertise `TransientAttachments` -- asked of the
driver rather than taken from documentation, and now logged at debug level by `gpu::Context`. It is
unusable here regardless: a transient attachment may never be loaded, stored or sampled, and all
five targets are sampled by a later pass -- emission weights bloom, velocity drives motion blur,
identifiers mask sharpening, linear depth feeds AO and the contact march. The one exception is
**target 1, normal+roughness, which nothing in the frame reads**: its only consumers are the
aux-debug views and external test accessors. It is still not transient, because it is still sampled
by those; the most it could be is a discarded store, and the `auxstore` arm just measured four
discarded stores at nothing.

**Eliding the background pass.** The pass clears the HDR target and stores it; the scene pass then
loads it back. When no enabled shader layer is staged as Background -- which is every scene in the
repository -- that is 8 B/px written to memory and read straight back for a constant. Implemented,
and then reverted: it is a third of the bandwidth the `auxstore` arm proved to be worth nothing, and
the pass's own timestamp reads 0.00 ms, i.e. below the counter's 0.066 ms quantum. A change that
cannot be measured is not an optimisation, and restructuring the pass order for it would trade
certain risk for uncertain nothing. Recorded so the arithmetic does not have to be redone.

**Framebuffer fetch and pixel-local storage.** See ADR-121; the adapter does not have them.

## Consequences

* The `auxstore` arm stays. It is the instrument that produced this answer, it costs nothing when
  not selected, and the answer has to be re-taken if the attachment set ever grows.
* **This result is specific to this resolution and this frame.** Bandwidth scales with pixels while
  the fragment cost that dominates here does not (§4.1: 56% of the scene pass was
  resolution-independent), so the ratio moves against bandwidth at higher resolution, not for it.
  The arm is there to re-ask the question rather than to have settled it forever.
* `normalRough_` being consumed by nothing on the normal frame path is left as it is and recorded as
  a finding. It is 8 B/px and one of the five targets a scene-pass pipeline must declare; removing it
  is a layout change with a real blast radius, for a saving this ADR has just measured at zero.
