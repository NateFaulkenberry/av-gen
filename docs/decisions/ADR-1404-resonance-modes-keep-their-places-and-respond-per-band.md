# ADR-1404: Resonance modes keep their places, respond per band, and ripples fade out

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World's motion stability: `docs/prototypes/cymatic-world/07-brief-motion-stability.md`, folded into `08-brief-resonant-shape-language.md` §9)
- Code:
  - `ResonanceSpec::{layout, attack, release, rippleCount (per band), rippleFade}`, `ResonanceSource::breath`, `bandTime`.
  - The ripple selection in `evaluateResonance` (`src/spatial/resonance.*`).
- Tests:
  - `tests/unit/test_resonance_motion.cpp` ("A fixed layout keeps every mode where it is while `modes` changes", "Per-band response times set the rise and ring-down...", "A displaced ring fades out instead of vanishing...").
  - The GPU parity case runs with a layout and per-band times.
  - The hidden diagnostic `[.resonance-trace]` (`tests/unit/test_resonance_probe.cpp`) records a passage frame by frame.

## Context

The owner found Cymatic World's new motion (ADR-1403) twitchy next to a resonating plate. A frame-by-frame trace of Hyperborea at 44-56 s (`[.resonance-trace]`) put the twitch down to four separate causes. The oscillators' phases were clean (≤ 1.7% of their energy above 3 Hz).

1. **Ripple evictions.** The ring cap was full in 100% of frames, so each new onset dropped a living ring in one frame. There were 21 such drops in 12 s, up to 223% of the ripple layer's rms (14 times its median step).
2. **Strike steps through the limiter.** An onset's strike reached its peak in 30 ms, and the per-source limiter then dipped every other mode of that source in the same frame. One family stepped up to 27% of its rms in one frame (5.7 times its median).
3. **The layout sliding under a route.** Mode i sat at u = i/(modes − 1). A route on `modes` (the scene's build: intensity → modes) moved every mode along the dispersion axis, so frequencies, wavelengths and oscillator rates slewed. Modes that crossed a source's band boundary switched source mid-play: their centre jumped up to 54 m and their shape was redrawn. Petals stepped 75% of their rms in one frame.
4. **Fine detail moving fast for its size** (about half a wavelength a second). This is a scene setting, not engine: see the report.

## Decision

1. **`layout`.** When > 0, mode i sits at u = i/(layout − 1), whatever `modes` is. `modes` then only says how many of those fixed modes ring, from the lowest up, with the last fading in by its fraction. No mode is ever re-placed during play. 0 keeps ADR-1400.
2. **Per-band response.** `attack` and `release` are three times each (low, mid, high at u = 0, 0.5, 1, log-interpolated), in place of the persistence and buildup rule. A mode's attack is also the rise of an onset's strike, so the strike is a bounded two-time-constant impulse, e^(−age/τd)(1 − e^(−age/τa)), and never a 30 ms step. All zeros keeps ADR-1400.
3. **Ripples per band, fading.**
   - Each band keeps at most `rippleCount` rings (0-3).
   - A ring that a newer one displaces fades out over `rippleFade`, counted from the moment it was displaced, which is a pure function of the onset list.
   - Every ring tapers over its last life (3-4 lives).
   - A ring's rise follows its band's attack.
   - Up to 12 ripple rows.
4. **A source's `breath`** multiplies the resonator's breath for its modes, so a broad swell can morph slowly while a fine family shivers (default 1).
5. **Unchanged:** phase is still Ω·t with fixed Ω (fixed by the layout). A route on `breath` or `spin` would slew phase; the scene does not route them.

## Consequences

- Seek-exact and frame-rate independent as before: every addition is a function of the clock and the history.
- The diagnosed jumps are gone by construction. A displaced ring's one-frame fall is under 15% of the pop it replaces (tested). A strike's per-frame step at 60 Hz is bounded (tested).
- Files without these members behave exactly as before (ADR-1400 and ADR-1403). `rippleCount` is now per band, at most 3; ADR-1403's file carries the corrected wording.
