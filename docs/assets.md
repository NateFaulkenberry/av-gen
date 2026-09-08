# Assets

Decision: ADR-005. Research: `docs/research/assets.md`.

Milestone 0.1 ships no asset loaders. Geometry is procedural (`scene::makeIcosphere`,
`makeCube`, `makePlane`) in glTF conventions (right-handed, +Y up, metres, CCW), and `scene::Vertex`
carries position/normal/uv so glTF meshes drop in without a format change. Textures, HDR
environments and animation arrive in 0.2 with fastgltf, meshoptimizer, stb_image, tinyexr, libktx
and ozz-animation (all permissive). Asset references will be project-relative paths plus a GUID
in the project file; the asset registry will hand out generational handles and load
asynchronously with placeholders.

Runtime assets today: `shaders/*.wgsl` (see `docs/shaders.md`) and the audio file chosen by the
user. `tools/make_test_audio.py` generates a deterministic test track; nothing binary is
committed.
