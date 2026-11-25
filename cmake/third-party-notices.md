# Components whose fetched sources carry no licence file

These are linked into `bin/avgen` but the files AV Gen fetches for them hold no licence text, so the notice is
stated here (cmake/third-party-notices.md, read by cmake/Install.cmake, ADR-2160). Each was checked against its
project's published licence when this file was written; check again before a public release.

## Dawn and Tint (WebGPU implementation and WGSL compiler): BSD-3-Clause

Linked from Dawn's prebuilt macOS Release archive (commit c4e47b5e, nightly v20260907.201642), which ships
headers and static libraries only. The licence, from the Dawn repository's `LICENSE`
(https://dawn.googlesource.com/dawn/+/refs/heads/main/LICENSE):

```text
BSD 3-Clause License

Copyright 2017-2023 The Dawn & Tint Authors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Abseil (inside Dawn's prebuilt library): Apache-2.0

Copyright The Abseil Authors. Licensed under the Apache License, Version 2.0
(https://www.apache.org/licenses/LICENSE-2.0); the full licence text is reproduced above, under Embree.

## simdjson (compiled into fastgltf, v3.12.3 single header): Apache-2.0 or MIT

Copyright the simdjson authors. Dual-licensed under the Apache License, Version 2.0
(https://www.apache.org/licenses/LICENSE-2.0, full text above under Embree) or the MIT licence, at the
recipient's choice (https://github.com/simdjson/simdjson/blob/master/LICENSE).
