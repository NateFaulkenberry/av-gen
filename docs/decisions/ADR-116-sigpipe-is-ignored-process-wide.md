# ADR-116: SIGPIPE is ignored process-wide, not blocked per thread

**Status:** Accepted
**Date:** 2026-09-13

## Problem

The ffmpeg video backend writes frames into a pipe. When ffmpeg exits — a bad codec, a full disk,
a user's broken install — the next write raises SIGPIPE, whose default action is to kill the
process. `writeAll` therefore blocked SIGPIPE with `pthread_sigmask` around its writes and consumed
any pending one, so the failure would surface as `EPIPE` and become a readable error carrying
ffmpeg's own stderr. There is a test named for exactly that: *"a failing encoder is reported with
its stderr, without SIGPIPE"*.

It worked, and then it stopped working, and nothing about the video writer had changed.

**A per-thread block protects the process only while that thread is the only one with the signal
unblocked.** Any other live thread is a candidate for delivery. After a native (AVFoundation)
render, CoreMedia leaves worker threads running for the life of the process. From that point on
every EPIPE in the program was delivered to one of *those* — idle, parked in a semaphore, with no
connection to the pipe — and the process died.

## How it was found

Not by the video tests, which pass. The **whole CPU test binary** was dying on signal 13 partway
through and never reaching Catch2's summary, so the suite had no result at all — it merely stopped.
The output ended in the middle of ordinary logging and looked like a completed run.

It reproduced in `[video]` alone, needed exactly two test cases, and was order-dependent:
`--order decl` died, `--order lex` passed. `lldb` named the thread:

```
thread #4, name = 'com.apple.coremedia.sharedRootQueue.47', stop reason = signal SIGPIPE
  frame #0: semaphore_timedwait_trap
  frame #3: libdispatch.dylib`_dispatch_worker_thread
```

The native-backend test ran first and left those threads behind; the fake-ffmpeg test then wrote
into a pipe nobody was reading.

## Decision

`signal(SIGPIPE, SIG_IGN)` once, process-wide, under `std::call_once`, before the writer creates its
pipe. `write()` then reports `EPIPE` on every thread, which is what the error path already expects.

The per-thread block in `writeAll` stays. It costs nothing, and it keeps the guarantee visible in
the code that depends on it rather than only in a process-wide side effect established elsewhere.

## Consequences

- **A user-facing crash is fixed, not just a test one.** An ffmpeg that dies mid-render could kill
  the app instead of producing the error message the code had carefully prepared. That path needed
  only a prior native render in the same session — ordinary use.
- **The CPU suite reports again**: 1537 cases, 493,369 assertions, zero failures. Any "CPU suite
  green" claim made from a truncated run before this was unfounded; the output was cut short, not
  clean.
- Changing a process-wide disposition from library code is a real side effect. It is justified here
  because `SIG_IGN` for SIGPIPE is the near-universal convention for any program writing to pipes,
  and the alternative is a process that dies from an unrelated thread at a time nobody can predict.

## Verification

A regression test holds a bystander thread alive across the failing write, rather than depending on
AVFoundation having left threads behind — a test whose reproduction depends on what some other test
happens to leave running is a hostage, not a test. It is barrier-synchronised so the thread is
provably running before the write and still running after it.

Confirmed against the broken build by stashing the fix: **exit 141 (SIGPIPE) without it, clean with
it.**
