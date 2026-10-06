# The Astral Forge: Parameter and State Model

Brief §20 item 4. This is what the prototype implements (`prototypes/astral-forge/`). It is also the
contract that a production version would expose as scene parameters, routes and MIDI.

## 1. Layers of state

```text
AUDIO ANALYSIS (offline, whole song; production AnalysisTrack + detectStructure)
   spectrogram 64 bins · band envelopes · kick/snare/hat onsets · beat grid · sections (+ repetition groups)
        │
        ▼
CONDUCTOR (CPU, pure function of t given the analysis)                  ── per frame, ~60 floats
   coherence C, dC/dt · archetype A→B + morph · fold vector · temper · breath · mass
   face flash · collapse impulse · light rig · camera behaviour
        │  (one uniform block, < 1 KB/frame)
        ▼
LATENT FIELD (GPU, analytic, never drawn)        PARTICLES (GPU, stateful, fixed 60 Hz)
   latent(p) = archetype SDF ∘ fold warp            pos, θ (binding threshold), vel, heat, normal, binding
   parts: eyes / mouth / plate / appendage          forces: spring to the latent projection × binding,
   engraving line fields in the same domain                 curl × (1 − binding), collapse impulse,
                                                            tangential flow, containment
        │                                                  │
        └───────────────────┬──────────────────────────────┘
                            ▼
DENSITY FIELD (GPU, rebuilt every frame from the particles)
   u32 fixed-point splat (order-independent) → blur → rgba16f 3-D texture (density, heat) + coarse occupancy
                            │
               ┌────────────┴────────────┐
               ▼                         ▼
     IMPLICIT SURFACE                 FLAKES
     iso of density,                  compute splat, glint shading,
     sharpened to the latent by S     depth-tested against the surface
               └────────────┬────────────┘
                            ▼
             MATERIAL / LIGHT (bands, temper film, grating, heat) → bloom → tonemap
```

## 2. The conductor's outputs (the entity state)

| Parameter | Range | Meaning | Drives |
|---|---|---|---|
| `coherence` C | 0-1 | how strongly matter is held to the latent anatomy | binding count, spring stiffness, chaos amplitude, smooth-union radius, sharpening |
| `dC` | per s | rate of change | collapse impulse when strongly negative |
| `sharpness` S | 0-1 | how far the density surface is pulled to the latent zero set | default `smoothstep(0.55, 1.0, C)`; "terrifyingly precise" at 1 |
| `archetypeA`, `archetypeB`, `morph` | ids, 0-1 | latent = mix(A, B, morph) | form |
| `fold` | vec4 + vec4 | twist, depth swap of one eye, mouth tunnel, sphere inversion, bend | the dimensional distortion layer (latent, engraving and particle targets together) |
| `temper` | 0-1 | oxide film thickness 0 → 420 nm | colour |
| `heat` impulse | 0-1 | added to released particles; decays at ~1.5/s | sparks, crack glow |
| `breath` | −1-1 | slow swell of plate and blades | low mid |
| `mass` | 0-1 | particle density weight and scale | sub |
| `faceFlash` | 0-1 | extra binding for eye and mouth roles only | transients |
| `flow` | units/s | tangential migration of bound matter along the engraving | mid |
| `shimmer` | 0-1 | flake tumble rate, groove crawl speed, band flicker | high mid / high |
| `rig` | angle, sweep | orbit of the light bands, a sweep position for a snare | phrases, snares |
| `choirSync`, `choirMerge` | 0-1 | small faces' independence → alignment → one face | TEST 04 |

### Coherence semantics (brief §3)

| C | Particles | Surface |
|---|---|---|
| 0.00 | all free; curl at full amplitude | none; density below threshold everywhere |
| 0.15 | eye roles (θ ≈ 0.1-0.5) begin to bind weakly | blobs flicker around the eye positions |
| 0.30 | eye and mouth roles mostly bound | **two eyes over a mouth: the face is first read here** |
| 0.50 | plate roles binding; filaments stream between features | incomplete plate patches; `k` (smooth union) still large |
| 0.70 | most plate bound; appendages starting | recognisable entity; blobby but continuous |
| 0.90 | appendages bound; chaos low | sharpening on; engraving resolves |
| 1.00 | all roles bound; the spring is at its stiffest | the surface is pinned to the latent anatomy: precise |
| 1.00 → 0.22 in ~0.1 s | released particles fly along the normal at a speed ∝ release rate; heat injected | sheets tear (density falls below the threshold), then dust |

Binding per particle: `b = smoothstep(θ − 0.08, θ + 0.08, C + flash·[role ∈ eyes, mouth])`.

Role mix (by particle hash):

| Role | Share | θ range | Latent part it is attracted to |
|---|---|---|---|
| eye | 9% | 0.08-0.45 | eye spheres, ring pupils |
| mouth | 7% | 0.15-0.55 | lips, teeth, the inner face |
| plate | 42% | 0.30-0.95 | face plate, brow, nose ridge, body |
| appendage | 24% | 0.45-1.00 | wings, horns, tendrils, gyro rings (archetype specific) |
| drifter | 18% | never | none: meta-scale dust, weak containment only |

## 3. Audio → state (brief §12), as implemented in TEST 06

| Musical signal | Source | → State | Why it is structural, not amplitude |
|---|---|---|---|
| Section boundary | `detectStructure` (Foote novelty) | archetype change; the camera REVEALs | form changes when the *music* changes, not when it gets loud |
| Repetition group | `detectStructure` | the same group summons the same archetype | the chorus always brings back the same god |
| Phrase (16 beats from the beat grid) | beat tracker | a coherence cycle: it builds through the phrase and collapses at the phrase's downbeat if the next phrase opens with a strong low onset | emergence follows musical sentences |
| Kick (low onset) | band onsets | mass pulse (density weight), breath | weight, not scale |
| Snare (mid onset) | band onsets | face flash (eyes and mouth over-bind, 250 ms); a light band sweeps | the "that's a face" moment lands on the backbeat |
| Sub envelope | band envelope | `mass`, low-frequency deformation of the plate | |
| Low-mid envelope | | `breath`, blade opening | |
| Mid envelope | | `flow`, plus up to 0.12 of coherence | filaments migrate faster when the guitars are dense |
| High-mid / high envelope | | `shimmer`: flake tumble, groove crawl, band flicker | |
| Spectral centroid of the section | | `temper` target | brighter sections temper toward violet and blue |
| Strong broadband transient after a build | onset strength + novelty | **collapse**: C → 0.2 in 0.1 s; heat; band strobe | the violent moment is a musical event |

The conductor is a pure function of `t` *given the whole-song analysis*, so a render is exactly repeatable
and any frame can be computed independently of play history. (The particles are not: see §5.)

## 4. GPU budget model (1920×1080, M2 Max)

Measured in `04-architecture.md`. The model:

- **Particle step:** about 5 latent evaluations per particle (a tetrahedral gradient plus one), so it is
  linear in N and in latent complexity.
- **Density splat:** 8 atomics per particle; the blur and resolve pass is linear in grid cells (`res³`).
- **Surface:** per pixel, coarse-occupancy skipping plus a short march in occupied cells. It is
  dominated by covered pixels × steps.
- **Flakes:** per particle, a footprint of 1-49 px plus streak samples.

## 5. Time and seek

- The conductor and the latent field are pure functions of `t`.
- The particles are stateful on a fixed 60 Hz step. Their memory is short: the spring damping forgets
  history in about 2 s, and a collapse's flight in about 3 s. The prototype therefore *pre-rolls 6 s* before
  any requested start. A seek is then visually equivalent to play (not bit-exact).
- In production this would be the same contract as ADR-360's particles (relaxed seek), unless the
  particles move into `Simulation` with checkpoints (ADR-1119). See `04-architecture.md` §Production path.

## 6. Live and MIDI (the production contract this implies)

| Control | Type | Why it is a performance control |
|---|---|---|
| coherence override / bias | fader | the central parameter, the "summon" knob |
| collapse | trigger | the violent moment, on demand |
| archetype A/B + morph | selector + fader | |
| fold amount (4 kinds) | 4 knobs | dimensional distortion |
| temper bias | knob | colour |
| face flash | trigger | |
| rig rotation | knob | light |
| camera behaviour | selector | |

All of these are uniforms: no rebuild, no pipeline change, and safe on a fast MIDI knob.
