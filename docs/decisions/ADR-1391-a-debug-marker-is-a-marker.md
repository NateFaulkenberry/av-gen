# ADR-1391: A debug marker is a marker -- point sizes are world-space diameters

**Status:** Accepted (the owner's "yellow blobs", 2026-10-15).
**Date:** 2026-10-15

## Context

The Character Animation Lab opened through Engineering Labs > Animation showed every character inside a large pale
yellow blob. It is the skeleton overlay: `DebugVertex::size` is a world-space diameter (`shaders/debug.wgsl`), but the
header said "pixel size" and the joints were drawn at `pointSize * 0.6` -- a 1.8 m disc on each of ninety joints of a
1.7 m alien. Their union is the blob. The transform trail (1.5 m), IK chain ends (2.7 m), targets (3.6 m) and pelvis
marker (3.3 m) had the same mistake; the Lighting Lab had found it for its own marker only (`kMarkerScale`).

## Decision

Joints 6 cm, trail samples 9 cm, motion markers 10-14 cm at the default point size. `DebugVertex::size` documents the
unit. The overlay stays on in the Animation and Character lab profiles, where the skeleton is the subject: it is a
deliberate diagnostic that was drawn at the wrong size, not a visual to hide.

## Consequences

`tests/rendering/test_debug_draw.cpp` bounds joint and trail marker sizes. The shadow cascade centre (1.8 m) is left as
it is: cascades span tens to hundreds of metres.
