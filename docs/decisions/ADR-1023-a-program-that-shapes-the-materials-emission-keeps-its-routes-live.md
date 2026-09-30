# ADR-1023: A program that shapes the material's own emission keeps the routes into it live

**Status:** Accepted (Sonic Garden POC, art pass 2).
**Date:** 2026-09-30

## Context

ADR-179 settled that a material program which writes emission owns it: the material's `emissive` and
`emissiveColor` no longer reach the screen, so ADR-902's route auditor reports every route into them as dead
(`program-owns-emission`). ADR-904 then added the `materialEmission` input (the material's own emissive colour x
intensity x the instance's variation), so a program can *shape* the material's emission -- a Fresnel rim of it, a
vein mask over it, a tip gradient along it -- instead of asserting a colour of its own. For such a program the
routes into the material's emissive are exactly what decides the colour and the strength on screen.

The auditor was never told. Art pass 2 of the Sonic Garden gives its surfaces six programs of this kind (the rims,
veins, fissures and fracture lines are the program's; the colour and every note's swell of light are routes, blended
by the interpreter's families), and loading the project reported about 40 of its 380 routes dead that visibly
work. `--audit-routes` is how the Sonic Garden proves its routes live, so the false verdicts would send whoever
tunes it after the wrong thing.

## Decision

`MaterialProgram::emissionReadsMaterial()` follows the ops' data flow exactly as ADR-904's
`emissionReadsInstance()` does (the same walk, now one shared helper), but accepts only the `materialEmission`
input: `instanceEmissive` is the instance's variation, a per-channel ratio against the material's colour, and a
program that reads only that still replaces the material's emission.

`program-owns-emission` now fires only when the program writes emission **and** that emission does not depend on
`materialEmission`. Data flow, not presence: a program that loads `materialEmission` into a register its emission
never reads still owns its emission.

## Consequences

- The renderer is unchanged: this is the auditor agreeing with what the shader already does (`pbr_shade.wgsl`
  fills `materialEmission` from the object's emissive lanes, which the routes drive).
- `tests/unit/test_route_liveness.cpp`, "a program that shapes the material's own emission leaves it live
  (ADR-1023)" (`[adr1023]`): a Fresnel-shaped program keeps both routes live; an `instanceEmissive`-only program and
  one that reads `materialEmission` aside are still dead; the query sees a layer's emission reading what the base
  left in a register.
