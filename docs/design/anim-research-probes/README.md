# Probes for `docs/design/autonomous-character-animation.md`

Throwaway measurement programs, kept because the report cites their numbers and a number whose
probe cannot be re-run is a number nobody can check.

**None of these links or includes anything from AV Gen**, and none is in the build. They read
assets and print. Every one is ADR-182-safe: each has an arm that would detect a broken probe.

Run from a worktree root:

```
python3 docs/design/anim-research-probes/clipstats.py assets/aliens/alien-scout.glb
python3 docs/design/anim-research-probes/rootpath.py            # paths are relative to the root
clang++ -std=c++20 -O2 -o /tmp/mmprobe  docs/design/anim-research-probes/mmprobe.cpp  && /tmp/mmprobe 200
clang++ -std=c++20 -O2 -o /tmp/mmprobe2 docs/design/anim-research-probes/mmprobe2.cpp && /tmp/mmprobe2 53500 27
clang++ -std=c++20 -O2 -o /tmp/pose     docs/design/anim-research-probes/poseprobe.cpp && /tmp/pose 20000
```

| probe | question | report |
|---|---|---|
| `clipstats.py` | how much motion does AV Gen own? | §1.8 |
| `rootpath.py` | do the locomotion clips travel? | §1.8 |
| `mmprobe.cpp` | what does a brute-force motion-matching query cost here? | §2.3 |
| `mmprobe2.cpp` | does the early-out win or lose, and **on what fixture**? | §2.3a |
| `poseprobe.cpp` | what does posing this rig cost, for scale? | §2.3 |

`poseprobe.cpp` is a faithful reimplementation of the *arithmetic shape* of
`sampleClip` + `poseToModel` + `jointPalette`, **not** AV Gen's code. Its number is a lower bound on
the real cost, not a measurement of the engine.

`mmprobe.cpp` produced a wrong conclusion and `mmprobe2.cpp` exists because of it. See §2.3a: the
early-out is 2.2× slower on a synthetic Gaussian database and a 20% win on motion-like data with a
near query, and the first probe used the fixture that does not occur.
