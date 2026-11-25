#!/bin/bash
# Builds the AV Gen engine/API package (ADR-2160): a folder and a tarball an external client can use
# with no access to this source tree.
#
#   tools/package_poc.sh --out <dir> [--jobs N] [--build-dir <dir>]
#
# <dir> receives avgen-poc-<revision>/ and avgen-poc-<revision>.tar.gz (+ .sha256). It must be outside
# every git checkout: a package never goes into the repository.
#
# Steps, each of which fails the script:
#   1. configure and build `avgen` in a dedicated build dir (default build/package-poc) with
#      -DAVGEN_DEV_SOURCE_PATHS=OFF -DAVGEN_BUILD_TESTS=OFF -DAVGEN_BUNDLE_ID=com.avgen.avgen.poc
#      -DCMAKE_BUILD_TYPE=Release (CPM_SOURCE_CACHE is passed through when set);
#   2. `cmake --install` into the staging folder (cmake/Install.cmake is the layout);
#   3. the allow-list: every staged file must be in the layout, nothing may look like an asset, no symlinks;
#   4. `strings bin/avgen`: no source root, no CPM cache, no home directory, no /Users/ path except the
#      Dawn prebuilt's own CI paths (/Users/runner/work/dawn/, compiled into Google's archive); and
#      `otool -L` lists only system libraries;
#   5. `bin/avgen --version` and `--describe version` run from / with a scrubbed environment, and the
#      latter matches VERSION.json;
#   6. the tarball and its SHA-256.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
OUT=""
JOBS=4
BUILD="$ROOT/build/package-poc"
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --build-dir) BUILD="$2"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "package_poc: unknown argument '$1'" >&2; exit 2 ;;
    esac
done
[ -n "$OUT" ] || { echo "package_poc: pass --out <dir> (outside every checkout)" >&2; exit 2; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd -P)"
case "$OUT/" in
    "$ROOT"/*) echo "package_poc: --out $OUT is inside the source tree" >&2; exit 2 ;;
esac
if git -C "$OUT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "package_poc: --out $OUT is inside a git checkout; a package never goes into a repository" >&2
    exit 2
fi

step() { printf '\n== %s\n' "$*"; }
die() { echo "package_poc: FAIL: $*" >&2; exit 1; }

REV="$(git -C "$ROOT" rev-parse --short=12 HEAD)"
if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ]; then
    echo "package_poc: WARNING: the tree has uncommitted changes; VERSION.json will say dirty" >&2
    REV="$REV-dirty"
fi
NAME="avgen-poc-$REV"
STAGE="$OUT/$NAME"

step "1. configure and build ($BUILD)"
CPM_ARGS=()
[ -n "${CPM_SOURCE_CACHE:-}" ] && CPM_ARGS=("-DCPM_SOURCE_CACHE=$CPM_SOURCE_CACHE")
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAVGEN_DEV_SOURCE_PATHS=OFF \
    -DAVGEN_BUILD_TESTS=OFF \
    -DAVGEN_BUNDLE_ID=com.avgen.avgen.poc \
    -DAVGEN_PATHTRACE_DENOISE=OFF \
    "${CPM_ARGS[@]+"${CPM_ARGS[@]}"}"
grep -q '^AVGEN_DEV_SOURCE_PATHS:BOOL=OFF$' "$BUILD/CMakeCache.txt" || die "AVGEN_DEV_SOURCE_PATHS is not OFF in $BUILD"
cmake --build "$BUILD" --target avgen -j "$JOBS"
if grep -q 'AVGEN_SOURCE_DIR\|AVGEN_SHADER_SOURCE_DIR' "$BUILD/compile_commands.json"; then
    die "a compile command still defines AVGEN_SOURCE_DIR or AVGEN_SHADER_SOURCE_DIR"
fi

step "2. install into $STAGE"
rm -rf "$STAGE" "$OUT/$NAME.tar.gz" "$OUT/$NAME.tar.gz.sha256"
cmake --install "$BUILD" --prefix "$STAGE"

step "3. allow-list"
bad=0
while IFS= read -r -d '' f; do
    rel="${f#"$STAGE"/}"
    case "$rel" in
        bin/avgen|VERSION.json|THIRD-PARTY.md|share/avgen/docs/README.md) ;;
        bin/shaders/*.wgsl) ;;
        share/avgen/schemas/*.json) [[ "$rel" == share/avgen/schemas/*/* ]] && { echo "  not in the layout: $rel"; bad=1; } ;;
        share/avgen/docs/api/*.md) [[ "$rel" == share/avgen/docs/api/*/* ]] && { echo "  not in the layout: $rel"; bad=1; } ;;
        *) echo "  not in the layout: $rel"; bad=1 ;;
    esac
    # Anything that looks like an asset, whatever folder it is in.
    if [[ "/$rel" == */assets/* || "/$rel" == */examples/* || "/$rel" == */_thirdparty/* ]]; then
        echo "  asset-like path: $rel"; bad=1
    fi
    case "$(printf '%s' "$rel" | tr '[:upper:]' '[:lower:]')" in
        *.glb|*.gltf|*.bin|*.fbx|*.obj|*.blend|*.usd*|*.png|*.jpg|*.jpeg|*.hdr|*.exr|*.ktx2|*.webp|*.tga|\
        *.wav|*.aif|*.aiff|*.flac|*.mp3|*.ogg|*.m4a|*.mp4|*.mov|*.bvh|*.npz|*.ttf|*.otf|*.zip|*.dylib)
            echo "  asset-like file: $rel"; bad=1 ;;
    esac
done < <(find "$STAGE" \( -type f -o -type l \) -print0)
links="$(find "$STAGE" -type l)"
[ -z "$links" ] || { echo "  symlinks: $links"; bad=1; }
[ "$bad" = 0 ] || die "the staged tree has files outside the layout (cmake/Install.cmake)"
for need in bin/avgen VERSION.json THIRD-PARTY.md share/avgen/docs/README.md share/avgen/docs/api/tools.md \
            share/avgen/docs/api/protocol.md share/avgen/docs/api/errors.md share/avgen/docs/api/transactions.md \
            share/avgen/docs/api/gltf-export.md share/avgen/schemas/tool-catalogue.json \
            share/avgen/schemas/avgen-project.schema.json share/avgen/schemas/avgen-scene.schema.json; do
    [ -s "$STAGE/$need" ] || die "missing from the package: $need"
done
nshaders="$(find "$STAGE/bin/shaders" -name '*.wgsl' | wc -l | tr -d ' ')"
nsrc="$(find "$ROOT/shaders" -name '*.wgsl' -not -path "$ROOT/shaders/examples/*" | wc -l | tr -d ' ')"
[ "$nshaders" = "$nsrc" ] || die "bin/shaders has $nshaders .wgsl files; shaders/ has $nsrc"
echo "  ok: $(find "$STAGE" -type f | wc -l | tr -d ' ') files, $nshaders shaders, nothing outside the layout"

step "4. strings and libraries"
BIN="$STAGE/bin/avgen"
STRINGS="$(mktemp -t avgen-strings)"
trap 'rm -f "$STRINGS"' EXIT
strings -a "$BIN" > "$STRINGS"
for needle in "$ROOT" "$(dirname "$ROOT")" "$HOME" ${CPM_SOURCE_CACHE:+"$CPM_SOURCE_CACHE"}; do
    n="$(grep -cF -- "$needle" "$STRINGS" || true)"
    echo "  strings | grep -c '$needle' = $n"
    [ "$n" = 0 ] || { grep -F -- "$needle" "$STRINGS" | head -5; die "bin/avgen contains '$needle'"; }
done
users="$(grep -F '/Users/' "$STRINGS" | grep -vF '/Users/runner/work/dawn/' || true)"
echo "  /Users/ paths other than the Dawn prebuilt's CI paths: $(printf '%s' "$users" | grep -c . || true)"
[ -z "$users" ] || { printf '%s\n' "$users" | head -5; die "bin/avgen contains a /Users/ path"; }
libs="$(otool -L "$BIN" | tail -n +2 | awk '{print $1}')"
nonsys="$(printf '%s\n' "$libs" | grep -v '^/System/' | grep -v '^/usr/lib/' || true)"
[ -z "$nonsys" ] || die "bin/avgen links non-system libraries: $nonsys"
echo "  otool -L: $(printf '%s\n' "$libs" | wc -l | tr -d ' ') libraries, all under /System or /usr/lib"

step "5. run from / with a scrubbed environment"
(cd / && env -i HOME="$HOME" PATH=/usr/bin:/bin "$BIN" --version)
(cd / && env -i HOME="$HOME" PATH=/usr/bin:/bin "$BIN" --describe version) > "$STRINGS.version"
cmp -s "$STRINGS.version" "$STAGE/VERSION.json" || die "--describe version from / differs from VERSION.json"
rm -f "$STRINGS.version"
cat "$STAGE/VERSION.json"

step "6. tarball"
tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
(cd "$OUT" && shasum -a 256 "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
cat "$OUT/$NAME.tar.gz.sha256"
echo
echo "package: $STAGE"
echo "tarball: $OUT/$NAME.tar.gz"
