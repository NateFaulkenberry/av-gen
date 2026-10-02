# ADR-1058: The near plane: a parameter, and at most 5 cm for a journey

- Status: Accepted (2026-10-02), proto/liminal-space (All You Got art pass 4, PART 7: the ~2:15 artifact)
- Code: `src/scene/composition.{hpp,cpp}` (`camera/near`, scene `camera.near`, the journey's automatic value).
- Tests: `tests/integration/test_film_validate.cpp` (`[adr1058]`).

## Context

At ~2:15 in pass 3 take 3 (134.8-138.4 s) a bookcase in the study broke into jagged, partial outlines as the camera
swung past it. The film pass (ADR-1057) measured the camera 0.13 m from `StudyBookcase` while the near plane was
0.404 m. The SDF march begins at the near plane (`shaders/sdf_raymarch.wgsl`: `tStart = max(slab.x, tNearPlane)`),
so every ray started inside the bookcase and drew its cut-open cross-section. Rendered with a 5 cm near plane the
same frame shows the whole bookcase (`~/Desktop/av-gen-review/24-liminal-space/pass4/eng/2m15-near-plane-before-after.png`).

The 0.404 m came from `Composition`'s automatic near plane, 0.5% of the scene's bounding radius, clamped to
1-50 cm. That suits a camera framing an object. The film is one composition that holds a street, a landscape and a
skyline (radius 81 m) while its journey camera walks through 3 m rooms.

## Decision

1. `camera/near` (metres, keyable; scene JSON `camera.near`): 0 = automatic (the default, saved only when set).
2. The automatic near plane of a journey camera (mode 3 with a journey) is at most 5 cm. Other modes are unchanged,
   so every non-journey scene renders exactly as before.
3. The film pass reports any frame where geometry is nearer than the near plane (`nearClip`), so a near plane that
   is too deep for a shot is found before render, whatever set it.

## Consequences

- Depth precision: a 5 cm near plane with the 2 km far plane is still sub-centimetre at 10 m; distant skyline meshes
  have less precision than before, which the liminal film does not exercise (it is SDF, which writes its own depth).
- The ~2:15 shot still has the camera 13 cm from the bookcase, a close-up that fills the lens (`closeGeometry`): the
  camera path is the art agent's to move.
