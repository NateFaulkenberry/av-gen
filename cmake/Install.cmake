# ADR-2160: the install layout of the engine/API package (`cmake --install`, tools/package_poc.sh).
#
#   bin/avgen                        the engine; `avgen serve --stdio`, `avgen call`, `avgen --describe`
#   bin/shaders/**.wgsl              the shader library (ShaderLibrary::defaultSearchDirs: <exe>/shaders),
#                                    without shaders/examples/ (the example projects' sample layers)
#   share/avgen/schemas/*.json       the generated contracts (schemas/)
#   share/avgen/docs/README.md       the package's entry point (docs/api/README.md)
#   share/avgen/docs/api/*.md        the generated and hand-written API reference (docs/api/)
#   VERSION.json                     `avgen --describe version`, run on the installed binary
#   THIRD-PARTY.md                   CREDITS.md and the licences of everything linked into bin/avgen
#
# Nothing under assets/, examples/, docs/help or labs is installed: `serve` reads none of them, and the
# repository's assets include commercial and third-party files that must never leave it (the repo is
# public). A packaged editor is out of scope. tools/package_poc.sh checks the staged tree against this
# list and fails on anything else.

install(TARGETS avgen RUNTIME DESTINATION bin)
# shaders/examples/ holds the sample shader layers the example projects use, not the engine's library: not installed.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders/" DESTINATION bin/shaders
        FILES_MATCHING PATTERN "*.wgsl" PATTERN "examples" EXCLUDE)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/schemas/" DESTINATION share/avgen/schemas
        FILES_MATCHING PATTERN "*.json")
install(FILES "${CMAKE_SOURCE_DIR}/docs/api/README.md" DESTINATION share/avgen/docs)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/docs/api/" DESTINATION share/avgen/docs/api
        FILES_MATCHING PATTERN "*.md" PATTERN "README.md" EXCLUDE)

# VERSION.json from the installed binary itself, so the file and the executable cannot disagree and the
# install proves the binary starts. DESTDIR-aware, like the rules above.
install(CODE [[
    set(_avgen_prefix "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    execute_process(COMMAND "${_avgen_prefix}/bin/avgen" --describe version
        WORKING_DIRECTORY /
        OUTPUT_FILE "${_avgen_prefix}/VERSION.json"
        RESULT_VARIABLE _avgen_status)
    if(NOT _avgen_status EQUAL 0)
        message(FATAL_ERROR "avgen --describe version failed (${_avgen_status}); VERSION.json not written")
    endif()
]])

# ---- THIRD-PARTY.md ------------------------------------------------------------------------------
# CREDITS.md says the libraries' licence texts must travel with a redistributed binary. Each statically
# linked library's own licence file, read from its fetched sources at configure time; the three that
# ship none in what is fetched (Dawn's prebuilt archive, the abseil inside it, fastgltf's bundled
# simdjson) are stated in cmake/third-party-notices.md. A package build (AVGEN_DEV_SOURCE_PATHS=OFF) fails
# when a licence file is missing; a development build only warns.
set(AVGEN_THIRD_PARTY_FILE "${CMAKE_BINARY_DIR}/THIRD-PARTY.md")
file(READ "${CMAKE_SOURCE_DIR}/CREDITS.md" _avgen_credits)
file(READ "${CMAKE_SOURCE_DIR}/cmake/third-party-notices.md" _avgen_notices)
string(CONCAT _avgen_tp "# Third-party notices for this AV Gen package\n\n"
    "This file has three parts: AV Gen's credits (CREDITS.md, verbatim), the licence of each library linked "
    "into `bin/avgen`, and the notices for the components whose fetched sources carry no licence file.\n\n"
    "`bin/avgen` links every library below statically. It links no other third-party code: NDI and ffmpeg are "
    "loaded or started only if the user has installed them, and their licences stay the user's. Open Image "
    "Denoise is not in this build. No third-party assets (models, textures, audio, motion data) are in this "
    "package.\n\n---\n\n${_avgen_credits}\n\n---\n\n# Library licences\n\n")
set(_avgen_tp_missing "")
# name | licence | version | source dir | licence file(s), relative to the source dir
function(_avgen_licence name spdx version dir)
    set(_tp "${_avgen_tp}")
    set(_missing "${_avgen_tp_missing}")
    string(APPEND _tp "## ${name} (${version}): ${spdx}\n\n")
    foreach(_f ${ARGN})
        if(NOT "${dir}" STREQUAL "" AND EXISTS "${dir}/${_f}")
            file(READ "${dir}/${_f}" _text)
            string(APPEND _tp "`${_f}`:\n\n```text\n${_text}\n```\n\n")
        else()
            list(APPEND _missing "${name}: ${_f}")
            string(APPEND _tp "(the licence file `${_f}` was not found when this package was built)\n\n")
        endif()
    endforeach()
    set(_avgen_tp "${_tp}" PARENT_SCOPE)
    set(_avgen_tp_missing "${_missing}" PARENT_SCOPE)
endfunction()
_avgen_licence("fmt" "MIT" "12.2.0" "${fmt_SOURCE_DIR}" LICENSE)
_avgen_licence("spdlog" "MIT" "v1.17.0" "${spdlog_SOURCE_DIR}" LICENSE)
_avgen_licence("GLM" "MIT (or the Happy Bunny Licence)" "1.0.3" "${glm_SOURCE_DIR}" copying.txt)
_avgen_licence("nlohmann/json" "MIT" "v3.12.0" "${nlohmann_json_SOURCE_DIR}" LICENSE.MIT)
_avgen_licence("miniaudio" "MIT-0 or public domain" "0.11.25" "${miniaudio_SOURCE_DIR}" LICENSE)
_avgen_licence("KissFFT" "BSD-3-Clause" "131.2.0" "${kissfft_SOURCE_DIR}" COPYING LICENSES/BSD-3-Clause)
_avgen_licence("fastgltf" "MIT" "v0.9.0" "${fastgltf_SOURCE_DIR}" LICENSE.md)
_avgen_licence("meshoptimizer" "MIT" "v1.2" "${meshoptimizer_SOURCE_DIR}" LICENSE.md)
_avgen_licence("stb" "MIT or public domain" "2c980bb" "${stb_SOURCE_DIR}" LICENSE)
_avgen_licence("tinyexr" "BSD-3-Clause" "v3.2.0" "${tinyexr_SOURCE_DIR}" LICENSE)
_avgen_licence("miniz (inside tinyexr)" "MIT" "bundled with tinyexr v3.2.0" "${tinyexr_SOURCE_DIR}" deps/miniz/LICENSE)
_avgen_licence("Embree (and its sse2neon.h, MIT)" "Apache-2.0" "v4.4.0" "${embree_SOURCE_DIR}" LICENSE.txt)
_avgen_licence("SDL3" "Zlib" "release-3.4.16" "${SDL3_SOURCE_DIR}" LICENSE.txt)
_avgen_licence("Dear ImGui" "MIT" "v1.92.9b-docking" "${imgui_SOURCE_DIR}" LICENSE.txt)
_avgen_licence("ImPlot" "MIT" "v1.0" "${implot_SOURCE_DIR}" LICENSE)
if(APPLE)
    _avgen_licence("Syphon-Framework" "BSD-2-Clause" "71351d4" "${syphon_SOURCE_DIR}" License.txt)
endif()
string(APPEND _avgen_tp "---\n\n${_avgen_notices}")
if(_avgen_tp_missing)
    if(AVGEN_DEV_SOURCE_PATHS)
        message(WARNING "avgen: THIRD-PARTY.md is missing licence files: ${_avgen_tp_missing}")
    else()
        message(FATAL_ERROR "avgen: a package needs every licence file; missing: ${_avgen_tp_missing}")
    endif()
endif()
# Written through a temporary so an unchanged file keeps its timestamp.
file(WRITE "${AVGEN_THIRD_PARTY_FILE}.tmp" "${_avgen_tp}")
file(COPY_FILE "${AVGEN_THIRD_PARTY_FILE}.tmp" "${AVGEN_THIRD_PARTY_FILE}" ONLY_IF_DIFFERENT)
file(REMOVE "${AVGEN_THIRD_PARTY_FILE}.tmp")
install(FILES "${AVGEN_THIRD_PARTY_FILE}" DESTINATION .)
