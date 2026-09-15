# ADR-225: A setting the application does not keep is not a setting

**Status:** Accepted
**Date:** 2026-09-15

## The defect

`AutoDirectorSettings` — every control in the Auto-director panel — lived on `DirectorState`, which
is a member of the running `Application` and of nothing that is written anywhere. The panel edits it
in place, `directEngine` reads it, and at quit it goes with the process.

That is a reset to the defaults on every launch, and for this struct the defaults are the settings
the controls exist to move away from:

| control | default | what the default means |
|---|---|---|
| `maxViewRate` | 0 | **off** — no cap on how fast the view swings |
| `maxCameraSpeed` | 0 | **off** — no cap on how fast the camera travels |
| `dwellShots` | 1 | a new subject every shot, which is the thing ADR-203 was written to fix |

ADR-200 added the two caps because "moving too fast" was the complaint; ADR-203 added the dwell
because importance could not express it. All three came back off, every launch, without saying so.

## The decision

`AppSettings` gains a `director` section. It is a global preference rather than a project field for
the same reason the canvas render scale is: it is a statement about how fast *this viewer* wants a
camera to move, not about the piece. A project carries its cut as baked timeline tracks either way,
so nothing about reopening somebody else's project changes because of this.

Three details that were not obvious.

**The keys are `--director`'s keys.** `mode`, `minShot`, `maxSpeed`, `maxSwing`, `dwell` and the
rest are already the spelling the command-line flag uses for the same fields. One spelling for a
setting whether it arrives from a command line or a preferences file is worth more than a prettier
key, and it means the flag's own documentation describes the file.

**The format version is not bumped.** `AppSettings::fromJson` refuses a document whose `version` is
newer than the build understands, and refusing is total — it would take the AI provider
configuration with it. A build that predates this key ignores it and rewrites the file without it,
which loses the director settings and nothing else. The section is additive in both directions and
saying so in the version number would make the older direction worse.

**A value outside `validate()`'s range is refused, not clamped.** Same rule as the AI section, for
the same reason: the file is machine-written, so the only route to one is a hand edit, and being
told which field is wrong beats launching with defaults and wondering where the settings went.

## The write, and why it is not on every change

The panel edits the struct in place and has no way to say "I am finished" — an ImGui slider reports
a change on every frame of a drag, and `AppSettings::save` writes a temporary file and renames it.
Saving on change would be sixty renames a second while somebody drags `max swing` across its range.

So the host watches the struct instead of being told: `Application::persistDirectorSettings` runs
once a frame, takes the value whenever it differs from the last one it took, and writes only after
it has stopped moving for 400 ms. Two guards on top of that:

* **An invalid intermediate is not a preference.** The panel deliberately lets a value sit invalid
  while it says so — a wide lens longer than the hero lens, on the way to a valid pair — and a
  settings file carrying one is refused wholesale on the next launch. Only a validating value is
  written.
* **`--director` reads and never writes.** That flag exists to make a measurement reproducible. A
  flag that quietly rewrote the preferences would make the next *unflagged* run a different
  measurement.

And the settle window is flushed in `~Application`, before anything is torn down: quitting straight
after moving a slider must not be the one path that loses it.

## Evidence

`tests/unit/test_app_settings.cpp`, four cases. The negative control is the whole of it: with
`doc["director"] = directorToJson(director)` removed and nothing else changed, a `maxViewRate` of
12 deg/s comes back as **0.0** (off), a `dwellShots` of 5 as **1**, and a `seed` of 99 as **1** —
which is the defect, reproduced exactly, in the three fields that matter most.

## What this does not cover

The host loop itself — load-to-live, the settle, the shutdown flush — is in `application.cpp`,
which is not compiled into the test binary (the binary has no window layer). The serialisation and
the file round trip are tested; the three call sites are reviewed and compiled. Putting them under
test means a headless `Application`, which is a larger change than this defect is worth and is
recorded here rather than done.

`AutoDirectorSettings` also gained a defaulted `operator==`. The panel compared it with `std::memcmp`
over a struct with padding in it; the host needed a comparison that is about the fields.
