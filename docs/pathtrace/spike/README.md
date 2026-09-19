# Phase 0 Embree spike

A standalone program, built **outside** AV Gen (spec §71), that validates Embree 4.4.0 on Apple
Silicon before any engine code is written. Kept in the tree because the conclusions in ADR-348 are
only worth what they can be re-run for.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spike     # 16 checks, each with a control arm; writes spike.ppm
./build/threads   # device thread configs + rtcJoinCommitScene from caller threads
```

`spike.png` is the frame `spike` produces: a small orange triangle (z = -2) in front of a large
blue one (z = -5). Both must be visible. An earlier version of this scene gave the two triangles
identical angular size, so the far one was exactly occluded — every numeric check passed and the
image contained one triangle. The `BOTH triangles visible in the frame` check exists because of
that, and the picture is in the repo so the next reader does not have to take the numbers' word
for it.

**Do not set `CMAKE_CXX_STANDARD` globally here.** It overrides Embree's own `-std=c++11` and
Embree 4.4.0 does not compile as C++23 under Apple clang 21. See ADR-348.
