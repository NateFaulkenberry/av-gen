# ADR-211 — A route you can click, and an answer that is complete

**Status:** accepted · 2026-09-15

## Context

The World panel's Inspector lists *Influences* — "Why is this moving? Timeline, routes and state
changes are listed here." Two problems, reported together:

> "the inspect tab is incredibly useful, but lets make it so the routes are clickable, and when you
> click on one it focuses that route for you in the modulate panel"
>
> "also verify the inspector tab is truly capable of displaying all modulation on all sources and all
> destinations"

## Clickable routes

A route bullet is now a `Selectable` that sets `WorldPanel::focusRouteSource` / `focusRouteTarget`.
The Modulation panel consumes the request: opens that route, scrolls it into view, tints its header,
and clears the request so it fires once.

Identified by **(source, target), not by index.** Both panels redraw every frame from the same live
route list, and an index is stale the moment a route above it is deleted — which is a click landing
on the wrong route, silently. Cleared by the consumer rather than by a timer, so a click made while
the Modulation panel is closed is still waiting when it opens.

`Selectable` rather than a button because the whole row should be the hit target. A bullet list you
have to hit a five-pixel widget inside is not clickable in any sense a person cares about.

## The completeness claim

The second request is the harder one, and the answer was **no**.

The way to check a claim like "everything that modulates this" is not to read `influencesOf` and
agree with it. It is to enumerate what can actually write a parameter's **final** value, which is a
grep:

| writes a final | the Inspector's answer |
|---|---|
| `params/modulation.cpp` | `Kind::Route` |
| `params/timeline.cpp` | `Kind::Timeline` (and `Cue` / `State`, which reach a parameter through a preset the timeline applies) |
| `params/parameter_set.cpp` | resets finals — establishes the base rather than modulating it |
| `ui/edit_history.cpp` | an undo: a user action, not an influence on a running scene |
| `entity/entity.cpp` | **nothing — this was the gap** |
| `stage/staging.cpp` | partly; see below |

`entity/entity.cpp` folds a behaviour's `MotionOffset` straight onto its node's position, rotation
and scale. So the one panel whose entire job is answering "why is this moving?" answered **"nothing
modulates this object; its parameters are static"** about a character visibly walking across the
valley. `Kind::Entity` now lists the entity and each of its behaviours by kind and name.

Matched on the parameter path rather than by asking an entity what it drives, so a behaviour that
happens to write nothing this frame is still listed: the question is "what can move this", not "what
moved it in the last sixteen milliseconds".

## The guard, and what it caught immediately

`"every writer of a parameter final is a kind the Inspector can name"`, in `test_repo_hygiene.cpp`.
It greps `src/` for writers and fails on any that is not in a known list, so a writer added later
cannot quietly reopen this hole. It is a grep rather than a link-time check because `influencesOf`
lives in `world_panel.cpp`, behind ImGui, which the unit-test binary does not link — and asking
`influencesOf` what it covers would be the function under test answering a question about itself.

It failed on its first run, on `stage/staging.cpp` — the decision layer from ADR-210, merged the
same day. That is the test doing its job on the day it was written.

## What is not done

**`stage/staging.cpp` is only partly covered.** A scenario drives a body through
`entity::DirectorMotion`, so a staged *entity* is named by `Kind::Entity` — but a scenario also
writes nodes that are not entities, such as the tractor beam's visibility, and those still show
nothing. The honest answer is a `Kind::Staging` naming the scenario and the role it bound. Recorded
rather than claimed.

**Destinations are scoped to the selected node's own prefix.** The Inspector walks parameters under
`nodes/<name>/` and nothing else, so a route onto a shared material, or onto `scene/`, changes how
the object looks without appearing in its list. That is a real limit of "all destinations" and it is
stated here rather than left for somebody to discover.
