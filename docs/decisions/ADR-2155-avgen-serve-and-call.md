# ADR-2155: `avgen serve --stdio` and `avgen call` drive the engine through a CPU-only host, with no application

- Status: Accepted (2026-11-25), eng/engine-api-s2 (Engine API stage S2; 113-engine-architecture, design in
  03-phase0-api-contracts/02-session-api.md sections 4, 6, 9 and 10, stage plan in 04-poc-plan.md)
- Code: `api/engine_host.{hpp,cpp}` (`EngineHost`, `HostFlags`/`parseHostFlag`, `protectStdout`/`restoreStdout`),
  `api/jsonrpc_server.{hpp,cpp}` (`serve`, `runServeCommand`, the method and error tables, `protocolMarkdown`),
  `api/cli_call.cpp` (`runCallCommand`), `app/main.cpp` (the dispatch, the `--help` text),
  `ai/main_thread_queue.{hpp,cpp}` (`waitForWork`, `wake`), `api/session.{hpp,cpp}` (`Host::canRender`, GPU tools
  `UNAVAILABLE`), `api/describe.cpp` (`docs/api/protocol.md` is a generated artefact)
- Docs: `docs/api/protocol.md` (generated), `docs/help/reference-command-line.md`
- Tests: `tests/unit/test_api_serve.cpp` (`[api][serve]`)

## Context

After ADR-2153 every client goes through one `api::Session`, but the only client was in the process: Studio's
assistant. A program outside the engine had two ways in, and both were the wrong shape:

- `--ai-script` runs a scripted conversation inside the whole `Application`, whose `init` always creates a
  `gpu::Context` and the scene renderer, headless or not (the evaluation's start-up trace, section 3);
- nothing at all for a conversation: each run is a fresh process that loads, acts and exits.

The evaluation's probe sequence (01-evaluation/evidence/poc-today/probe.ai.json) has `project.create` and
`asset.import` refused when the session has no roots, which is what a run with nothing to give it roots gets (run
again through `avgen call --calls` with no roots at ecbada74f: both `PERMISSION_DENIED`).

## Decision

1. **`api::EngineHost`, a CPU-only host.** It owns one Offline `app::Engine` with live control off, and reuses
   `LocalHost` for the queue and hooks rather than duplicating them. Its loop (`run`) calls `session.pump()` and,
   when nothing is queued, sleeps in `MainThreadQueue::waitForWork` until a call is queued, `stop()` is called or
   the queue shuts down: no timer, no busy loop, no engine tick while idle. It lives in `avgen_core`, which cannot
   link `gpu::Context`, and has no GPU member; `canRender()` is false. The two seams are marked: stage S1b sets the
   time policy in its constructor (`EngineHostOptions`), and stage S6 adds the GPU half and the job slices between
   pumps.
2. **`Host::canRender()`**, false by default. A tool whose permissions include `gpu` answers `UNAVAILABLE` ("no GPU
   host in this session yet"), both in its capability answer and when called, and its body never runs. No such
   tool exists yet; S6 adds them.
3. **`avgen serve --stdio`: newline-delimited JSON-RPC 2.0.** `initialize`, `tools/list`, `tools/call`,
   `shutdown`; `notifications/progress` and `notifications/log` from the server; batches; `-32700`, `-32600`,
   `-32601`, `-32602`, `-32002` (before `initialize`), `-32603`. The host thread is the main thread; a reader
   thread parses, validates the envelope, and calls `Session::call`, which validates the arguments and
   permissions on that thread against the immutable registry and waits for the host to run the body. The reader
   never touches the engine. Every write goes through one mutex, a whole line at a time. A tool failure is a
   successful response whose result is the envelope with `success: false`; an unknown tool is `NOT_FOUND` there.
   `dryRun` checks the name, the arguments, the permissions and availability and runs nothing.
4. **stdout is the protocol only.** At start fd 1 is duplicated for the protocol and fd 1 is pointed at stderr, so
   a stray `printf` anywhere in the process lands in the log.
5. **Shutdown.** `shutdown`, or end of input, closes the session on the host thread: an open explicit transaction
   is rolled back (and its files with it), and the project is never saved for the client.
6. **`avgen call <tool> '<json>'` and `avgen call --calls file.json`**: one fresh session, the calling thread as
   the host thread (each body inline), the envelope printed. Exit codes 0, 1 (a tool failed; the envelope is still
   printed), 2 (usage), 7 (the host or `--project` failed). `--project` opens a project first and makes its folder
   a read and write root; `--save` saves after the calls if they all succeeded.
7. **Startup.** Both dispatch in `main` before `parseArgs` and `Application`: no window, no ImGui, no settings
   file, no OSC or MIDI listener, no GPU device. `--root` is a read and write root, `--read-root` and
   `--write-root` one each (all repeatable, each must exist, made absolute once on the command line), and
   `--allow-subprocess`. `liveControl` is always false; nothing calls `setLiveOsc`.
8. **The protocol document is generated** from the server's own method and error tables (`protocolMarkdown`), as
   the fifth artefact of `avgen --describe all`, so the T1 drift test holds it to the code.

## Alternatives considered

- **Cut a headless mode out of `Application`.** Rejected: the application's start-up is the window, the device,
  the settings file and the listeners, which is everything this must not do (Part 3 of the plan).
- **A thread per request**, so a slow call does not hold the next request and a cancel can be read while a call
  runs. Deferred: the host runs one body at a time anyway, and nothing the PoC needs is cancellable. Requests are
  answered in order, and `initialize` says `concurrency: "sequential"` and `cancellation: false`.
- **A socket transport.** Rejected for now: stdio needs no port, no permission and no authentication, and the
  owner's rule (ADR-2133) is no listener by default.
- **Refuse GPU tools with `PERMISSION_DENIED`** by starting the session with `gpu: false`. Rejected: the session
  is permitted to use the GPU; the host cannot. That is `UNAVAILABLE`, which is what a client retries elsewhere.

## Consequences

- A program with no access to the source can create a project, import a model and edit it, through the same tool
  registry as Studio's assistant, with the permissions it was started with and nothing else.
- The run creates no `gpu::Context` and never takes the GPU lock, so it does not wait behind a render.
- A created project has no composition until stage S3 (`project.create {scene: "empty"}`), so the probe's
  `scene.create_node` right after `project.create` is still refused ("no composition"); on a project with a scene
  it succeeds.

- Every tool in the catalogue, called with `{}` on a fresh session with no project, answers a structured result:
  no crash and no `INTERNAL` (`tests/unit/test_api_serve.cpp`). The 81 tools in one `avgen call --calls` session at
  ecbada74f: 29 succeed, 35 `INVALID_ARGUMENTS`, 5 `PERMISSION_DENIED`, 11 `UNAVAILABLE` (the composition tools,
  `camera.set` on the orb scene, `performance.get_stats`, `director.watch_events`) and 1 `FAILED_PRECONDITION`
  (`project.save`). The composition tools answer
  `UNAVAILABLE` "no composition", which is what ADR-2153's capability answer says for the `composition`
  requirement (`available: false`); they were left as they are rather than changed to `FAILED_PRECONDITION`.

## Revisit triggers

- Stage S6 (the GPU half): `canRender()` becomes true there, and the loop runs job slices between pumps.
- A client that needs a cancel or a second request while a call runs: a request thread pool and
  `notifications/cancelled`.
- MCP (stage S9): the same server with a different result shaper.
