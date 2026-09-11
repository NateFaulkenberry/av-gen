---
id: modulation/overview
title: Modulation
category: Modulation
summary: The wiring that makes a scene react — what a route is and what happens to a value on its way through one.
order: 31
tags: modulation, routes, signals, reactive
keywords: how do i make something react to the music; connect audio to a parameter; how do i make it pulse; audio reactive
related: modulation/routes, modulation/recipes, modulation/parameters, audio/signals
features: panel.modulation, subsystem.modulation
---

# Modulation

Modulation is the layer that connects a **signal** to a **parameter**.

```
signal -> route -> parameter -> engine property
```

A **route** names a source signal and a target parameter, and describes what to do to the value on
the way: shape it, scale it, and combine it with whatever is already there.

Routes live in the **Modulation** panel, on the **Routes** tab. The **Control** panel carries a
compact version of the same list — one amount slider per route, under a **Master gain** that
multiplies every route's output at once.

## The path of a value

Each frame, for each route:

1. The source signal is read from the bus.
2. If the route is **bipolar**, the value is mapped from 0…1 to −1…1.
3. It passes through the processing chain, in this fixed order:
   `gain → offset → curve → clamp → threshold → smoothing → envelope → remap`.
4. The result is multiplied by the route's **amount** and by the **master gain**.
5. It is combined with the parameter's current final value using the route's **operation**.
6. The result is clamped to the parameter's hard range.

## Operations, and the order they run in

| Operation | Result |
|---|---|
| `add` | current + modulation |
| `multiply` | current × modulation |
| `replace` | modulation |
| `min` | the smaller of the two |
| `max` | the larger |

Routes are grouped by operation and applied in this order: **replace, multiply, add, min, max.**
Within a group they run in the order you created them.

The reason is composition. `replace` establishes a value, `multiply` scales it, `add` offsets it,
and `min`/`max` bound the result. Because the grouping is by operation rather than by creation
order, two routes on the same parameter compose the same way however you happened to add them.

## Smoothing

The `attack ms` and `decay ms` controls are an asymmetric one-pole filter: `attack` governs the
rising edge and `decay` the falling one. Both are frame-rate independent, so a value that takes
200 ms to fall takes 200 ms to fall at any frame rate. Zero means instant. A new route is created
with 20 ms attack and 200 ms decay.

There is no "release" in a route. The falling edge is called **decay**. (`release` exists only
inside an envelope *source*, which is a different thing — see
[Modulation sources](help://modulation/sources).)

## Where to go next

- [Routes in detail](help://modulation/routes) — every control on a route and what it does
- [Recipes](help://modulation/recipes) — worked examples
- [Sources](help://modulation/sources) — LFOs, envelopes, noise and macros
- [MIDI and OSC](help://modulation/external-control) — driving parameters from hardware
