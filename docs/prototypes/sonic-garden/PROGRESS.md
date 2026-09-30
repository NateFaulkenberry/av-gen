# Sonic Garden POC: progress

Resume from here. Branch `proto/sonic-garden` in `../av-gen-sonic`. ADR block 1020-1039 (used: 1020).
Engineering agent: phases 0-4 and the engineering half of Phase 5. The art agent (sonic-art) owns the mappings,
the families and the look.

## Rules in force

- All GPU work, **including the full `avgen_tests`**, goes through `tools/gpu-lock.sh`. The CPU suite encodes
  video (see the lock script's header). A filtered run of pure-CPU `[sonic]` cases may run directly.
- Build: `cmake --preset release && cmake --build --preset release`. Reconfigure after adding a test file.
- Judge by the binary's exit code. One FAILED line (the `[!shouldfail]` slope lean) is expected on a clean CPU run.

## Status

| phase | state | notes |
|---|---|---|
| 0 research | done | `RESEARCH.md`, ADR-1020 |
| 1 audio analyzer (timbre) | in progress | |
| 2 musical analyzer (notes) | todo | |
| 3 sonic character | todo | |
| 4 interpreter | todo | |
| 5 test material + scene | todo | |
