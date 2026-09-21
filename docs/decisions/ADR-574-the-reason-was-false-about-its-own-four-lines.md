# ADR-574: the reason for leaving it unreachable was false about its own four lines

Status: accepted. Date: 2026-09-21. A correction, arising from ADR-573's audit.

## Context

ADR-573 found two volumetric controls that reached the picture and had no parameter -- no panel
row, no automation, no modulation, editable only by hand. It named a third, `fogHeightAmount`
(ADR-058's surface/volume coupling), and **left it alone on the grounds that its omission was
documented as deliberate**, flagging it for its owner to decide.

That was the wrong move and the reason it was wrong is one this project has written down: **a
stated reason is not evidence** (ADR-385). Escalating a question whose answer a grep settles spends
someone's attention on something measurable. So it was measured.

## What the measurement found

The recorded reason, verbatim:

> `// ADR-058: the distance fog's share of the mist layer and the styled hemisphere travel with`
> `// the rest of the atmosphere; nothing about them is animated, so they are copied, not picked.`

**It is false about the four lines it sits on.** Immediately beneath it:

| field | the comment says | the code does |
|---|---|---|
| `fogHeightAmount` | copied | copied |
| `styledSkyAmbient` | copied | **picked, from a registered parameter** |
| `styledGroundAmbient` | copied | **picked, from a registered parameter** |
| `styledAmbientFloor` | copied | copied |

Somebody gave the styled hemisphere parameters and left the reason saying they had not. Half the
claim describes code that has not existed for some time.

**And the surviving half is circular.** Nothing animated `fogHeightAmount` because nothing *could*:
with no parameter there is no keyframe, no route and no row. A reason that is true only because of
the thing it justifies is not a reason.

The two substantive questions ADR-573 should have asked, answered:

1. **Is ADR-058's coupling live?** Yes, and this was checked rather than believed -- five
   unreachable things have been found in this codebase this week. `shaders/common.wgsl:489` reads
   `frame.fogHeight.z`; `scene_renderer.cpp:2640` uploads it; `test_volume_gpu.cpp` exercises it at
   0 and 1 and asserts a difference; **9 of 90 shipped scenes set it above 0**, every one by hand.
2. **Would exposing it make two controls for one quantity?** No. It is the only control for how
   much of the mist layer the surface fog integrates.

So the reason is void, the feature works, and nothing is duplicated. Exposing it is a **correction**,
not a judgement call.

## Decision

`fogHeightAmount` is a registered parameter, drawn in the Environment panel as *"Surface fog
follows layer"*. The default and the serialisation condition are unchanged, so a scene that never
mentioned it round-trips byte for byte.

**The save writes the parameter's base, not the authored setting**, and that was a second defect
one line away: `environment["fogHeightAmount"] = v.fogHeightAmount` would have discarded every
change made through the row this ADR adds. *A control you can move and cannot keep is not a
control* -- the same half of "reachable" ADR-573's case asks about, which is why the case asks both.

**The false comment is replaced** with what the code does and why.

### What is NOT done, with the measurement rather than the question

ADR-058 shipped four controls and **not one of them is on a panel**:

| | parameter | panel row |
|---|---|---|
| `fogHeightAmount` | **now** | **now** |
| `styledSkyAmbient` | yes | **no** |
| `styledGroundAmbient` | yes | **no** |
| `styledAmbientFloor` | **no** | **no** |

The remaining three are the styled hemisphere ambient -- lighting, not fog, and not this brief's.
They are recorded here with their state measured rather than raised as a question, because the
answer to *"is this deliberate"* is now known for the comment that covered all four: it was not.
Two of them are automatable and invisible, which is its own odd state: a control an artist cannot
find but a route can drive.

## Consequences

- **Break demonstrations, one for each half of "reachable".** Restoring the copy: the environment
  holds 0.60 where the parameter was moved to 0.35 -- registered, drawn, dropped. Restoring the
  authored value in the save: the file holds 0.60 where the parameter was moved to 0.35 -- moved
  and not kept.
- **The rule this batch has produced, stated on its own rather than as a note on ADR-421.**
  ADR-421: *a control that does nothing teaches an artist that the system is broken.* Its converse
  is worse: **a capability with no control teaches an artist that the system cannot do it** -- and
  nobody ever files a bug about a feature they do not know exists. A dead knob wastes time; an
  invisible capability is never found at all. Both are "declared and disconnected"; they differ
  only in which end is missing.
- **The check is cheap and belongs in a review.** Two greps over
  `Composition::applyParameters` -- assignments that read a parameter, and assignments that copy --
  and a third over the panel for the paths the first list registers. Three lists, and anything in
  the second or missing from the third is unreachable by somebody.

## Revisit when

- **The styled hemisphere's three controls are given a home.** Not fog; measured above.
- **Anything else is left undone on a recorded reason.** The reason is a claim, it has a date, and
  it can be false about the code beneath it. Read the code, not the comment.
