# ADR-364: The viewport kept drawing underneath, and the jobs were not on the destruction list

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend`
Relates to: ADR-020 (render jobs), ADR-351 (what "offline" already means), ADR-182 (a probe that
cannot fail proves nothing), ADR-225 (a setting the application does not keep is not a setting),
ADR-084 (`canvasRenderScale`, the shape this setting follows)

*Numbered 364 after 361-363 on this branch. Main's high-water mark was 360 when this started and
has since moved; renumber at merge.*

## What the brief asked for, and what was here

> When ANY offline output render begins, the normal interactive viewport renderer must stop
> consuming unnecessary resources. Do NOT merely hide the viewport.

There was **no render state in this program at all.** No enum, no `paused`, no `needsRedraw`, no
idle throttle, no frame limiter. Three behaviours ever shed work: a 50 ms sleep when the window is
minimised (`application.cpp:3476`), a 16 ms backoff on a failed swapchain acquire (`:3670`), and
`PresentMode::Fifo` hardcoded at `gpu/surface.cpp:70`.

An in-app render is pumped from the main loop as `job_->step(4, 0.010)` — four frames or ten
milliseconds per UI frame — while the world goes on being recorded and submitted behind it, into
the same device the render is using.

## The part the brief gets wrong, and why this is a setting rather than a reversal

**That behaviour is deliberate and is advertised.** The Render panel's closing line reads
*"renders load the saved project; the live view keeps playing"*, and for an author iterating it is
the right answer: the render is a background task and the editor stays an editor.

So this is not a bug to be silently corrected. It is **two legitimate answers to one question** and
the program had only ever offered the second. `AppSettings::suspendViewportDuringRender`, **default
on**, because the brief is right about what a final render wants and that is the more common case —
and reachable in one click in Settings › Rendering, because taking the other answer away would be
ADR-225's failure running backwards: a behaviour the application had, and stopped keeping.

It is in `AppSettings` rather than in the project, for `canvasRenderScale`'s reason: it describes
how this person works on this machine, and opening somebody else's project must not change whether
your viewport keeps running.

## The state, and the one invariant written down

`src/app/render_state.hpp` — header-only, no ImGui, no GPU, no engine, for the reason
`output_preview.hpp` is.

```cpp
enum class RenderActivity : std::uint8_t { Interactive, OfflineRaster, OfflinePathTrace };
struct ViewportPolicy { bool drawWorld = true; bool drawUi = true; };
constexpr ViewportPolicy viewportPolicyFor(RenderActivity, bool suspendSetting);
```

Not called "offline". ADR-351 already spends that word on `QualityTier::Offline` and on the
ADR-020 batch pipeline, and a third meaning in a state enum is the collision that ADR exists to
prevent. This says what the program is *doing*.

`drawUi` is **always true** and a test asserts it for all six combinations. Progress and Cancel live
in the interface, and a render that cannot be stopped because the UI stopped being drawn is a worse
failure than the one this file fixes. The field exists so that invariant is somewhere a change has
to argue with, rather than being true by accident.

The path tracer suspends the viewport too, which is worth stating because it is tempting not to:
the tracer is CPU-only, so there is no GPU contention — but it takes every core it is given, and the
world's per-frame CPU work is the largest thing competing with it.

What is skipped is the world: the scene pass, the post chain, the debug geometry, and the
`transformHistory_` record that reads a frame which was not rendered. The clear, ImGui and the
submit all still happen. The canvas therefore shows the last frame before the render began, and the
panel says so rather than letting a stale picture be mistaken for a live one.

## The evidence, in the product and not only in a test

ADR-182 and the lesson of the four subsystems built, tested and unreachable: **a test that
constructs the objects shares the product's blind spot.** So the count of frames the viewport did
not draw is in the Render panel, under both renderers' progress lines, and on the log at each edge.
A suspension that is switched on and reports zero skipped frames is a suspension that is not
happening, and a person can see that rather than having to trust it.

Measured, on this machine, `examples/chamber/chamber.json` at `--render-in-app --frames 400`:

| arm | `viewport: suspended` | frames not drawn |
|---|---|---|
| setting on (default) | yes | **307** |
| setting off (the control) | **no line at all** | 0 |

The control was run by flipping the key in the real settings file and restoring it afterwards; the
file's SHA-256 is unchanged.

## And the crash the control arm found

The control arm exited **139**. So did the arm, once `--frames` was small enough that the render was
still in flight at exit. The report:

```
EXC_BAD_ACCESS ... possible pointer authentication failure
avgen::gpu::Context::waitFor
avgen::gpu::ReadbackRing::~ReadbackRing
avgen::app::RenderJob::~RenderJob
avgen::app::Application::~Application
```

`Application::~Application` carries an explicit, commented destruction order — *"UI before GPU
context, renderer before context, window last"* — and `job_` and `ptJob_` were **not on it**.
`unique_ptr` members destroy in reverse declaration order; `job_` is declared at `application.hpp:346`
and `context_` at `:394`, so the context went first and the readback ring then waited on a Dawn
instance that no longer existed.

**It reproduces on a pristine `main` build at `1cdfb84a`, so it is not this branch's.** It is fixed
here anyway, because this is the list whose comment claims to own destruction order and because the
brief asks in terms for cancellation to shut the renderer, the encoder and the background jobs down
cleanly. Three runs of three, exit 0, where three of three were 139 before.

## What is not done

The viewport is suspended, not throttled. The UI still runs at vsync while a render goes, which
costs an ImGui pass and a present per frame. That is deliberate — progress that updates once a
second reads as a hang — but if it ever shows up in a measurement, a frame cap while suspended is
the next lever and it is not pulled on a guess.

`engine_->update` is also **not** suspended. It drives the transport and the audio, and stopping it
would freeze playback under somebody who only wanted a render in the background. The scene work
inside it is real cost and is the obvious next candidate; it needs a measurement first, and the
machine has two other agents on it.
