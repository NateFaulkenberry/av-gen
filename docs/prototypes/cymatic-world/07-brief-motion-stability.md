# Cymatic World — Motion Stability and Resonant Continuity Pass

(Owner brief, received 2026-10-16, saved verbatim. Applies on top of the motion overhaul, 05-brief-resonant-simulation-overhaul.md.)

The new geometry is moving in the right direction, but some motion still looks twitchy compared with the coherent behavior of a resonating cymatic plate.

Perform a focused investigation and correction of the motion system. Do not redesign the scene's appearance, replace its palette, or broadly reduce animation speed.

## 1. Diagnose before tuning

Capture the same audio passage while visualizing:

- Individual mode contributions.
- Mode amplitude envelopes.
- Mode phases or temporal oscillators.
- Total displacement.
- The final rendered surface.

Temporarily disable color and emission modulation to isolate geometry.

Identify whether twitching originates from noisy audio features, discontinuous mode selection, phase resets, excessive high-order displacement, unstable numerical integration, frame-rate dependence, or another cause. Do not assume all instability has the same source.

## 2. Preserve modal continuity

- Do not regenerate spatial modes during playback.
- Do not reset phase when a mode's amplitude changes.
- Avoid abrupt switching between mode configurations.
- Smooth changes in mode weights and spatial parameters.
- Preserve continuous temporal phase.
- If frequencies or mode parameters change, ensure their evolution does not introduce phase discontinuities.
- Keep the simulation deterministic for a given input and initial state wherever practical.

## 3. Add appropriate temporal response

Use separate, configurable response times for low-, mid-, and high-frequency excitation.

Smooth the control envelopes rather than indiscriminately smoothing the finished geometry.

Use asymmetric attack and release behavior where it helps the response feel musical. Sustained excitation should build naturally; after excitation decreases, modes should decay according to their damping rather than immediately stopping.

Ensure transient excitation creates a clear impulse response without introducing an abrupt, sustained change in displacement.

If the scene uses a numerical wave simulation, verify time-step stability and appropriate damping. Avoid frame-rate-dependent integration.

## 4. Control fine-scale twitching

- Keep broad deformation expressive and visible.
- Reduce excessive high-order displacement before reducing all movement.
- Bound transient contributions.
- Filter spatial detail that cannot be represented cleanly at the current screen resolution.
- Prevent multiple high-frequency components from overwhelming the large-scale structure.
- Check whether rapid normal changes or temporal aliasing are making the surface appear more unstable than its actual displacement.

Do not eliminate useful fine motion or turn the world into a slow, uniform pulse.

## 5. Tune for coherent musical behavior

Test quiet passages, sustained bass, kick-heavy material, snare-heavy material, dense mixes, and transitions.

For each case, assess:

- Whether broad structures remain visually legible.
- Whether excitation builds and decays naturally.
- Whether individual transients produce coherent responses.
- Whether the surface exhibits meaningful changes without degenerating into noise.
- Whether fine motion complements rather than obscures the major forms.

Compare captures before and after at normal playback speed, not just frame by frame.

## 6. Make measured changes

Expose a small number of useful tuning parameters through the existing scene configuration or modulation system, rather than creating an elaborate new control panel.

Keep the implementation compatible with the existing AV Gen architecture and live performance budget.

Report the diagnosed root cause, the changes made, and before/after captures. Do not report success merely because the animation became slower.

**Acceptance criterion:** the surface should retain energetic, audio-reactive movement while feeling like one continuous resonant medium. It should vibrate, accumulate excitation, and settle—not twitch in response to every fluctuation in the audio.
