# ADR-2131: no command buffer runs past the GPU watchdog, and a frame the GPU never ran is an error

- Status: Accepted (2026-11-24), avagent/neon-dreams-cloud4k (Neon Dreams; owner msg 320: "a 4K render at full cloud
  resolution comes out black with no error -- investigate this")
- Code: `rendering/cloud_renderer.{hpp,cpp}` (`cloudMarchBands`, `kCloudMarchUnitsPerSubmit`, the banded march in
  `encode`), `gpu/context.{hpp,cpp}` (`encodeExecutionStamp`, `reportNotExecuted`), `gpu/readback_ring.{hpp,cpp}` (the
  stamp after each frame's pixels), `rendering/scene_renderer.cpp` (`renderSubmitted`'s stamp)
- Tests: `tests/rendering/test_gpu_watchdog_gpu.cpp` (`[adr2131]`)

## Context

Neon Dreams at 3840x2160 with `clouds.resolution` 1.0 (192 steps, 8 light taps) rendered every frame black, with
"GPU errors: 0" and exit 0. 1080p at any resolution and 4K at 0.75 were fine. Measured (it21 build, 56.0 s, three
frames each):

| output | march | march MP | frames |
|---|---|---|---|
| 3840x2160 | 0.75 | 4.67 | correct; the march pass alone 4.40 s GPU (timestamps) |
| 2560x1440 | 1.0 | 3.69 | correct; march 3.62 s |
| 3200x1800 | 1.0 | 5.76 | black |
| 3520x1980 | 1.0 | 6.97 | black |
| 3840x2160 | 1.0 | 8.29 | black (est. ~7.8 s march) |

The unified log has the cause for exactly the black runs and none of the others:
`Execution of the command buffer was aborted due to an error during execution. Caused GPU Timeout Error
(00000002:kIOGPUCommandBufferCallbackErrorTimeout)`, then `Ignored (for causing prior/excessive GPU errors)
(00000004:kIOGPUCommandBufferCallbackErrorSubmissionsIgnored)` for every later submission of the process. macOS kills a
command buffer that runs past its GPU watchdog (~5 s here: 4.8 s frames pass, ~5.5 s ones do not) and then ignores
the process. The frame's whole command buffer -- scene, clouds, post, readback copy -- is the one killed, so the
picture is black, not just the clouds; and later frames come back black in milliseconds.

It is silent because Dawn's Metal backend never reads `MTLCommandBuffer.status`/`.error`: no uncaptured error, no
device loss, and `MapAsync` of the readback buffer succeeds over the zeros the copy never wrote. Nothing is wrong with
the sizes (no texture or buffer near a limit; the march is a fragment pass, no dispatch counts), and nothing is NaN.
The same signature -- "every pixel 0, GPU errors: 0, device not lost" -- was met before in `simulation.cpp`'s catch-up
and fixed there the same way (bounded submissions with a wait), root cause then unknown.

The shipped 4K master (0.75) was within ~0.4 s of the limit at its heaviest view.

## Decision

1. **The cloud march is split into bands of rows when it would run long.** Its cost is counted as march pixels x
   steps x (1 + light taps); a band holds at most `kCloudMarchUnitsPerSubmit` (2^30, ~0.6 s at Neon Dreams' heaviest
   measured view, ~8x under the watchdog). Each band is a scissored pass; before every band but the first the frame's
   encoder is finished, submitted and waited for, and the caller's encoder is replaced by a fresh one, so the frame
   goes on in it. The first band clears both targets, the rest load. A pixel's value does not depend on the band
   (bit-identical in the test). Neon Dreams live (0.4, 40 steps) and offline 1080p (0.5, 128 steps) stay one pass;
   the 4K master at 0.75 becomes 8 bands, at 1.0 14.
2. **Every frame carries an execution stamp.** `Context::encodeExecutionStamp` writes a fresh token to a small source
   ring on the queue and copies it into the frame's own readback (the ring's buffer, 256 bytes after the pixels;
   `renderSubmitted`'s stamp buffer) as the frame's last command. A token that did not land is
   `Context::reportNotExecuted`: a counted GPU error naming the cause, the readback ring drops the frame and the render
   fails instead of writing black.

## Consequences

- A render that the watchdog would still catch (a pass other than the clouds that runs for seconds) now fails loudly
  at the first such frame with the Console message to look for, rather than writing a black film.
- A banded frame waits for the GPU between bands (offline renders are GPU-bound already; the job waited for the GPU
  3976 of 4364 s of the it21 master). Timeline marks: each band is a `clouds` mark.
- The bands' budget is a cost model, not a measurement; a march much denser per step than Neon Dreams' could still
  approach the watchdog. The stamp is what makes that visible.
- Tests do not trip the watchdog itself: a process that has is ignored by the GPU from then on. The threshold
  reproduction is the 4K render above.
