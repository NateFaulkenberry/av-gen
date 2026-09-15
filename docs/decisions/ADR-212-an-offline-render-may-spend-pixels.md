# ADR-212 — An offline render may spend pixels a realtime one cannot

**Status:** accepted · 2026-09-15
**Follows:** ADR-137 (fixed render scale), ADR-147 / ADR-186 (the deliverable is rendered at the Offline tier)

## Context

Reported twice. First as a comparison — *"The 1920 looks much better than the 1280"* — and then, after
the intervening work, as a standing item: *"there is still an issue with 720 output videos."*

Measured on Glowmere through `--project`, one frame, three sizes, neighbour-to-neighbour chroma noise
(`tools/chroma_speckle.py`, which exists because mean saturation is blind to this):

| output | chroma step | pixels over 0.06 |
|---|---:|---:|
| 1280×720 | **2.816%** | **13.28%** |
| 1920×1080 | 2.190% | 10.11% |
| 2560×1440 | 1.864% | 8.48% |

Monotonic in resolution. Glowmere's foliage is dense enough that at 720p a grass blade covers less
than a pixel, and what the frame records is a sample of it rather than a picture of it. This is
undersampling, not a shader bug — and a 720p deliverable had no way to buy its way out of it.

## The thing that was already there

`QualitySettings::renderScale` (ADR-137) sizes the HDR target to `output × renderScale` and the tone
map resolves it back, and **it has always clamped to [0.25, 2.0]**. The entire upper half of that
range was unreachable:

* `--canvas-scale` refuses anything above 1;
* the settings slider stops at 1;
* the Offline tier pins `renderScale = 1.0`, under a comment reading *"an offline render takes no
  temporal or resolution shortcut"*.

That comment is right about going **down**. Going up is not a shortcut — it is spending more, which
is the one thing an offline render exists to do. The mechanism was built, tested and shipped; nothing
could ask for it.

## Decision

`RenderSettings::supersample`, default 1.0 (off, so nothing changes for a project that never heard of
it), range [1, 2]. `--supersample <f>` on the command line, serialised in the project, validated
rather than clamped: the ceiling is the renderer's own, and a setting that quietly means something
other than what it says is worse than one that is refused.

### Measured

720p, same second, same project, interleaved:

| | chroma step | pixels over 0.06 | render time, 1800 frames |
|---|---:|---:|---:|
| 720p native | 2.816% | 13.28% | 49.7 s |
| **720p at 2× supersample** | **2.227%** | **10.48%** | 111.6 s |
| 1080p native, for reference | 2.190% | 10.11% | — |

**720p supersampled is within 2% of native 1080p on the metric**, for 2.2× the render time. That is
the trade offered, and for an offline deliverable it is the right one.

## The bug in the first version, and why it was caught

The first implementation set `renderScale` *after* the tier and *after* `renderer_->resize(...)`. It
logged `supersampling at 2.00x -- the scene renders at 2560x1440`, and produced a **byte-identical
sequence hash**: `resize()` is what consumes `renderScale`, so the target had already been sized at
the output resolution and the setting reached nothing.

It was caught only because the arm was checked for non-vacuity before its number was believed
(ADR-182 — a probe must prove it established the state it measures). A log line saying a thing
happened is not evidence that it happened. The override now sits between the tier and the resize,
with the ordering constraint written where somebody would otherwise move it back.

## What this does not do

It is **offline only**. The realtime path keeps `renderScale` where its tier puts it, and the
interactive slider still stops at 1 — a 2× interactive canvas is a different decision with different
evidence behind it.

It does not address the *cause*. Sub-pixel foliage is still sub-pixel; supersampling pays for a
better sample of it. The cheaper structural answers — vegetation LOD that thins by projected size
rather than by distance, or a density that falls with output resolution — are untouched and
unmeasured.

And 2× is the ceiling the existing clamp allows. 4× would be better still on the metric and would
need `renderScale`'s clamp widened, which is ADR-137's decision to revisit rather than this one's.
