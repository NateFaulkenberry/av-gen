# Motion matching in AV Gen

The Phase C final report (§89). It describes what exists in the tree on 2026-09-21, how it fits
together, what was measured, and what is still missing. The working record, with every
measurement and every wrong turn, is `docs/design/procedural-character-motion.md` (Phase C
sections). The decisions are ADR-606 to ADR-624 and ADR-650. This document is the map; those are
the evidence.

---

## 1. Architecture

```
MotionRequest (intent, world space)          entity::MotionRequest
  + body state (bodyFacing, bodyVelocity)
      │
      ▼
MotionChain ── MatchMotionProvider ──fallback──► ClipMotionProvider        ADR-541, ADR-623
      │              │  advance (simulation, every step, replayed on seek)
      │              │  pose    (presentation, once per drawn frame)
      │              ▼
      │        MotionDatabase (built offline or at load, immutable)        scene/motion_database
      │              ▲
      │        MotionPack (retargeted, analysed clips + provenance)        scene/motion_pack
      ▼
Base pose (body frame) → Phase B layers (feet, look, reach) → inertialized blend → final pose
```

- **Opt-in per character**, default off (ADR-623): `"motionMatching": {joints, contacts?,
  trajectory?, pack?, clips?, weights?}`. No shipping scene sets it. A Glowmere state digest shows
  that behaviour with the key absent is unchanged, and a control arm with the key present differs.
- **The simulation owns where the body is and which way it faces** (§71). The matcher supplies a
  pose in the body's own frame. Travelling and turning clips are posed with their travel removed;
  in-place clips are posed exactly as authored.
- **Behaviour is not the matcher's business** (§34). It receives motion requirements. Phase D decides
  what the character wants.

## 2. MotionDatabase and MotionSample

Parallel arrays, sample-major (§5/§6), built once and never mutated:

| array | per | what |
|---|---|---|
| `features` | sample × dimension | standardised feature vector (zero mean, unit spread per dimension, §9) |
| `sampleClip`, `sampleTime`, `samplePhase` | sample | where the sample is, and its gait phase |
| `sampleTags` | sample | `MotionTag` bits: locomotion, idle, walk, run, turn, airborne, cyclic, travelling, one-shot, **terminal** |
| `sampleNext` | sample | the continuation, so carrying on is an array walk (§29); a loop wraps |
| `sampleRoot` | sample × 3 | travel-joint position and facing in clip space (§71) |
| `clipTravels` | clip | whether a clip's travel is real (so its pose is rebased) |
| `mean`, `scale` | dimension | the standardisation |

A `MotionSample` is an index into these arrays. Its clip and time give the pose; its feature row
describes it to the search.

## 3. Feature representation (§7, §8)

Everything is expressed **in the body's own frame**. The frame is the travel joint's position and
facing. Facing comes from, in order of preference:
- a heading track the clip carries (§21's variants);
- for a travelling clip, the travel joint's facing relative to rest, averaged over one second
  (`facingWindow`), because a pelvis sways;
- for an in-place clip, the same measured relative to the clip's own mean, so the posture
  cancels and a turn on the spot remains.

| group | dims | content |
|---|---|---|
| joint position | 3 per feature joint | body-relative, body frame |
| joint velocity | 3 per feature joint | body-relative, body frame |
| trajectory position | 2 per horizon | where the body will be (0.2 / 0.4 / 0.6 s, §24) |
| trajectory facing | 2 per horizon | **which way it will face** (extraction v6) |
| root velocity | 3 | the body's velocity now |
| phase | 2 (optional) | gait phase on a circle |
| contact | 1 per contact joint (optional) | planted flag |

**In-place content carries implied travel** (`impliedTravel`): the opposite of its planted feet's
velocity. Without it every Glowmere walk read as standing still. **A look ahead past a clip's end
continues it**: a loop wraps into its start, and anything else extrapolates at its final velocity.
`kMotionFeatureExtractionVersion` (now 7) is part of the schema digest, so any change to how a
feature is computed invalidates stored databases.

## 4. Trajectory representation and query construction (§24, §25)

The query's pose half is the current sample's own feature row: an array read. The intent half comes
from the request:
- When the request says how the body moves now (`bodyVelocityKnown`), the trajectory is
  **predicted** with `predictTrajectory`, stepping the motion controller under
  `MatchSettings::limits` and turning the request by `desiredTurnRate` over the horizon. That is
  what makes a start, a stop and a curve choosable (§48's control).
- Otherwise the asked-for velocity is held.

Positions are divided by the node's scale, because a request is in world metres and a database is
in the asset's units. Facings are expressed relative to `bodyFacing`.

## 5. Candidate filtering (§14)

Tags remove candidates before scoring: one AND per sample. The matcher always rejects `Airborne`
(for a grounded body) and `Terminal`. Terminal samples are the last stretch of a non-looping clip,
whose future is extrapolated; a matcher that could choose one froze on it (§46).
`motionMatching.clips` is the per-character allow-list, for corpora without authored tags.

## 6. Cost function (§10, §45)

Squared difference per dimension, times that dimension's group weight (`motionFeatureTerm`, the one
function every search and the explainer call), plus:
- a **continuity** penalty for not carrying on (flat across clips; graded continuity was measured
  and rejected, §11);
- a **transition** penalty for leaving the current motion family (§12).

The seven group weights and the two penalties are configurable per character in a **versioned**
block (`motionMatching.weights`, version 1; an unknown version is refused).

## 7. Search strategy (§15, §16, §54)

**Linear scan with an early out**, over contiguous arrays. It is exact, and on real data it is
faster in wall time than §16's strided two-stage plan. Three approximate structures were built and
measured (`scene/motion_search_index`):
- **PCA** (top-8 components as stage one): 1.6× faster at 434k samples, 100% recall.
- **VQ** (k-means cells, inverted lists): 5–8× faster, 100% recall on the §20 queries.
- **KD-tree** (exact branch and bound): *slower* than linear; it visits 36–79% of a 33-dimension
  corpus. Rejected.

None is adopted. Under §54's rule the exact scan is not too slow at real scale: about 9 µs on the
Glowmere corpus. All three finish with `scoreMotionCandidates`, the shared exact scorer.

## 8. Continuity (§26–§29, §32)

- The matcher searches at most every `searchInterval` and never within `minimumContinuation` of its
  last search; between searches it carries on **by time** (§26/§73).
- A switch must beat carrying on by a margin that is a fraction of the measured cost spread (§28).
- Every switch is inertialized at the provider seam, with up to three nested blends (ADR-613).
- A scrub reproduces a play exactly: a seek replays the provider memory on every step (ADR-623).

## 9. Phase and contact integration (§30, §31)

Phase and contact terms exist and are weightable. **On the Glowmere corpus they measure as inert**,
because every clip is a steady in-place cycle. **On 100STYLE's transitions contacts are active**:
−3.2% foot jump per transition, but +24% over a run, because they switch more often. Phase's
benefit is unmeasured: the retrieval metric cannot see it, and the attempt was retracted.

## 10. Fallback (§35, ADR-623)

The chain falls through to the clip provider for each of these, and each is tested with a healthy
control arm:
- no database;
- an empty database;
- a filter that empties the candidate set;
- the wrong skeleton;
- an invalid query.

A corrupted file is refused on load. A database swap mid-flight re-selects instead of falling
through. The chain never hands one provider another provider's memory.

## 11. MotionPack integration and the offline/runtime split (§37–§43, ADR-650)

- **Offline:** import (glTF, BVH), retarget, then `buildMotionPack`. The pack is analysed, and the
  analysis records contacts, phase, the loop flag read from the clip, root travel and licensed
  provenance. `avgen-motion augment` adds §21 variants kept only where they add coverage. The
  database is written as a `.motiondb` with a build key, schema digest, source digest and file
  version (cinfra, §36–§38).
- **Runtime:** a database is loaded off-thread and published atomically, or built at load from the
  rig's own clips (ADR-623's path). It is shared by every character on the same skeleton and
  config.
- **Retargeting onto the Glowmere alien** needs the IK leg pass (ADR-624), because its legs are
  three siblings and a rotation retarget cannot move its feet.

## 12. Memory layout (§6, §53)

About 150 bytes per sample at 33 dimensions: 0.25 MB for the scout, 64.6 MB for 434k samples of
100STYLE. Features are one contiguous float array, sample-major. Contiguous storage is 1.2× faster
than scattered storage at 0.5 MB and 2.1× faster at 55 MB (the cache effect, measured).

## 13. Performance measurements (§50, §51, §55)

Full tables are in the phase log, "§50–§55". They were **taken under load** (load average 5–23 on
12 cores); relative results hold and absolutes are upper bounds.

| | 10k samples | 434k | 714k |
|---|---|---|---|
| linear, p50 / p99 µs | 67 / 169 | 2,123 / 4,036 | 3,190 / 7,584 |
| VQ, p50 µs | 8 | 393 | 739 |

| characters × samples | frame ms, p95 |
|---|---|
| 1 × 10k | 0.15 |
| 10 × 105k | 7.5 |
| 50 × 105k | 35 |
| 100 × 714k | 411 |

Build throughput is ~46,000 samples/s. A 68 MB database loads verified in 27–41 ms.

## 14. Quality measurements (§22, §23, §46, §47–§49, §58)

- **Adversarial and correctness** (§47, §49, §78): every case on a golden corpus of distinguishable
  clips (`tests/support/golden_motion.hpp`), each paired so a no-op answer fails one arm.
- **Golden scenario** (§48): walk north, turn east, stop plays start, walk, left turn, idle.
- **Evaluation harness** (§46): replays a known clip as requests and reports continuity,
  trajectory, velocity, pose, contact and phase error, and switches per second. On the golden
  corpus it reproduces a clip at a floor of zero. **On the scout the floor is not a floor**: a
  velocity-only request cannot tell one walk from another, so the matcher does not pick
  `Walking` even when it is available.
- **Coverage** (§22, §58): speed, direction and turn are read from the trajectory. Scout
  augmentation (§21) moved run from limited to moderate and left turn from moderate to good.

## 15. Failure-case analysis (§70)

§70: "classify the failure; fix the correct layer." Every defect Phase C found, classified:

| failure | class | layer fixed |
|---|---|---|
| Walk requests chose fight clips (in-place walks read as standing) | C. bad features | implied travel from planted feet |
| Every clip ended in a fictitious stop | C. bad features | look-ahead continues past the end |
| Features in world orientation; east-facing body got a sidestep | C. bad features / E. bad trajectory | body-frame features and query |
| Tags said walk; motion said still | A. bad database | measured from implied travel; clip-loop flag from the clip |
| `OneShot` had no writer; every clip looped | A. bad database | loop closure measured per clip |
| Continuation played 2× at 60 Hz | G. bad transition | carry on by time |
| Fallback read the matcher's sample index as a clip | G. bad transition | chain hands over only a provider's own memory |
| Seek never replayed provider memory | G. bad transition | replay on every step |
| Matcher froze on the end of a start | F. bad candidate filtering | `Terminal` tag rejected |
| Curves predicted as straight lines | E. bad trajectory | turn-rate integration in prediction |
| A turn on the spot invisible to the search | C. bad features | future-facing feature; in-place mean-relative facing |
| Pose half outweighed the request (the alien stayed idle) | D. bad weights | per-character versioned weights |
| 100STYLE legs welded at rest reach | B. bad retarget | IK leg retarget (ADR-624) |
| Retargeted walks read as idle | B. bad retarget / A. bad database | rest channels pruned, so the travel joint is the travel joint |
| Travelling clips walked away from the entity | H. procedural adaptation | the §71 policy: simulation owns translation |
| Strided plan slower than linear | I. search approximation | measured, not adopted |

**No failure in this table was fixed by adding smoothing.**

## 16. Known limitations

- **The scout cannot strafe.** It has no strafe clip, and warping its walk 35° sideways fails the IK
  gate.
- **Selection quality on a real corpus depends on authored filtering and weights.** The demo needed
  an allow-list and a weight sweep, and the weights did not survive a corpus rebuild (trajectory
  position 3 became 6 when the turn variants grew to three cycles). A velocity-only request under-determines which walk plays (§46).
- **Style** is in the request and in the clip provider, but it is not yet a term in the matcher's
  cost (§44).
- **Human legs straighten more than the alien's**: 14% of retargeted leg-frames are at or past
  straight. The look is the owner's call.
- **Timings were taken under load**; to re-take.
- **Glowmere ships on the clip provider** until the owner approves a matcher-driven alien.
- **Dataset licences**: 100STYLE is verified (CC BY 4.0). ACCAD and CMU are not yet assessed (§61).

## 17. Phase D integration points

- **Intent in, pose out.** Phase D writes `EntityState::intent` (vector intent, facing, steering),
  and the chain turns it into motion. `motionScript` is the first producer and a model for how.
- **Avoidance** arrives as `MotionRequest::steering`, added to the desired velocity, never blended.
- **Style** (`MotionRequest::style`) is the hook for behaviour-level mood. It needs §44's cost term to
  reach the matcher.
- **Diagnostics**: `MotionDebug` (provider, fall-through, layers), `avgen-motion explain` (why a
  sample won), and the §46 harness.

## 18. Phase E integration points

- **A learned provider is another `IMotionProvider`** in front of the matcher. The chain, the
  fallback, the seek replay and the inertialized seam are provider-agnostic (ADR-541 corollary 1).
- **The database and the evaluation harness are the training and test sets**: the same features,
  the same body frame, the same golden corpus.
- **Determinism**: a provider must keep all state in `MotionMemory` and advance by time, or scrub
  will not equal play (ADR-360, ADR-556).
