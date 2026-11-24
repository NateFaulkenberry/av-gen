# ADR-2133: no network listener by default; OSC is the editor's, on loopback, and the interface is never a project's

- Status: Accepted (2026-11-24), eng/save-and-osc (engine architecture evaluation, 113-engine-architecture)
- Code: `app/control_hub.{hpp,cpp}` (`setLiveOsc`, `setLiveMidi`, `setOscBind`, `Status::oscBind/oscExposed`;
  both sources off until asked), `app/cli_batch.hpp` (`LiveControlAsk`, `liveControlFor`), `app/application.cpp`
  (`--osc-bind`, `--no-osc`, `--midi` opening MIDI in a headless run, the hub set up before the project loads),
  `control/osc.{hpp,cpp}` (`kOscLoopback` default, no `SO_REUSEADDR`, "in use" error, `boundAddress`,
  `isLoopbackAddress`), `control/control_map.{hpp,cpp}` (no `oscBind`; `bind` reported and not read; `toJson` writes
  non-defaults only), `ui/control_panel.cpp` (the address, orange when exposed)
- Tests: `tests/unit/test_live_control_defaults.cpp` (`[adr2133]`); `test_engine_live_control.cpp` and
  `test_control_engine.cpp` now ask for the listener they test

## Context

Every engine opened an OSC listener on `0.0.0.0:9000` and opened MIDI input. `ControlHub::liveIo_` defaulted to true,
the map defaulted to `oscEnabled = true, oscBind = "0.0.0.0"`, and only the jobs that remembered to call
`setLiveControl(false)` stayed shut. Headless captures and `--ai-script` runs were not among them. Reproduced on
8731bf360 during the evaluation's run 09: `osc: listening on 0.0.0.0:9000`, and `lsof -nP -iUDP:9000` showed
`UDP *:9000` held by the headless process. The direct scheme (`/avgen/param/<path>`) sets any parameter with no
authentication. So anyone on the same network could drive an unattended session.

Every save also wrote `"bind": "0.0.0.0"` into the project (11 tracked projects carried it), so a file could carry
"listen on every interface" to the next machine that opened it.

The socket set `SO_REUSEADDR`. On BSD that lets a second program bind `127.0.0.1:9000` beside another's
`0.0.0.0:9000`, and the two then split the messages silently.

## Decision

1. **Live control is off until asked.** `ControlHub` opens neither OSC nor MIDI until `setLiveOsc`/`setLiveMidi`
   (or `setLiveIo`). `liveControlFor` is the one rule for the app. The interactive editor (a window, not
   `--capture-ui`, `--ui-script` or `--ui-ab` automation) turns both on. Every other run opens OSC only with
   `--osc-port`/`--osc-bind`, and MIDI only with `--midi <filter>`. `--no-osc` keeps OSC shut anywhere. A project
   file is not a request, because every older save wrote `"enabled": true`.
2. **The editor's OSC stays on by default, on 127.0.0.1:9000.** The owner's live use keeps working: a sender on the
   same Mac reaches it with no change. Loopback cannot be reached from the network. It is still unauthenticated
   against local programs, which is the same trust a local user already has. Making OSC opt-in in the editor
   would have broken an existing workflow for no gain against remote attackers.
3. **The interface belongs to the machine, not the project.** `ControlMap` has no bind address. `control.osc.bind`
   in a file is reported once per load ("no longer read") and never written. The hub's `setOscBind` (`--osc-bind`)
   is the only way to listen beyond loopback. It refuses anything that is not IPv4. When the socket opens on a
   non-loopback address it logs a warning naming the risk, and the Control tab shows the address in orange.
   The 11 tracked projects had the line removed by a text edit (ADR-441: no alias).
4. **`control` holds decisions only.** `ControlMap::toJson` writes a field only when it differs from the default.
   The engine writes `tempoSource` only when it is not `analysis`. A project that never touched live control has no
   `control` block.
5. **A port in use is an error, and it is said.** No `SO_REUSEADDR`, so a clash is `EADDRINUSE`, reported as "port N
   on A is in use by another program (another AV Gen session?); OSC is off for this session -- choose another port".
   The session carries on with OSC closed (`status().oscError`, the Control tab). The test holds a loopback port
   and checks that neither the hub nor a wildcard bind can take it.
6. Changing the bind reopens the receiver. Before, only a port change did, so a bind edit was silently ignored.

## Consequences

- Headless and scripted runs hold no UDP socket. On this branch the `lsof` sample during run 09 is empty, and the
  editor's sample shows `UDP 127.0.0.1:9000`.
- Two agents' headless runs no longer race for port 9000. Two editors still cannot share a port: the second reports
  it, and `--osc-port` picks another.
- A controller on another device (a phone or tablet running TouchOSC) now needs `--osc-bind 0.0.0.0` (or the Mac's LAN
  address). Documented in `docs/help/modulation-external-control.md`.
- `--midi <filter>` used to only change the filter. In a headless run it now also opens MIDI input.

## Revisit triggers

- If the owner wants network OSC on every launch without a flag, it belongs in `AppSettings` (per machine, beside the
  live quality), not in the project. Add it with the Control tab control that edits it.
- An API/RPC server (the evaluation's next step) must follow the same rule: off in headless runs unless asked,
  loopback by default.
