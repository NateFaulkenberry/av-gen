# ADR-064: The job system

Status: Accepted

## Context

`app::RenderJob` already runs a render on its own thread with progress and cancellation, and does
it well. What it cannot do is be *one of several* queued things, report which stage of itself it is
in, or say honestly that it does not know how long it has left. The offline AI work in milestone 9
needs all three.

## Decision

A queue with a fixed worker pool, and four properties enforced by shape rather than by convention.

**Progress is honest.** Overall progress exists only when the running stage measures itself; a
stage that never calls `setStageProgress` leaves `progressKnown` false and the UI shows the stage
instead of a bar. `estimatedRemainingSeconds` is negative when unknown, so a caller has to check it
rather than formatting a negative duration. Inventing 72% is worse than admitting ignorance,
because a fabricated bar is indistinguishable from a real one.

**Cancellation is prompt and total.** A queued job is cancelled without ever starting. A running
one is cancelled when it next polls `shouldCancel()`, and whatever it returns after that, the
system records Cancelled. The destructor cancels everything before joining, so quitting cannot take
as long as the longest job.

**A failing job does not take the queue with it.** An error marks that job Failed and the workers
carry on. A body that *throws* is caught and becomes an ordinary failure — otherwise it would take
a worker thread with it and the queue would quietly stop draining, which is the kind of bug that
looks like a hang.

**Nothing blocks the render or audio thread.** `statuses()` returns snapshots copied under the
job's own lock, so a UI reading them holds nothing a worker needs.

## Consequences

`waitFor` and `waitAll` exist for tests and shutdown and are documented as never to be called from
the render or audio thread.

Logs are bounded at 512 lines per job: a job that logs per frame for an hour must not become the
reason memory ran out.

Finished jobs stay in the list until `clearFinished()`, because the UI should keep them until a
person dismisses them.

Not done: `RenderJob` has not been ported onto this. It works, it is tested, and moving it is a
separate change with its own risk; the two coexist until there is a reason to merge them.
