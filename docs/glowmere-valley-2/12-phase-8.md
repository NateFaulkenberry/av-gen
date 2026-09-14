# Phase 8 — the Auto-director (§9)

The rename, the shot modes, the settings panel, and the one engineering change that makes a
continuous take continuous.

---

## 1. The continuous shot: position was never the problem

Phase 1's audit found that `DirectionBrief` already carried a `continuous` flag, defaulting true and
invisible to every user, and read then that **position was already C⁰ and velocity was what broke**.
That read was right and this is the fix.

`ease(t, in, out)` smoothsteps whichever ends ask for it, and **both ends ask by default**. So every
shot arrived at a section boundary at zero velocity and left the next one from zero. Pinning the
position made the join continuous *in a still* and read as a cut *in motion* — a camera that stops
dead and accelerates away has cut; it has just done it without a frame of black.

In `ContinuousShot` mode the interior joins no longer ease: a shot that continues from another does
not ramp in, and the one it continues from does not ramp out. Three lines.

**Only the interior joins.** The film still starts and ends at rest — a take that begins mid-move and
ends mid-move is a clip, not a film — and that is asserted rather than assumed.

### What is *not* claimed

Velocity **magnitude** still steps at a join: shot A ends at `|endA − startA| / durationA` and B
begins at its own ratio, and matching them would mean retiming the shots, which is exactly the thing
that must not happen because the cuts belong to the music. So this is C⁰ in position and *non-zero,
continuous in direction* in velocity — not C¹. Arc-length reparameterisation is likewise not done;
within a pinned shot the path is a lerp plus a sine bow, so speed is nearly uniform already and the
bow's contribution is bounded.

## 2. Shot modes, as a real mode

`DirectorMode::{ContinuousShot, EditedSequence}`, replacing the invisible boolean.

- **Continuous shot** — one uninterrupted take; the camera travels through the world and around its
  subjects without editorial cuts.
- **Edited sequence** — a cut list; each shot composed independently, cutting between compositions
  and subjects.

Tested as genuinely different films from one structure: same cuts (the music decides those),
different paths, and in edited mode no shot is pinned and every shot eases at both ends.

## 3. The rename

"Direct to Music" → **Enable Auto-director**. "Hand Camera Back to the Viewport" → **Disable
Auto-director**. "the camera director" → "the Auto-director" through tooltips, status lines, logs,
help topics and menu references. `docs/camera-director.md` → `docs/auto-director.md`.

**Three things called a director were deliberately not renamed**: `app::WorldDirector` (artistic
knobs → macros, and it owns the project key `"director"`), `seq::Director` (the sequencer's
installer), and `entity::Authority::Director`. The internal `camera_director.*` filenames and
`DirectorState` keep their names too. The brief asks for the *feature* to be renamed, and a rename
that reached into three unrelated subsystems would be a worse outcome than the old name.

`tests/unit/test_help.cpp` asserted the old menu path and was updated — the test doing its job.

## 4. The panel, and what it deliberately omits

`AutoDirectorSettings` carries seven fields, every one of which changes the film: mode, three shot
timings, two focal lengths, and the seed. It validates rather than clamps, and the panel shows the
refusal in place instead of logging it.

**Three controls a panel would obviously want are absent, and that is the point:**

| not exposed | why |
|---|---|
| `CompositionProfile::framing`, `headroom` | stored, serialised, and read by no geometry function. They *look* like framing controls and are dead |
| `HeroPoint::preferredCameraElevationDegrees` | authored per hero, never read by the director |
| `Shot::speed` | real, but only through `Sequence::retime()`, which the director never calls — a "camera speed" slider would move nothing |

They are documented as future extensions rather than shipped as inert sliders. A knob wired to
nothing is worse than a missing knob, because it spends the user's trust.

Changing a setting while the camera is already directed re-cuts immediately, rather than waiting for
somebody to find the menu item again.

### What I cannot verify

**I cannot see ImGui.** The panel compiles, the settings are plumbed, and the *behaviour* is tested
headlessly — shot-length bounds change the number of shots, mode changes whether shots are pinned,
the seed changes the cast, the wide lens reaches the baked keys, and invalid settings are refused.
None of that is a claim that the panel looks right or that the controls are laid out sensibly.
Somebody who can see it should.

## 5. What §9 still does not have

- **Per-hero cinematic regions** — approach direction, orbit arc, entry and exit transitions. The
  brief's §9 asks for them; `HeroPoint` carries stand-off and aim offset and nothing about approach.
- **Lateral camera clearance.** `ClearanceField` is vertical-only and only ever raises
  (`04-plan.md` R1), so a continuous take that dollies to a mushroom and arcs around it is still
  lifted *over* the thing it should be circling. This remains the largest real risk in §9 and is
  untouched.
- **Camera diagnostics** — path, target, hero volumes, speed, progression.
- **Arc-length parameterisation**, §1.

ADR-158's aim-follow already gives a continuous take around a *moving* hero what it needs, and it
landed on main before this phase — so the one part of §9 that looked hardest was already done.
